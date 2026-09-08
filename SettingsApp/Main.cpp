#include "Decoders/ThumbnailEffects.h"
#include "Provider/Module.h"
#include "Settings/UserSettings.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"ArtThumb.Settings.Window";
constexpr UINT kUpdateComplete = WM_APP + 1;

enum ControlId {
    kFormat = 1001,
    kSharpnessSlider,
    kSharpnessValue,
    kSave,
    kDefaults,
    kCheckUpdate,
    kOpenGitHub,
    kStatus,
};

struct FormatChoice {
    const wchar_t* label;
    const wchar_t* extension;
};

constexpr FormatChoice kFormats[] = {
    {L"Photoshop (.psd)", L".psd"},
    {L"Photoshop Large (.psb)", L".psb"},
    {L"Illustrator (.ai)", L".ai"},
    {L"Illustrator EPS (.eps)", L".eps"},
    {L"InDesign (.indd)", L".indd"},
    {L"PDF (.pdf)", L".pdf"},
};

HWND g_window = nullptr;
HWND g_format = nullptr;
HWND g_previewFilename = nullptr;
HWND g_sharpnessSlider = nullptr;
HWND g_sharpnessValue = nullptr;
HWND g_updateButton = nullptr;
HWND g_status = nullptr;
HFONT g_font = nullptr;
HFONT g_headingFont = nullptr;
HBRUSH g_backgroundBrush = nullptr;
HBITMAP g_preview = nullptr;
UINT g_dpi = 96;
std::wstring g_latestReleaseUrl;

int Scale(int value) noexcept {
    return MulDiv(value, static_cast<int>(g_dpi), 96);
}

RECT PreviewBounds() noexcept {
    return RECT{Scale(58), Scale(150), Scale(418), Scale(370)};
}

void SetControlFont(HWND control, HFONT font = nullptr) noexcept {
    SendMessageW(control, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font ? font : g_font), TRUE);
}

HWND AddControl(const wchar_t* windowClass, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id = 0,
                DWORD extendedStyle = 0) {
    HWND control = CreateWindowExW(extendedStyle, windowClass, text,
        WS_CHILD | WS_VISIBLE | style, Scale(x), Scale(y), Scale(width), Scale(height),
        g_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (control) SetControlFont(control);
    return control;
}

std::wstring PercentText(int value) {
    return std::to_wstring(value) + L"%";
}

int SliderValue(HWND slider) noexcept {
    return static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
}

void SetStatus(const wchar_t* text) noexcept {
    SetWindowTextW(g_status, text);
}

HBITMAP CreateSampleBitmap(int width, int height) noexcept {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* raw = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
    if (!bitmap || !raw) return nullptr;

    auto* pixels = static_cast<unsigned char*>(raw);
    const double sunX = width * 0.68;
    const double sunY = height * 0.34;
    const double sunRadius = height * 0.16;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double fx = static_cast<double>(x) / std::max(1, width - 1);
            const double fy = static_cast<double>(y) / std::max(1, height - 1);
            int red = static_cast<int>(35 + 95 * (1.0 - fy) + 30 * fx);
            int green = static_cast<int>(70 + 95 * (1.0 - fy));
            int blue = static_cast<int>(120 + 95 * (1.0 - fy));

            const double dx = x - sunX;
            const double dy = y - sunY;
            if (dx * dx + dy * dy < sunRadius * sunRadius) {
                red = 252;
                green = 185 + static_cast<int>(35 * (1.0 - fy));
                blue = 54;
            }

            const int ridge = static_cast<int>(height * 0.58 +
                std::sin(fx * 13.0) * height * 0.07 +
                std::sin(fx * 31.0) * height * 0.025);
            if (y > ridge) {
                const int band = (y - ridge) / std::max(2, height / 35);
                const std::array<std::array<int, 3>, 5> palette{{
                    {{28, 83, 108}}, {{225, 83, 47}}, {{245, 153, 38}},
                    {{27, 123, 145}}, {{79, 54, 111}}
                }};
                const auto& color = palette[static_cast<size_t>(band) % palette.size()];
                red = color[0]; green = color[1]; blue = color[2];
            }

            if ((x + y * 2) % 47 == 0 || (x * 3 - y + width * 4) % 71 == 0) {
                red = std::min(255, red + 38);
                green = std::min(255, green + 38);
                blue = std::min(255, blue + 38);
            }
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            pixels[offset] = static_cast<unsigned char>(blue);
            pixels[offset + 1] = static_cast<unsigned char>(green);
            pixels[offset + 2] = static_cast<unsigned char>(red);
            pixels[offset + 3] = 255;
        }
    }
    return bitmap;
}

void RebuildPreview() noexcept {
    if (g_preview) {
        DeleteObject(g_preview);
        g_preview = nullptr;
    }
    g_preview = CreateSampleBitmap(360, 220);
    if (!g_preview) return;
    const int sharpness = SliderValue(g_sharpnessSlider);
    artthumb::ApplyThumbnailSharpness(g_preview, sharpness);
    SetWindowTextW(g_sharpnessValue, PercentText(sharpness).c_str());
    RECT bounds = PreviewBounds();
    InvalidateRect(g_window, &bounds, FALSE);
}

void PaintPreview(HDC destination) noexcept {
    const RECT bounds = PreviewBounds();
    HBRUSH card = CreateSolidBrush(RGB(37, 39, 43));
    FillRect(destination, &bounds, card);
    DeleteObject(card);
    if (!g_preview) return;

    HDC source = CreateCompatibleDC(destination);
    HGDIOBJ previous = SelectObject(source, g_preview);
    BITMAP bitmap{};
    GetObjectW(g_preview, sizeof(bitmap), &bitmap);
    SetStretchBltMode(destination, HALFTONE);
    StretchBlt(destination, bounds.left, bounds.top,
               bounds.right - bounds.left, bounds.bottom - bounds.top,
               source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
    SelectObject(source, previous);
    DeleteDC(source);

    HBRUSH border = CreateSolidBrush(RGB(82, 86, 94));
    FrameRect(destination, &bounds, border);
    DeleteObject(border);
}

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
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read) ||
            read == 0) break;
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
    EnableWindow(g_updateButton, FALSE);
    SetStatus(L"Đang kiểm tra GitHub...");
    HANDLE thread = CreateThread(nullptr, 0, UpdateWorker, g_window, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        EnableWindow(g_updateButton, TRUE);
        SetStatus(L"Không thể bắt đầu kiểm tra cập nhật.");
    }
}

void OpenUrl(const wchar_t* url) noexcept {
    ShellExecuteW(g_window, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void SaveSettings() noexcept {
    artthumb::UserSettings settings;
    settings.sharpness = SliderValue(g_sharpnessSlider);
    if (!artthumb::SaveUserSettings(settings)) {
        SetStatus(L"Không thể lưu cài đặt.");
        return;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    SetStatus(L"Đã lưu. Thumbnail mới trong Explorer sẽ dùng cài đặt này.");
}

void ResetControls() noexcept {
    SendMessageW(g_sharpnessSlider, TBM_SETPOS, TRUE, artthumb::kDefaultSharpness);
    RebuildPreview();
    SetStatus(L"Đã đưa phần xem trước về mặc định; bấm Lưu để áp dụng.");
}

void CreateInterface() {
    g_font = CreateFontW(Scale(-15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_headingFont = CreateFontW(Scale(-26), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    HWND title = AddControl(L"STATIC", L"ArtThumb Settings", SS_LEFT,
                            28, 18, 370, 38);
    SetControlFont(title, g_headingFont);
    const std::wstring version = std::wstring(L"Phiên bản ") + artthumb::kProductVersion;
    AddControl(L"STATIC", version.c_str(), SS_RIGHT,
               728, 27, 220, 24);

    AddControl(L"BUTTON", L"Xem trước trong File Explorer", BS_GROUPBOX,
               24, 72, 430, 340);
    AddControl(L"STATIC", L"Định dạng", SS_LEFT, 48, 105, 90, 24);
    g_format = AddControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                          140, 100, 270, 220, kFormat);
    for (const auto& format : kFormats)
        SendMessageW(g_format, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(format.label));
    SendMessageW(g_format, CB_SETCURSEL, 0, 0);
    g_previewFilename = AddControl(L"STATIC", L"sample.psd", SS_CENTER,
                                   58, 376, 360, 24);

    AddControl(L"BUTTON", L"Tùy chỉnh thumbnail", BS_GROUPBOX,
               472, 72, 480, 340);
    AddControl(L"STATIC", L"Độ sắc nét của nội dung thumbnail", SS_LEFT,
               500, 112, 320, 24);
    g_sharpnessValue = AddControl(L"STATIC", L"0%", SS_RIGHT,
                                  852, 112, 62, 24, kSharpnessValue);
    g_sharpnessSlider = AddControl(TRACKBAR_CLASSW, L"",
        TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, 500, 142, 414, 46, kSharpnessSlider);
    SendMessageW(g_sharpnessSlider, TBM_SETRANGE, TRUE,
        MAKELPARAM(artthumb::kMinimumSharpness, artthumb::kMaximumSharpness));
    SendMessageW(g_sharpnessSlider, TBM_SETTICFREQ, 10, 0);
    AddControl(L"STATIC",
        L"0% giữ nguyên ảnh gốc. Mức cao làm rõ cạnh và chi tiết nhỏ; không tăng độ phân giải nguồn.",
        SS_LEFT, 500, 194, 414, 58);
    AddControl(L"STATIC",
        L"Thumbnail trong Explorer chỉ có nội dung tài liệu, không chèn logo Ps, Ai, Id hoặc PDF.",
        SS_LEFT, 500, 274, 414, 48);
    AddControl(L"STATIC",
        L"Thay đổi độ sắc nét hiển thị ngay ở khung xem trước bên trái.",
        SS_LEFT, 500, 350, 414, 30);

    AddControl(L"BUTTON", L"Định dạng được hỗ trợ", BS_GROUPBOX,
               24, 428, 560, 166);
    AddControl(L"STATIC",
        L"PSD / PSB   Photoshop — preview hoặc composite phẳng\r\n"
        L"AI           Illustrator — PDF tương thích hoặc preview nhúng\r\n"
        L"EPS          TIFF, EPSI hoặc JPEG preview\r\n"
        L"INDD         Preview trang được InDesign lưu trong tài liệu\r\n"
        L"PDF          Trang đầu tiên",
        SS_LEFT, 50, 462, 500, 112);

    AddControl(L"BUTTON", L"Thông tin & cập nhật", BS_GROUPBOX,
               602, 428, 350, 166);
    const std::wstring author = std::wstring(L"Tác giả: ") + artthumb::kProductAuthor;
    AddControl(L"STATIC", author.c_str(), SS_LEFT, 628, 462, 290, 24);
    g_updateButton = AddControl(L"BUTTON", L"Kiểm tra cập nhật",
        BS_PUSHBUTTON | WS_TABSTOP, 628, 496, 142, 34, kCheckUpdate);
    AddControl(L"BUTTON", L"Mở GitHub", BS_PUSHBUTTON | WS_TABSTOP,
               782, 496, 142, 34, kOpenGitHub);
    AddControl(L"STATIC", L"Chỉ kết nối GitHub khi bạn bấm kiểm tra.",
               SS_LEFT, 628, 546, 296, 30);

    g_status = AddControl(L"STATIC", L"", SS_LEFT,
                          28, 619, 570, 30, kStatus);
    AddControl(L"BUTTON", L"Khôi phục mặc định", BS_PUSHBUTTON | WS_TABSTOP,
               616, 612, 158, 38, kDefaults);
    AddControl(L"BUTTON", L"Lưu cài đặt", BS_DEFPUSHBUTTON | WS_TABSTOP,
               790, 612, 158, 38, kSave);

    const artthumb::UserSettings settings = artthumb::LoadUserSettings();
    SendMessageW(g_sharpnessSlider, TBM_SETPOS, TRUE, settings.sharpness);
    RebuildPreview();
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g_window = window;
            g_dpi = GetDpiForWindow(window);
            g_backgroundBrush = CreateSolidBrush(RGB(246, 247, 249));
            CreateInterface();
            return 0;
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == g_sharpnessSlider) {
                RebuildPreview();
                SetStatus(L"");
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kFormat && HIWORD(wParam) == CBN_SELCHANGE) {
                const int index = static_cast<int>(SendMessageW(g_format, CB_GETCURSEL, 0, 0));
                if (index >= 0 && index < static_cast<int>(std::size(kFormats))) {
                    std::wstring name = L"sample";
                    name += kFormats[index].extension;
                    SetWindowTextW(g_previewFilename, name.c_str());
                }
                RebuildPreview();
                return 0;
            }
            if (LOWORD(wParam) == kSave) { SaveSettings(); return 0; }
            if (LOWORD(wParam) == kDefaults) { ResetControls(); return 0; }
            if (LOWORD(wParam) == kCheckUpdate) { StartUpdateCheck(); return 0; }
            if (LOWORD(wParam) == kOpenGitHub) {
                OpenUrl(artthumb::kGitHubRepositoryUrl);
                return 0;
            }
            break;
        case kUpdateComplete: {
            std::unique_ptr<UpdateResult> result(reinterpret_cast<UpdateResult*>(lParam));
            EnableWindow(g_updateButton, TRUE);
            if (!result) {
                SetStatus(L"Không thể kiểm tra cập nhật GitHub lúc này.");
                return 0;
            }
            SetStatus(result->message.c_str());
            if (result->succeeded) g_latestReleaseUrl = result->releaseUrl;
            if (result->newer) {
                const std::wstring prompt = L"Có ArtThumb " + result->latestVersion +
                    L" trên GitHub. Mở trang tải bản mới?";
                if (MessageBoxW(window, prompt.c_str(), L"ArtThumb Update",
                                MB_YESNO | MB_ICONINFORMATION) == IDYES)
                    OpenUrl(result->releaseUrl.c_str());
            }
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT client{};
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, g_backgroundBrush);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(g_backgroundBrush);
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            PaintPreview(dc);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            if (g_preview) DeleteObject(g_preview);
            if (g_font) DeleteObject(g_font);
            if (g_headingFont) DeleteObject(g_headingFont);
            if (g_backgroundBrush) DeleteObject(g_backgroundBrush);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) return 1;

    const UINT dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(976, static_cast<int>(dpi), 96),
                MulDiv(686, static_cast<int>(dpi), 96)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, dpi);
    HWND window = CreateWindowExW(0, kWindowClass, L"ArtThumb Settings",
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
