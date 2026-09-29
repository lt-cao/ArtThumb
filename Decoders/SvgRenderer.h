#pragma once

#include "Decoders/StreamReader.h"

#include <windows.h>

namespace artthumb {

HRESULT DecodeSvg(const StreamReader& reader, UINT edge, HBITMAP* bitmap) noexcept;

} // namespace artthumb
