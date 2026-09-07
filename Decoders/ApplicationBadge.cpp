#include "Decoders/ApplicationBadge.h"

#include "Settings/UserSettings.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <string>

namespace artthumb {
namespace {

struct BadgeStyle {
    COLORREF background;
    COLORREF foreground;
    const wchar_t* label;
};

const wchar_t* DefaultExtension(ThumbnailKind kind) noexcept {
    switch (kind) {
        case ThumbnailKind::Photoshop: return L".psd";
        case ThumbnailKind::Illustrator: return L".ai";
        case ThumbnailKind::InDesign: return L".indd";
        case ThumbnailKind::Pdf: return L".pdf";
        default: return L"";
    }
}

BadgeStyle StyleFor(ThumbnailKind kind) noexcept {
    switch (kind) {
        case ThumbnailKind::Photoshop: return {RGB(0, 30, 54), RGB(49, 168, 255), L"Ps"};
        case ThumbnailKind::Illustrator: return {RGB(51, 0, 0), RGB(255, 154, 0), L"Ai"};
        case ThumbnailKind::InDesign: return {RGB(73, 0, 36), RGB(255, 51, 153), L"Id"};
        case ThumbnailKind::Pdf: return {RGB(207, 28, 42), RGB(255, 255, 255), L"PDF"};
        default: return {RGB(70, 70, 70), RGB(255, 255, 255), L""};
    }
}

HICON ExtractIconLocation(wchar_t* iconLocation, int iconIndex, int iconSize) noexcept {
    wchar_t expanded[32768]{};
    DWORD expandedLength = ExpandEnvironmentStringsW(iconLocation, expanded, ARRAYSIZE(expanded));
    if (expandedLength == 0 || expandedLength >= ARRAYSIZE(expanded) ||
        _wcsicmp(expanded, L"%1") == 0) return nullptr;

    HICON icon = nullptr;
    const int extractionSize = std::clamp(iconSize, 16, 256);
    if (SUCCEEDED(SHDefExtractIconW(expanded, iconIndex, 0, &icon, nullptr,
                                    MAKELONG(extractionSize, 0))) && icon)
        return icon;
    HICON large = nullptr;
    HICON small = nullptr;
    if (ExtractIconExW(expanded, iconIndex, &large, &small, 1) == 0) return nullptr;
    if (large) {
        if (small) DestroyIcon(small);
        return large;
    }
    return small;
}

HICON ExtractAssociatedIcon(const std::wstring& requestedExtension, int iconSize) noexcept {
    if (requestedExtension.empty()) return nullptr;
    wchar_t iconLocation[32768]{};
    DWORD length = ARRAYSIZE(iconLocation);
    HRESULT hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE,
                                   requestedExtension.c_str(), nullptr,
                                   iconLocation, &length);
    if (SUCCEEDED(hr) && iconLocation[0] != L'\0') {
        if (HICON icon = ExtractIconLocation(iconLocation, 0, iconSize)) return icon;
    }

    return nullptr;
}

void MakeBadgeRegionOpaque(const DIBSECTION& dib, int left, int top,
                           int width, int height) noexcept {
    if (!dib.dsBm.bmBits || dib.dsBm.bmBitsPixel != 32 || dib.dsBm.bmWidthBytes <= 0) return;
    const int bitmapWidth = dib.dsBm.bmWidth;
    const int bitmapHeight = dib.dsBm.bmHeight;
    const bool topDown = dib.dsBmih.biHeight < 0;
    auto* pixels = static_cast<unsigned char*>(dib.dsBm.bmBits);
    for (int y = std::max(0, top); y < std::min(bitmapHeight, top + height); ++y) {
        const int memoryY = topDown ? y : bitmapHeight - 1 - y;
        auto* row = pixels + static_cast<size_t>(memoryY) * dib.dsBm.bmWidthBytes;
        for (int x = std::max(0, left); x < std::min(bitmapWidth, left + width); ++x)
            row[static_cast<size_t>(x) * 4 + 3] = 255;
    }
}

HRESULT DrawFallbackBadge(HDC dc, int left, int top, int size,
                          ThumbnailKind kind) noexcept {
    const BadgeStyle style = StyleFor(kind);
    if (!style.label[0]) return E_FAIL;
    HBRUSH brush = CreateSolidBrush(style.background);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, size / 18), RGB(255, 255, 255));
    if (!brush || !pen) {
        if (brush) DeleteObject(brush);
        if (pen) DeleteObject(pen);
        return E_OUTOFMEMORY;
    }
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    const int radius = std::max(3, size / 4);
    RoundRect(dc, left, top, left + size, top + size, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    const int fontHeight = kind == ThumbnailKind::Pdf ? -(size * 31 / 100) : -(size * 45 / 100);
    HFONT font = CreateFontW(fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!font) return HRESULT_FROM_WIN32(GetLastError());
    HGDIOBJ oldFont = SelectObject(dc, font);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = SetTextColor(dc, style.foreground);
    RECT bounds{left, top, left + size, top + size};
    DrawTextW(dc, style.label, -1, &bounds,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    return S_OK;
}

} // namespace

ThumbnailKind ThumbnailKindFromExtension(const std::wstring& extension) noexcept {
    if (extension == L".psd" || extension == L".psb") return ThumbnailKind::Photoshop;
    if (extension == L".ai" || extension == L".eps") return ThumbnailKind::Illustrator;
    if (extension == L".indd") return ThumbnailKind::InDesign;
    if (extension == L".pdf") return ThumbnailKind::Pdf;
    return ThumbnailKind::Unknown;
}

HRESULT AddApplicationBadge(HBITMAP bitmap, ThumbnailKind kind,
                            const std::wstring& extension, UINT requestedEdge,
                            int badgePercent) noexcept {
    if (!bitmap || kind == ThumbnailKind::Unknown || requestedEdge == 0) return E_INVALIDARG;
    DIBSECTION dib{};
    if (GetObjectW(bitmap, sizeof(dib), &dib) != sizeof(dib) ||
        dib.dsBm.bmWidth <= 0 || dib.dsBm.bmHeight <= 0) return E_FAIL;
    const int shortest = std::min(dib.dsBm.bmWidth, dib.dsBm.bmHeight);
    const int longest = std::max(dib.dsBm.bmWidth, dib.dsBm.bmHeight);
    const int requested = static_cast<int>(std::min<UINT>(requestedEdge, 4096));
    const int basis = std::min(longest, requested);
    badgePercent = std::clamp(badgePercent, kMinimumBadgePercent, kMaximumBadgePercent);
    const int margin = std::clamp(basis * 2 / 100, 1, 32);
    const int available = shortest - margin * 2;
    if (available < 8) return E_FAIL;
    const int badgeSize = std::min(available, std::max(12, basis * badgePercent / 100));
    const int left = dib.dsBm.bmWidth - badgeSize - margin;
    const int top = dib.dsBm.bmHeight - badgeSize - margin;

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return HRESULT_FROM_WIN32(GetLastError());
    HGDIOBJ previous = SelectObject(dc, bitmap);
    if (!previous || previous == HGDI_ERROR) {
        DeleteDC(dc);
        return E_FAIL;
    }

    const std::wstring association = extension.empty() ? DefaultExtension(kind) : extension;
    HICON icon = ExtractAssociatedIcon(association, badgeSize);
    HRESULT hr = S_OK;
    if (icon) {
        if (!DrawIconEx(dc, left, top, icon, badgeSize, badgeSize, 0, nullptr, DI_NORMAL))
            hr = HRESULT_FROM_WIN32(GetLastError());
        DestroyIcon(icon);
    } else {
        hr = DrawFallbackBadge(dc, left, top, badgeSize, kind);
    }
    SelectObject(dc, previous);
    DeleteDC(dc);
    if (SUCCEEDED(hr)) MakeBadgeRegionOpaque(dib, left, top, badgeSize, badgeSize);
    return hr;
}

} // namespace artthumb
