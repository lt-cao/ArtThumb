#include "Provider/Module.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <string>

namespace {

constexpr const wchar_t* kExtensions[] = {
    L".psd", L".psb", L".ai", L".eps", L".indd", L".pdf", L".svg"
};

std::wstring ParentDirectory(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring CurrentExecutable() {
    std::wstring path(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}

bool EnsureDirectory(const std::wstring& path) {
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
           GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool SetString(HKEY root, const std::wstring& subkey, const wchar_t* name,
               const std::wstring& value) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, &disposition) != ERROR_SUCCESS) return false;
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG error = RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return error == ERROR_SUCCESS;
}

bool DeleteNamedValue(HKEY root, const std::wstring& subkey, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return true;
    const LONG error = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND;
}

bool SetDword(HKEY root, const std::wstring& subkey, const wchar_t* name, DWORD value) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, &disposition) != ERROR_SUCCESS) return false;
    const LONG error = RegSetValueExW(key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return error == ERROR_SUCCESS;
}

bool QueryDword(HKEY root, const std::wstring& subkey, const wchar_t* name,
                DWORD& value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    const LONG error = RegQueryValueExW(key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &bytes);
    RegCloseKey(key);
    return error == ERROR_SUCCESS && type == REG_DWORD;
}

bool QueryString(HKEY root, const std::wstring& subkey, const wchar_t* name,
                 std::wstring& value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD bytes = 0;
    LONG error = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (error != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }
    value.assign(bytes / sizeof(wchar_t), L'\0');
    error = RegQueryValueExW(key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (error != ERROR_SUCCESS) return false;
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return true;
}

bool QueryDefaultString(HKEY root, const std::wstring& subkey, std::wstring& value) {
    return QueryString(root, subkey, nullptr, value);
}

std::wstring AssociationKey(const wchar_t* extension) {
    return std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + extension;
}

std::wstring OverlayKey(const wchar_t* extension) {
    wchar_t progId[512]{};
    DWORD length = ARRAYSIZE(progId);
    if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, extension, nullptr,
                                    progId, &length)) && progId[0] != L'\0')
        return std::wstring(L"Software\\Classes\\") + progId;
    return AssociationKey(extension);
}

std::wstring HandlerKey(const wchar_t* extension) {
    return AssociationKey(extension) + L"\\ShellEx\\" +
           artthumb::kThumbnailHandlerIidText;
}

std::wstring BackupKey(const wchar_t* extension) {
    return std::wstring(L"Software\\ArtThumb\\Backups\\") + extension;
}

void BackUpHandler(const wchar_t* extension) {
    DWORD made = 0;
    const std::wstring backup = BackupKey(extension);
    if (QueryDword(HKEY_CURRENT_USER, backup, L"BackupMade", made) && made == 1) return;
    std::wstring current;
    const bool hadValue = QueryDefaultString(HKEY_CURRENT_USER, HandlerKey(extension), current);
    SetDword(HKEY_CURRENT_USER, backup, L"HadValue", hadValue ? 1 : 0);
    if (hadValue) SetString(HKEY_CURRENT_USER, backup, L"Value", current);
    SetDword(HKEY_CURRENT_USER, backup, L"BackupMade", 1);
}

void RestoreHandler(const wchar_t* extension) {
    DWORD made = 0;
    DWORD hadValue = 0;
    const std::wstring backup = BackupKey(extension);
    if (!QueryDword(HKEY_CURRENT_USER, backup, L"BackupMade", made) || made != 1) {
        std::wstring current;
        if (QueryDefaultString(HKEY_CURRENT_USER, HandlerKey(extension), current) &&
            _wcsicmp(current.c_str(), artthumb::kThumbnailProviderClsidText) == 0)
            SHDeleteKeyW(HKEY_CURRENT_USER, HandlerKey(extension).c_str());
        return;
    }
    QueryDword(HKEY_CURRENT_USER, backup, L"HadValue", hadValue);
    if (hadValue == 1) {
        std::wstring oldValue;
        if (QueryString(HKEY_CURRENT_USER, backup, L"Value", oldValue))
            SetString(HKEY_CURRENT_USER, HandlerKey(extension), nullptr, oldValue);
    } else {
        SHDeleteKeyW(HKEY_CURRENT_USER, HandlerKey(extension).c_str());
    }
}

void BackUpOverlay(const wchar_t* extension) {
    DWORD made = 0;
    const std::wstring backup = BackupKey(extension);
    if (QueryDword(HKEY_CURRENT_USER, backup, L"OverlayBackupMade", made) && made == 1)
        return;
    const std::wstring target = OverlayKey(extension);
    std::wstring current;
    const bool hadValue = QueryString(HKEY_CURRENT_USER, target, L"TypeOverlay", current);
    SetString(HKEY_CURRENT_USER, backup, L"OverlayTarget", target);
    SetDword(HKEY_CURRENT_USER, backup, L"OverlayHadValue", hadValue ? 1 : 0);
    if (hadValue) SetString(HKEY_CURRENT_USER, backup, L"OverlayValue", current);
    SetDword(HKEY_CURRENT_USER, backup, L"OverlayBackupMade", 1);
}

bool ConfigureOverlay(const wchar_t* extension) {
    BackUpOverlay(extension);
    std::wstring target;
    if (!QueryString(HKEY_CURRENT_USER, BackupKey(extension), L"OverlayTarget", target))
        return false;
    // An absent TypeOverlay tells Explorer to use the associated app's default
    // icon as a native thumbnail overlay. An empty string disables the overlay.
    if (!SetDword(HKEY_CURRENT_USER, BackupKey(extension),
                  L"InstalledOverlayPresent", 0))
        return false;
    return DeleteNamedValue(HKEY_CURRENT_USER, target, L"TypeOverlay");
}

void RestoreOverlay(const wchar_t* extension) {
    const std::wstring backup = BackupKey(extension);
    DWORD made = 0;
    if (!QueryDword(HKEY_CURRENT_USER, backup, L"OverlayBackupMade", made) || made != 1)
        return;
    std::wstring target;
    DWORD installedPresent = 0;
    const bool hasInstallState = QueryDword(HKEY_CURRENT_USER, backup,
                                             L"InstalledOverlayPresent", installedPresent);
    std::wstring installed;
    std::wstring current;
    if (!QueryString(HKEY_CURRENT_USER, backup, L"OverlayTarget", target))
        return;
    const bool hasCurrent = QueryString(HKEY_CURRENT_USER, target, L"TypeOverlay", current);
    if (hasInstallState) {
        if ((installedPresent != 0 && (!hasCurrent ||
             !QueryString(HKEY_CURRENT_USER, backup, L"InstalledOverlay", installed) ||
             current != installed)) || (installedPresent == 0 && hasCurrent))
            return;
    } else {
        // Backward compatibility with 1.2.1, which installed an empty value.
        if (!hasCurrent ||
            !QueryString(HKEY_CURRENT_USER, backup, L"InstalledOverlay", installed) ||
            current != installed)
            return;
    }
    DWORD hadValue = 0;
    QueryDword(HKEY_CURRENT_USER, backup, L"OverlayHadValue", hadValue);
    if (hadValue == 1) {
        std::wstring oldValue;
        if (QueryString(HKEY_CURRENT_USER, backup, L"OverlayValue", oldValue))
            SetString(HKEY_CURRENT_USER, target, L"TypeOverlay", oldValue);
    } else {
        DeleteNamedValue(HKEY_CURRENT_USER, target, L"TypeOverlay");
    }
}

std::wstring InstallDirectory() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                    nullptr, &localAppData))) return {};
    std::wstring result(localAppData);
    CoTaskMemFree(localAppData);
    return result + L"\\Programs\\ArtThumb";
}

std::wstring StartMenuDirectory() {
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE,
                                    nullptr, &programs))) return {};
    std::wstring result(programs);
    CoTaskMemFree(programs);
    return result + L"\\ArtThumb";
}

bool CreateSettingsShortcut(const std::wstring& settingsPath,
                            const std::wstring& installDirectory) {
    const std::wstring menuDirectory = StartMenuDirectory();
    if (menuDirectory.empty() || !EnsureDirectory(menuDirectory)) return false;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))) return false;
    HRESULT hr = link->SetPath(settingsPath.c_str());
    if (SUCCEEDED(hr)) hr = link->SetWorkingDirectory(installDirectory.c_str());
    if (SUCCEEDED(hr)) hr = link->SetDescription(L"Cài đặt và xem trước thumbnail ArtThumb");
    if (SUCCEEDED(hr)) hr = link->SetIconLocation(settingsPath.c_str(), 0);
    IPersistFile* persist = nullptr;
    if (SUCCEEDED(hr)) hr = link->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(hr)) {
        const std::wstring shortcut = menuDirectory + L"\\ArtThumb Settings.lnk";
        hr = persist->Save(shortcut.c_str(), TRUE);
    }
    if (persist) persist->Release();
    link->Release();
    return SUCCEEDED(hr);
}

void RemoveSettingsShortcut() {
    const std::wstring menuDirectory = StartMenuDirectory();
    if (menuDirectory.empty()) return;
    DeleteFileW((menuDirectory + L"\\ArtThumb Settings.lnk").c_str());
    RemoveDirectoryW(menuDirectory.c_str());
}

bool RegisterProvider(const std::wstring& dllPath, const std::wstring& uninstallPath,
                      const std::wstring& settingsPath,
                      const std::wstring& installDirectory) {
    const std::wstring clsid = std::wstring(L"Software\\Classes\\CLSID\\") +
                               artthumb::kThumbnailProviderClsidText;
    if (!SetString(HKEY_CURRENT_USER, clsid, nullptr, L"ArtThumb Thumbnail Provider") ||
        !SetString(HKEY_CURRENT_USER, clsid + L"\\InprocServer32", nullptr, dllPath) ||
        !SetString(HKEY_CURRENT_USER, clsid + L"\\InprocServer32", L"ThreadingModel", L"Apartment"))
        return false;
    for (const wchar_t* extension : kExtensions) {
        BackUpHandler(extension);
        if (!SetString(HKEY_CURRENT_USER, HandlerKey(extension), nullptr,
                       artthumb::kThumbnailProviderClsidText)) return false;
        if (!ConfigureOverlay(extension)) return false;
    }

    const std::wstring uninstallKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ArtThumb";
    const std::wstring command = L"\"" + uninstallPath + L"\" /uninstall";
    if (!SetString(HKEY_CURRENT_USER, uninstallKey, L"DisplayName", L"ArtThumb") ||
        !SetString(HKEY_CURRENT_USER, uninstallKey, L"DisplayVersion", artthumb::kProductVersion) ||
        !SetString(HKEY_CURRENT_USER, uninstallKey, L"Publisher", artthumb::kProductAuthor) ||
        !SetString(HKEY_CURRENT_USER, uninstallKey, L"DisplayIcon", settingsPath) ||
        !SetString(HKEY_CURRENT_USER, uninstallKey, L"InstallLocation", installDirectory) ||
        !SetString(HKEY_CURRENT_USER, uninstallKey, L"UninstallString", command) ||
        !SetDword(HKEY_CURRENT_USER, uninstallKey, L"NoModify", 1) ||
        !SetDword(HKEY_CURRENT_USER, uninstallKey, L"NoRepair", 1)) return false;
    SetString(HKEY_CURRENT_USER, L"Software\\ArtThumb", L"InstallLocation", installDirectory);
    SetString(HKEY_CURRENT_USER, L"Software\\ArtThumb", L"SettingsPath", settingsPath);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

int Install() {
    const std::wstring executable = CurrentExecutable();
    const std::wstring sourceDll = ParentDirectory(executable) + L"\\ArtThumbProvider.dll";
    const std::wstring sourceSettings = ParentDirectory(executable) + L"\\ArtThumbSettings.exe";
    if (GetFileAttributesW(sourceDll.c_str()) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(sourceSettings.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr,
            L"Không tìm thấy đủ ArtThumbProvider.dll và ArtThumbSettings.exe cạnh bộ cài. Hãy giải nén toàn bộ gói rồi chạy lại ArtThumbSetup.exe.",
            L"ArtThumb", MB_OK | MB_ICONERROR);
        return 2;
    }
    const std::wstring directory = InstallDirectory();
    if (directory.empty() || !EnsureDirectory(directory)) {
        MessageBoxW(nullptr, L"Không thể tạo thư mục cài đặt.", L"ArtThumb", MB_OK | MB_ICONERROR);
        return 3;
    }
    const std::wstring destinationDll = directory + L"\\ArtThumbProvider.dll";
    const std::wstring destinationSettings = directory + L"\\ArtThumbSettings.exe";
    const std::wstring uninstaller = directory + L"\\Uninstall.exe";
    if (!CopyFileW(sourceDll.c_str(), destinationDll.c_str(), FALSE) ||
        !CopyFileW(sourceSettings.c_str(), destinationSettings.c_str(), FALSE) ||
        !CopyFileW(executable.c_str(), uninstaller.c_str(), FALSE)) {
        MessageBoxW(nullptr, L"Không thể sao chép file cài đặt. Hãy đóng ArtThumb Settings và File Explorer rồi thử lại.",
                    L"ArtThumb", MB_OK | MB_ICONERROR);
        return 4;
    }
    if (!RegisterProvider(destinationDll, uninstaller, destinationSettings, directory)) {
        MessageBoxW(nullptr, L"Không thể đăng ký thumbnail provider cho tài khoản Windows hiện tại.",
                    L"ArtThumb", MB_OK | MB_ICONERROR);
        return 5;
    }
    CreateSettingsShortcut(destinationSettings, directory);
    MessageBoxW(nullptr,
        L"Đã cài ArtThumb cho PSD, PSB, AI, EPS, INDD, PDF và SVG.\n\nArtThumb Settings sẽ mở để bạn xem trước icon ứng dụng và chỉnh độ sắc nét thumbnail. Windows Explorer tự phủ icon ứng dụng riêng ở góc; ảnh thumbnail không bị sửa. Nếu thư mục đang mở chưa đổi, hãy đóng rồi mở lại File Explorer.",
        L"ArtThumb", MB_OK | MB_ICONINFORMATION);
    ShellExecuteW(nullptr, L"open", destinationSettings.c_str(), nullptr,
                  directory.c_str(), SW_SHOWNORMAL);
    return 0;
}

int Uninstall() {
    const std::wstring directory = InstallDirectory();
    const std::wstring destinationDll = directory + L"\\ArtThumbProvider.dll";
    const std::wstring destinationSettings = directory + L"\\ArtThumbSettings.exe";
    const std::wstring uninstaller = directory + L"\\Uninstall.exe";
    for (const wchar_t* extension : kExtensions) {
        RestoreHandler(extension);
        RestoreOverlay(extension);
    }

    const std::wstring clsid = std::wstring(L"Software\\Classes\\CLSID\\") +
                               artthumb::kThumbnailProviderClsidText;
    SHDeleteKeyW(HKEY_CURRENT_USER, clsid.c_str());
    SHDeleteKeyW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ArtThumb");
    SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\ArtThumb");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    RemoveSettingsShortcut();
    DeleteFileW(destinationDll.c_str());
    DeleteFileW(destinationSettings.c_str());
    const std::wstring command = L"/d /q /c \"timeout /t 3 /nobreak >nul & del /f /q \"\"" +
        destinationDll + L"\"\" \"\"" + destinationSettings + L"\"\" \"\"" +
        uninstaller + L"\"\" & rmdir \"\"" + directory + L"\"\"\"";
    const std::wstring runOnceCommand = L"C:\\Windows\\System32\\cmd.exe " + command;
    SetString(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
        L"ArtThumbCleanup", runOnceCommand);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring mutableCommand = command;
    if (CreateProcessW(L"C:\\Windows\\System32\\cmd.exe", mutableCommand.data(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    MessageBoxW(nullptr, L"Đã gỡ ArtThumb và khôi phục thumbnail provider trước đó.",
                L"ArtThumb", MB_OK | MB_ICONINFORMATION);
    return 0;
}

bool HasArgument(const wchar_t* expected) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return false;
    bool found = false;
    for (int index = 1; index < count; ++index) {
        if (_wcsicmp(arguments[index], expected) == 0) found = true;
    }
    LocalFree(arguments);
    return found;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const int result = HasArgument(L"/uninstall") ? Uninstall() : Install();
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
