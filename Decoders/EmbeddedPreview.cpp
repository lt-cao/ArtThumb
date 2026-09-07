#include "Decoders/EmbeddedPreview.h"

#include "Decoders/WicImage.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace artthumb {
namespace {

constexpr size_t kSearchWindow = 16u * 1024u * 1024u;
constexpr size_t kMaximumEmbeddedImage = 64u * 1024u * 1024u;

bool FindJpeg(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& jpeg) {
    jpeg.clear();
    if (bytes.size() < 4) return false;
    size_t start = std::string_view::npos;
    for (size_t index = 0; index + 1 < bytes.size(); ++index) {
        if (start == std::string_view::npos && index + 2 < bytes.size() &&
            bytes[index] == 0xff && bytes[index + 1] == 0xd8 && bytes[index + 2] == 0xff) {
            start = index;
            index += 2;
            continue;
        }
        if (start != std::string_view::npos && bytes[index] == 0xff && bytes[index + 1] == 0xd9) {
            const size_t length = index + 2 - start;
            if (length >= 128 && length <= kMaximumEmbeddedImage) {
                jpeg.assign(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                            bytes.begin() + static_cast<std::ptrdiff_t>(index + 2));
                return true;
            }
            start = std::string_view::npos;
        } else if (start != std::string_view::npos && index - start > kMaximumEmbeddedImage) {
            start = std::string_view::npos;
        }
    }
    return false;
}

int Base64Value(char value) noexcept {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

bool DecodeBase64(std::string_view text, std::vector<uint8_t>& result) {
    result.clear();
    if (text.size() > kMaximumEmbeddedImage * 2) return false;
    uint32_t accumulator = 0;
    unsigned bits = 0;
    for (size_t index = 0; index < text.size(); ++index) {
        const char value = text[index];
        if (value == '&' && index + 5 <= text.size()) {
            const std::string_view entity = text.substr(index, 5);
            if (entity == "&#xA;" || entity == "&#xa;" ||
                entity == "&#xD;" || entity == "&#xd;" ||
                entity == "&#10;" || entity == "&#13;") {
                index += 4;
                continue;
            }
        }
        if (value == '=') break;
        const int digit = Base64Value(value);
        if (digit < 0) {
            if (std::isspace(static_cast<unsigned char>(value))) continue;
            return false;
        }
        accumulator = (accumulator << 6) | static_cast<uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xff));
            if (result.size() > kMaximumEmbeddedImage) return false;
        }
    }
    return result.size() >= 4;
}

bool FindXmpImage(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& encoded) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    constexpr std::string_view open = "<xmpGImg:image>";
    constexpr std::string_view close = "</xmpGImg:image>";
    size_t cursor = 0;
    while ((cursor = text.find(open, cursor)) != std::string_view::npos) {
        const size_t dataStart = cursor + open.size();
        const size_t dataEnd = text.find(close, dataStart);
        if (dataEnd == std::string_view::npos) return false;
        if (DecodeBase64(text.substr(dataStart, dataEnd - dataStart), encoded) &&
            encoded.size() > 3 && encoded[0] == 0xff && encoded[1] == 0xd8) return true;
        cursor = dataEnd + close.size();
    }
    return false;
}

int HexValue(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

HRESULT DecodeEpsi(const std::vector<uint8_t>& bytes, UINT edge, HBITMAP* bitmap) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const size_t marker = text.find("%%BeginPreview:");
    if (marker == std::string_view::npos) return E_FAIL;
    const size_t lineEnd = text.find_first_of("\r\n", marker);
    if (lineEnd == std::string_view::npos) return E_FAIL;

    unsigned width = 0, height = 0, depth = 0, lines = 0;
    std::string header(text.substr(marker, lineEnd - marker));
    if (std::sscanf(header.c_str(), "%%%%BeginPreview: %u %u %u %u",
                    &width, &height, &depth, &lines) != 4) return E_FAIL;
    if (width == 0 || height == 0 || width > 100000 || height > 100000 ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8)) return E_FAIL;
    const size_t rowBytes = (static_cast<size_t>(width) * depth + 7) / 8;
    if (rowBytes > SIZE_MAX / height || rowBytes * height > kMaximumEmbeddedImage) return E_FAIL;

    std::vector<uint8_t> packed;
    packed.reserve(rowBytes * height);
    size_t cursor = lineEnd;
    while (cursor < text.size() && packed.size() < rowBytes * height) {
        const size_t next = text.find_first_of("\r\n", cursor + 1);
        const size_t end = next == std::string_view::npos ? text.size() : next;
        std::string_view line = text.substr(cursor, end - cursor);
        while (!line.empty() && (line.front() == '\r' || line.front() == '\n')) line.remove_prefix(1);
        if (line.rfind("%%EndPreview", 0) == 0) break;
        if (!line.empty() && line.front() == '%') {
            line.remove_prefix(1);
            int high = -1;
            for (char value : line) {
                const int digit = HexValue(value);
                if (digit < 0) continue;
                if (high < 0) high = digit;
                else {
                    packed.push_back(static_cast<uint8_t>((high << 4) | digit));
                    high = -1;
                    if (packed.size() == rowBytes * height) break;
                }
            }
        }
        if (next == std::string_view::npos) break;
        cursor = next;
    }
    if (packed.size() < rowBytes * height) return E_FAIL;

    const double scale = std::min(1.0, static_cast<double>(edge) / std::max(width, height));
    const UINT outWidth = std::max<UINT>(1, static_cast<UINT>(std::lround(width * scale)));
    const UINT outHeight = std::max<UINT>(1, static_cast<UINT>(std::lround(height * scale)));
    std::vector<uint8_t> bgra(static_cast<size_t>(outWidth) * outHeight * 4);
    const unsigned maximum = (1u << depth) - 1u;
    for (UINT y = 0; y < outHeight; ++y) {
        const unsigned sourceY = std::min<unsigned>(height - 1,
            static_cast<unsigned>((static_cast<uint64_t>(y) * height) / outHeight));
        for (UINT x = 0; x < outWidth; ++x) {
            const unsigned sourceX = std::min<unsigned>(width - 1,
                static_cast<unsigned>((static_cast<uint64_t>(x) * width) / outWidth));
            const size_t bit = static_cast<size_t>(sourceX) * depth;
            const uint8_t packedByte = packed[static_cast<size_t>(sourceY) * rowBytes + bit / 8];
            const unsigned shift = 8 - depth - static_cast<unsigned>(bit % 8);
            const unsigned sample = (packedByte >> shift) & maximum;
            const uint8_t gray = static_cast<uint8_t>(255u - (sample * 255u / maximum));
            const size_t output = (static_cast<size_t>(y) * outWidth + x) * 4;
            bgra[output] = gray;
            bgra[output + 1] = gray;
            bgra[output + 2] = gray;
            bgra[output + 3] = 255;
        }
    }
    return PixelsToBitmap(bgra.data(), outWidth, outHeight, bitmap);
}

} // namespace

HRESULT DecodeInddPreview(const StreamReader& reader, UINT edge, HBITMAP* bitmap) noexcept {
    try {
        std::vector<uint8_t> bytes;
        uint64_t base = 0;
        HRESULT hr = reader.ReadTail(kSearchWindow, bytes, base);
        if (FAILED(hr)) return hr;
        std::vector<uint8_t> encoded;
        // INDD can contain many linked JPEG assets. Only its XMP thumbnail is a
        // reliable document preview; choosing an arbitrary JPEG shows wrong art.
        if (FindXmpImage(bytes, encoded))
            return DecodeEncodedImage(encoded.data(), encoded.size(), edge, bitmap);

        if (reader.Size() > bytes.size()) {
            hr = reader.ReadPrefix(kSearchWindow, bytes);
            if (FAILED(hr)) return hr;
            if (FindXmpImage(bytes, encoded))
                return DecodeEncodedImage(encoded.data(), encoded.size(), edge, bitmap);
        }
        return E_FAIL;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT DecodeEpsPreview(const StreamReader& reader, UINT edge, HBITMAP* bitmap) noexcept {
    try {
        uint8_t header[30]{};
        if (reader.Size() >= sizeof(header) && SUCCEEDED(reader.ReadAt(0, header, sizeof(header))) &&
            header[0] == 0xc5 && header[1] == 0xd0 && header[2] == 0xd3 && header[3] == 0xc6) {
            const uint32_t tiffOffset = ReadLe32(header + 20);
            const uint32_t tiffLength = ReadLe32(header + 24);
            if (tiffLength > 0 && tiffLength <= kMaximumEmbeddedImage &&
                tiffOffset <= reader.Size() && tiffLength <= reader.Size() - tiffOffset) {
                std::vector<uint8_t> tiff;
                HRESULT hr = reader.ReadVector(tiffOffset, tiffLength, tiff);
                if (SUCCEEDED(hr)) {
                    hr = DecodeEncodedImage(tiff.data(), tiff.size(), edge, bitmap);
                    if (SUCCEEDED(hr)) return hr;
                }
            }
        }

        std::vector<uint8_t> bytes;
        HRESULT hr = reader.ReadPrefix(kSearchWindow, bytes);
        if (FAILED(hr)) return hr;
        hr = DecodeEpsi(bytes, edge, bitmap);
        if (SUCCEEDED(hr)) return hr;
        std::vector<uint8_t> encoded;
        if (FindJpeg(bytes, encoded)) return DecodeEncodedImage(encoded.data(), encoded.size(), edge, bitmap);
        return E_FAIL;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT DecodeGenericEmbeddedPreview(const StreamReader& reader, UINT edge,
                                     HBITMAP* bitmap) noexcept {
    try {
        std::vector<uint8_t> bytes;
        std::vector<uint8_t> encoded;
        HRESULT hr = reader.ReadPrefix(kSearchWindow, bytes);
        if (FAILED(hr)) return hr;
        if (FindXmpImage(bytes, encoded) || FindJpeg(bytes, encoded))
            return DecodeEncodedImage(encoded.data(), encoded.size(), edge, bitmap);
        uint64_t base = 0;
        hr = reader.ReadTail(kSearchWindow, bytes, base);
        if (FAILED(hr)) return hr;
        if (FindXmpImage(bytes, encoded) || FindJpeg(bytes, encoded))
            return DecodeEncodedImage(encoded.data(), encoded.size(), edge, bitmap);
        return E_FAIL;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace artthumb
