#include "Provider/Module.h"

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace {

constexpr wchar_t kWindowClass[] = L"ArtThumb.Update.Window";
constexpr UINT kUpdateComplete = WM_APP + 1;
constexpr UINT kCheckUpdates = WM_APP + 2;
constexpr int kCheckButton = 1001;
constexpr int kOpenButton = 1002;
constexpr int kStatusLabel = 1003;

HWND g_window = nullptr;
HWND g_checkButton = nullptr;
HWND g_openButton = nullptr;
HWND g_status = nullptr;
HFONT g_font = nullptr;
HFONT g_headingFont = nullptr;
std::wstring g_releaseUrl;

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::string JsonString(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    size_t cursor = json.find(needle);
    if (cursor == std::string_view::npos) return {};
    cursor = json.find(':', cursor + needle.size());
    if (cursor == std::string_view::npos) return {};
    cursor = json.find('"', cursor + 1);
    if (cursor == std::string_view::npos) return {};
    ++cursor;

    std::string result;
    bool escaped = false;
    for (; cursor < json.size(); ++cursor) {
        const char value = json[cursor];
        if (escaped) {
            if (value == 'n') result.push_back('\n');
            else if (value == 'r') result.push_back('\r');
            else if (value == 't') result.push_back('\t');
            else result.push_back(value);
            escaped = false;
        } else if (value == '\\') {
            escaped = true;
        } else if (value == '"') {
            return result;
        } else {
            result.push_back(value);
        }
    }
    return {};
}

std::array<int, 4> ParseVersion(std::wstring_view version) noexcept {
    std::array<int, 4> parts{};
    size_t part = 0;
    int value = 0;
    bool reading = false;
    for (wchar_t character : version) {
        if (character >= L'0' && character <= L'9') {
            value = std::min(999999, value * 10 + character - L'0');
            reading = true;
        } else if (reading) {
            parts[part++] = value;
            if (part == parts.size()) return parts;
            value = 0;
            reading = false;
        }
    }
    if (reading && part < parts.size()) parts[part] = value;
    return parts;
}

struct UpdateResult {
    bool succeeded = false;
    bool newer = false;
    std::wstring latestVersion;
    std::wstring releaseUrl;
    std::wstring message;
};

UpdateResult QueryLatestRelease() {
    UpdateResult result;
    const std::wstring userAgent = std::wstring(L"ArtThumb/") + artthumb::kProductVersion;
    HINTERNET session = WinHttpOpen(userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.message = L"Không thể khởi tạo kết nối cập nhật.";
        return result;
    }
    WinHttpSetTimeouts(session, 4000, 4000, 5000, 5000);
    HINTERNET connection = WinHttpConnect(session, L"api.github.com",
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET",
        artthumb::kGitHubLatestReleaseApiPath, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool sent = false;
    if (request) {
        const wchar_t headers[] =
            L"Accept: application/vnd.github+json\r\n"
            L"X-GitHub-Api-Version: 2022-11-28\r\n";
        sent = WinHttpAddRequestHeaders(request, headers, -1,
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) &&
               WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
               WinHttpReceiveResponse(request, nullptr);
    }

    DWORD status = 0;
    DWORD statusBytes = sizeof(status);
    if (!sent || !WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes,
            WINHTTP_NO_HEADER_INDEX) || status != 200) {
        result.message = status == 404
            ? L"Repository chưa có GitHub Release để cập nhật."
            : L"Không thể kiểm tra cập nhật GitHub lúc này.";
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    std::string response;
    std::array<char, 8192> buffer{};
    while (response.size() < 1024u * 1024u) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(),
                             static_cast<DWORD>(buffer.size()), &read) || read == 0)
            break;
        response.append(buffer.data(), read);
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    const std::string tag = JsonString(response, "tag_name");
    const std::string url = JsonString(response, "html_url");
    if (tag.empty() || url.empty()) {
        result.message = L"GitHub trả về thông tin bản phát hành không hợp lệ.";
        return result;
    }
    result.succeeded = true;
    result.latestVersion = Utf8ToWide(tag);
    result.releaseUrl = Utf8ToWide(url);
    result.newer = ParseVersion(result.latestVersion) >
                   ParseVersion(artthumb::kProductVersion);
    result.message = result.newer ? L"Đã tìm thấy phiên bản mới."
                                  : L"Bạn đang dùng phiên bản mới nhất.";
    return result;
}

DWORD WINAPI UpdateWorker(void* parameter) noexcept {
    HWND window = static_cast<HWND>(parameter);
    std::unique_ptr<UpdateResult> result;
    try {
        result = std::make_unique<UpdateResult>(QueryLatestRelease());
    } catch (...) {
        result = std::make_unique<UpdateResult>();
        result->message = L"Không thể kiểm tra cập nhật GitHub lúc này.";
    }
    if (!PostMessageW(window, kUpdateComplete, 0,
                      reinterpret_cast<LPARAM>(result.get()))) return 0;
    result.release();
    return 0;
}

void StartUpdateCheck() noexcept {
    EnableWindow(g_checkButton, FALSE);
    EnableWindow(g_openButton, FALSE);
    SetWindowTextW(g_status, L"Đang kiểm tra phiên bản mới trên GitHub...");
    HANDLE thread = CreateThread(nullptr, 0, UpdateWorker, g_window, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        EnableWindow(g_checkButton, TRUE);
        EnableWindow(g_openButton, !g_releaseUrl.empty());
        SetWindowTextW(g_status, L"Không thể bắt đầu kiểm tra cập nhật.");
    }
}

void AddControl(const wchar_t* windowClass, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id = 0) {
    HWND control = CreateWindowExW(0, windowClass, text,
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (control) SendMessageW(control, WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font), TRUE);
}

void CreateInterface() {
    g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_headingFont = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    HWND title = CreateWindowExW(0, L"STATIC", L"ArtThumb Update",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 28, 22, 480, 42, g_window,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (title) SendMessageW(title, WM_SETFONT,
        reinterpret_cast<WPARAM>(g_headingFont), TRUE);

    const std::wstring current = std::wstring(L"Phiên bản đang dùng: ") +
                                 artthumb::kProductVersion;
    AddControl(L"STATIC", current.c_str(), SS_LEFT, 32, 82, 480, 26);
    g_status = CreateWindowExW(0, L"STATIC", L"Đang khởi động...",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 32, 124, 480, 48, g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusLabel)),
        GetModuleHandleW(nullptr), nullptr);
    if (g_status) SendMessageW(g_status, WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font), TRUE);

    g_checkButton = CreateWindowExW(0, L"BUTTON", L"Kiểm tra lại",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        32, 194, 150, 40, g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCheckButton)),
        GetModuleHandleW(nullptr), nullptr);
    if (g_checkButton) SendMessageW(g_checkButton, WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font), TRUE);

    g_openButton = CreateWindowExW(0, L"BUTTON", L"Mở trang tải GitHub",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED,
        196, 194, 190, 40, g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenButton)),
        GetModuleHandleW(nullptr), nullptr);
    if (g_openButton) SendMessageW(g_openButton, WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font), TRUE);

    const std::wstring author = std::wstring(L"Tác giả: ") + artthumb::kProductAuthor;
    AddControl(L"STATIC", author.c_str(), SS_LEFT, 32, 250, 480, 26);
    AddControl(L"STATIC", L"Updater chỉ kiểm tra và mở trang phát hành; không tự tải hay cài đặt.",
               SS_LEFT, 32, 278, 500, 34);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g_window = window;
            CreateInterface();
            PostMessageW(window, kCheckUpdates, 0, 0);
            return 0;
        case kCheckUpdates:
            StartUpdateCheck();
            return 0;
        case kUpdateComplete: {
            std::unique_ptr<UpdateResult> result(reinterpret_cast<UpdateResult*>(lParam));
            EnableWindow(g_checkButton, TRUE);
            if (!result) {
                SetWindowTextW(g_status, L"Không thể kiểm tra cập nhật GitHub lúc này.");
                return 0;
            }
            g_releaseUrl = result->releaseUrl;
            EnableWindow(g_openButton, result->succeeded ? TRUE : FALSE);
            std::wstring message = result->message;
            if (result->newer)
                message += L" Phiên bản mới nhất: " + result->latestVersion + L".";
            SetWindowTextW(g_status, message.c_str());
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kCheckButton) {
                StartUpdateCheck();
                return 0;
            }
            if (LOWORD(wParam) == kOpenButton && !g_releaseUrl.empty()) {
                ShellExecuteW(window, L"open", g_releaseUrl.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (g_font) DeleteObject(g_font);
            if (g_headingFont) DeleteObject(g_headingFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) return 1;

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, 560, 350};
    AdjustWindowRectEx(&bounds, style, FALSE, 0);
    HWND window = CreateWindowExW(0, kWindowClass, L"ArtThumb Update",
        style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
        bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
