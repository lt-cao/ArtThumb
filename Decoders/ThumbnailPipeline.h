#pragma once

#include <windows.h>
#include <objidl.h>

namespace artthumb {

enum class ThumbnailKind {
    Unknown,
    Photoshop,
    Illustrator,
    InDesign,
    Pdf
};

HRESULT DecodeThumbnail(IStream* stream, UINT edge, HBITMAP* bitmap,
                        ThumbnailKind* kind) noexcept;

} // namespace artthumb
