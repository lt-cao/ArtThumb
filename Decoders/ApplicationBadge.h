#pragma once

#include "Decoders/ThumbnailPipeline.h"

#include <windows.h>
#include <string>

namespace artthumb {

ThumbnailKind ThumbnailKindFromExtension(const std::wstring& extension) noexcept;
HRESULT AddApplicationBadge(HBITMAP bitmap, ThumbnailKind kind,
                            const std::wstring& extension, UINT requestedEdge,
                            int badgePercent) noexcept;

} // namespace artthumb
