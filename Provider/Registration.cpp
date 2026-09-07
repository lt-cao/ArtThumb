#include "Provider/Registration.h"

#include "Provider/Module.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>

namespace artthumb {
namespace {

constexpr const wchar_t* kExtensions[] = {
    L".psd", L".psb", L".ai", L".eps", L".indd", L".pdf"
};

HRESULT FromWin32(LONG value) noexcept {
    return value == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(value);
}

HRESULT SetDefaultValue(HKEY root, const std::wstring& subkey, const wchar_t* value) noexcept {
    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG error = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                 KEY_SET_VALUE, nullptr, &key, &disposition);
    if (error != ERROR_SUCCESS) return FromWin32(error);
    const DWORD bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
    error = RegSetValueExW(key, nullptr, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(value), bytes);
    RegCloseKey(key);
    return FromWin32(error);
}

HRESULT SetNamedDword(HKEY root, const std::wstring& subkey,
                      const wchar_t* name, DWORD value) noexcept {
    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG error = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                 KEY_SET_VALUE, nullptr, &key, &disposition);
    if (error != ERROR_SUCCESS) return FromWin32(error);
    error = RegSetValueExW(key, name, 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return FromWin32(error);
}

bool DefaultValueEquals(HKEY root, const std::wstring& subkey, const wchar_t* expected) noexcept {
    wchar_t value[128]{};
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    LONG error = RegQueryValueExW(key, nullptr, nullptr, &type,
                                  reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    return error == ERROR_SUCCESS && type == REG_SZ &&
           _wcsicmp(value, expected) == 0;
}

} // namespace

HRESULT RegisterProviderForCurrentUser() noexcept {
    wchar_t dllPath[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(g_module, dllPath, ARRAYSIZE(dllPath));
    if (length == 0 || length == ARRAYSIZE(dllPath)) return HRESULT_FROM_WIN32(GetLastError());

    const std::wstring clsidKey =
        std::wstring(L"Software\\Classes\\CLSID\\") + kThumbnailProviderClsidText;
    HRESULT hr = SetDefaultValue(HKEY_CURRENT_USER, clsidKey, L"ArtThumb Thumbnail Provider");
    if (FAILED(hr)) return hr;
    hr = SetDefaultValue(HKEY_CURRENT_USER, clsidKey + L"\\InprocServer32", dllPath);
    if (FAILED(hr)) return hr;
    HKEY inproc = nullptr;
    LONG error = RegOpenKeyExW(HKEY_CURRENT_USER, (clsidKey + L"\\InprocServer32").c_str(),
                               0, KEY_SET_VALUE, &inproc);
    if (error != ERROR_SUCCESS) return FromWin32(error);
    const wchar_t apartment[] = L"Apartment";
    error = RegSetValueExW(inproc, L"ThreadingModel", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(apartment), sizeof(apartment));
    RegCloseKey(inproc);
    if (error != ERROR_SUCCESS) return FromWin32(error);

    for (const wchar_t* extension : kExtensions) {
        const std::wstring handler = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") +
                                     extension + L"\\ShellEx\\" + kThumbnailHandlerIidText;
        hr = SetDefaultValue(HKEY_CURRENT_USER, handler, kThumbnailProviderClsidText);
        if (FAILED(hr)) return hr;
    }

    const std::wstring product = L"Software\\ArtThumb";
    hr = SetNamedDword(HKEY_CURRENT_USER, product, L"RegistrationVersion", 1);
    if (FAILED(hr)) return hr;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

HRESULT UnregisterProviderForCurrentUser() noexcept {
    for (const wchar_t* extension : kExtensions) {
        const std::wstring handler = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") +
                                     extension + L"\\ShellEx\\" + kThumbnailHandlerIidText;
        if (DefaultValueEquals(HKEY_CURRENT_USER, handler, kThumbnailProviderClsidText)) {
            SHDeleteKeyW(HKEY_CURRENT_USER, handler.c_str());
        }
    }
    const std::wstring clsidKey =
        std::wstring(L"Software\\Classes\\CLSID\\") + kThumbnailProviderClsidText;
    SHDeleteKeyW(HKEY_CURRENT_USER, clsidKey.c_str());
    SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\ArtThumb");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

} // namespace artthumb
