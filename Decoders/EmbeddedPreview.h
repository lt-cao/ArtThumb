#pragma once

#include "Decoders/StreamReader.h"

#include <windows.h>

namespace artthumb {

HRESULT DecodeInddPreview(const StreamReader& reader, UINT edge, HBITMAP* bitmap) noexcept;
HRESULT DecodeEpsPreview(const StreamReader& reader, UINT edge, HBITMAP* bitmap) noexcept;
HRESULT DecodeGenericEmbeddedPreview(const StreamReader& reader, UINT edge,
                                     HBITMAP* bitmap) noexcept;

} // namespace artthumb
