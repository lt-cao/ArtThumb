#pragma once

#include <windows.h>

namespace artthumb {

extern HMODULE g_module;
extern volatile long g_objectCount;
extern volatile long g_lockCount;

// {A0654CAE-E1AC-4A73-96F8-CB5453EE3264}
inline constexpr CLSID kThumbnailProviderClsid = {
    0xa0654cae, 0xe1ac, 0x4a73, {0x96, 0xf8, 0xcb, 0x54, 0x53, 0xee, 0x32, 0x64}
};

inline constexpr wchar_t kThumbnailProviderClsidText[] =
    L"{A0654CAE-E1AC-4A73-96F8-CB5453EE3264}";
inline constexpr wchar_t kThumbnailHandlerIidText[] =
    L"{E357FCCD-A995-4576-B01F-234630154E96}";
inline constexpr wchar_t kProductName[] = L"ArtThumb";
inline constexpr wchar_t kProductVersion[] = L"1.2.2";
inline constexpr wchar_t kProductAuthor[] = L"Cao Le";
inline constexpr wchar_t kGitHubRepositoryUrl[] = L"https://github.com/lt-cao/ArtThumb";
inline constexpr wchar_t kGitHubLatestReleaseApiPath[] =
    L"/repos/lt-cao/ArtThumb/releases/latest";

void ObjectCreated() noexcept;
void ObjectDestroyed() noexcept;

} // namespace artthumb
