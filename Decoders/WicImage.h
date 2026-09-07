#pragma once

#include <windows.h>
#include <objidl.h>
#include <cstdint>
#include <vector>

namespace artthumb {

HRESULT DecodeEncodedImage(const uint8_t* bytes, size_t length, UINT edge,
                           HBITMAP* bitmap) noexcept;
HRESULT DecodeEncodedStream(IStream* stream, UINT edge, HBITMAP* bitmap) noexcept;
HRESULT PixelsToBitmap(const uint8_t* bgra, UINT width, UINT height,
                       HBITMAP* bitmap) noexcept;

} // namespace artthumb
