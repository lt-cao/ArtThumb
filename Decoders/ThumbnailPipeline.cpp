#include "Decoders/ThumbnailPipeline.h"

#include "Decoders/EmbeddedPreview.h"
#include "Decoders/PdfRenderer.h"
#include "Decoders/PsdDecoder.h"
#include "Decoders/StreamReader.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace artthumb {
namespace {

bool ContainsPdfHeader(const std::vector<uint8_t>& prefix) noexcept {
    const size_t limit = std::min<size_t>(prefix.size(), 1024);
    constexpr char marker[] = "%PDF-";
    for (size_t index = 0; index + sizeof(marker) - 1 <= limit; ++index) {
        if (std::memcmp(prefix.data() + index, marker, sizeof(marker) - 1) == 0) return true;
    }
    return false;
}

bool IsInDesign(const std::vector<uint8_t>& prefix) noexcept {
    static constexpr uint8_t magic[] = {
        0x06, 0x06, 0xed, 0xf5, 0xd8, 0x1d, 0x46, 0xe5,
        0xbd, 0x31, 0xef, 0xe7, 0xfe, 0x74, 0xb7, 0x1d
    };
    return prefix.size() >= sizeof(magic) && std::memcmp(prefix.data(), magic, sizeof(magic)) == 0;
}

bool IsPostScript(const std::vector<uint8_t>& prefix) noexcept {
    if (prefix.size() >= 4 && prefix[0] == 0xc5 && prefix[1] == 0xd0 &&
        prefix[2] == 0xd3 && prefix[3] == 0xc6) return true;
    return prefix.size() >= 4 && std::memcmp(prefix.data(), "%!PS", 4) == 0;
}

} // namespace

HRESULT DecodeThumbnail(IStream* stream, UINT edge, HBITMAP* bitmap,
                        ThumbnailKind* kind) noexcept {
    if (!stream || !bitmap || !kind || edge == 0) return E_INVALIDARG;
    *bitmap = nullptr;
    *kind = ThumbnailKind::Unknown;
    try {
        StreamReader reader(stream);
        if (!reader.IsValid() || reader.Size() < 4) return E_FAIL;
        std::vector<uint8_t> prefix;
        HRESULT hr = reader.ReadPrefix(4096, prefix);
        if (FAILED(hr)) return hr;

        if (prefix.size() >= 4 && std::memcmp(prefix.data(), "8BPS", 4) == 0) {
            *kind = ThumbnailKind::Photoshop;
            return DecodePsd(reader, edge, bitmap);
        }
        if (ContainsPdfHeader(prefix)) {
            *kind = ThumbnailKind::Pdf;
            hr = DecodePdfFirstPage(reader, edge, bitmap);
            if (SUCCEEDED(hr)) return hr;
            return DecodeGenericEmbeddedPreview(reader, edge, bitmap);
        }
        if (IsInDesign(prefix)) {
            *kind = ThumbnailKind::InDesign;
            return DecodeInddPreview(reader, edge, bitmap);
        }
        if (IsPostScript(prefix)) {
            *kind = ThumbnailKind::Illustrator;
            hr = DecodeEpsPreview(reader, edge, bitmap);
            if (SUCCEEDED(hr)) return hr;
            return DecodeGenericEmbeddedPreview(reader, edge, bitmap);
        }
        return DecodeGenericEmbeddedPreview(reader, edge, bitmap);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace artthumb
