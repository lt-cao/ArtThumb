#include "Decoders/ThumbnailPipeline.h"

#include "Decoders/EmbeddedPreview.h"
#include "Decoders/PdfRenderer.h"
#include "Decoders/PsdDecoder.h"
#include "Decoders/StreamReader.h"
#include "Decoders/SvgRenderer.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace artthumb {
namespace {

constexpr size_t kIllustratorWarningSearchWindow = 16u * 1024u * 1024u;

bool ContainsText(const std::vector<uint8_t>& bytes, std::string_view needle) noexcept {
    if (needle.empty() || bytes.size() < needle.size()) return false;
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return text.find(needle) != std::string_view::npos;
}

bool ContainsPdfHeader(const std::vector<uint8_t>& prefix) noexcept {
    const size_t limit = std::min<size_t>(prefix.size(), 1024);
    constexpr char marker[] = "%PDF-";
    for (size_t index = 0; index + sizeof(marker) - 1 <= limit; ++index) {
        if (std::memcmp(prefix.data() + index, marker, sizeof(marker) - 1) == 0) return true;
    }
    return false;
}

bool IsIllustratorPdf(const std::vector<uint8_t>& prefix) noexcept {
    return ContainsText(prefix, "http://ns.adobe.com/illustrator/");
}

bool HasIllustratorNoPdfContentWarning(const StreamReader& reader) {
    // Illustrator still wraps non-PDF-compatible AI files in a PDF container,
    // but its PDF page is only a compatibility warning. The actual artwork is
    // represented by the JPEG thumbnail in the XMP metadata.
    constexpr std::string_view warning = "without PDF C";
    std::vector<uint8_t> bytes;
    HRESULT hr = reader.ReadPrefix(kIllustratorWarningSearchWindow, bytes);
    if (SUCCEEDED(hr) && ContainsText(bytes, warning)) return true;
    if (reader.Size() <= kIllustratorWarningSearchWindow) return false;

    uint64_t baseOffset = 0;
    hr = reader.ReadTail(kIllustratorWarningSearchWindow, bytes, baseOffset);
    return SUCCEEDED(hr) && ContainsText(bytes, warning);
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

size_t SkipXmlDeclaration(const std::string& xml, size_t start) noexcept {
    char quote = 0;
    unsigned int subsetDepth = 0;
    for (size_t index = start; index < xml.size(); ++index) {
        const char value = xml[index];
        if (quote != 0) {
            if (value == quote) quote = 0;
            continue;
        }
        if (value == '\'' || value == '"') quote = value;
        else if (value == '[') ++subsetDepth;
        else if (value == ']' && subsetDepth != 0) --subsetDepth;
        else if (value == '>' && subsetDepth == 0) return index + 1;
    }
    return std::string::npos;
}

bool LooksLikeSvg(const std::vector<uint8_t>& prefix) {
    size_t offset = 0;
    bool littleEndianUtf16 = false;
    bool bigEndianUtf16 = false;
    if (prefix.size() >= 3 && prefix[0] == 0xef && prefix[1] == 0xbb && prefix[2] == 0xbf)
        offset = 3;
    else if (prefix.size() >= 2 && prefix[0] == 0xff && prefix[1] == 0xfe) {
        offset = 2;
        littleEndianUtf16 = true;
    } else if (prefix.size() >= 2 && prefix[0] == 0xfe && prefix[1] == 0xff) {
        offset = 2;
        bigEndianUtf16 = true;
    } else if (prefix.size() >= 2 && prefix[0] == '<' && prefix[1] == 0) {
        littleEndianUtf16 = true;
    } else if (prefix.size() >= 2 && prefix[0] == 0 && prefix[1] == '<') {
        bigEndianUtf16 = true;
    }

    std::string xml;
    xml.reserve(prefix.size());
    if (littleEndianUtf16 || bigEndianUtf16) {
        for (size_t index = offset; index + 1 < prefix.size(); index += 2) {
            const uint16_t value = littleEndianUtf16
                ? static_cast<uint16_t>(prefix[index] | (prefix[index + 1] << 8))
                : static_cast<uint16_t>((prefix[index] << 8) | prefix[index + 1]);
            xml.push_back(value <= 0x7f ? static_cast<char>(value) : ' ');
        }
    } else {
        for (size_t index = offset; index < prefix.size(); ++index)
            xml.push_back(prefix[index] <= 0x7f ? static_cast<char>(prefix[index]) : ' ');
    }

    size_t position = 0;
    for (;;) {
        position = xml.find_first_not_of(" \t\r\n", position);
        if (position == std::string::npos) return false;
        if (xml.compare(position, 2, "<?") == 0) {
            position = xml.find("?>", position + 2);
            if (position == std::string::npos) return false;
            position += 2;
        } else if (xml.compare(position, 4, "<!--") == 0) {
            position = xml.find("-->", position + 4);
            if (position == std::string::npos) return false;
            position += 3;
        } else if (xml.compare(position, 2, "<!") == 0) {
            position = SkipXmlDeclaration(xml, position + 2);
            if (position == std::string::npos) return false;
        } else {
            if (xml[position] != '<' || position + 1 >= xml.size() || xml[position + 1] == '/')
                return false;
            size_t nameEnd = position + 1;
            while (nameEnd < xml.size() && xml[nameEnd] != '>' && xml[nameEnd] != '/' &&
                   xml[nameEnd] != ' ' && xml[nameEnd] != '\t' && xml[nameEnd] != '\r' &&
                   xml[nameEnd] != '\n') ++nameEnd;
            const std::string elementName = xml.substr(position + 1, nameEnd - position - 1);
            const size_t colon = elementName.find_last_of(':');
            return elementName.substr(colon == std::string::npos ? 0 : colon + 1) == "svg";
        }
    }
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
            const bool illustratorPdf = IsIllustratorPdf(prefix);
            *kind = illustratorPdf ? ThumbnailKind::Illustrator : ThumbnailKind::Pdf;
            if (illustratorPdf && HasIllustratorNoPdfContentWarning(reader)) {
                hr = DecodeGenericEmbeddedPreview(reader, edge, bitmap);
                if (SUCCEEDED(hr)) return hr;
            }
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
        if (LooksLikeSvg(prefix)) {
            *kind = ThumbnailKind::Svg;
            return DecodeSvg(reader, edge, bitmap);
        }
        return DecodeGenericEmbeddedPreview(reader, edge, bitmap);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace artthumb
