#include "Decoders/PsdDecoder.h"

#include "Decoders/WicImage.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace artthumb {
namespace {

struct PsdInfo {
    uint16_t version = 0;
    uint16_t channels = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t depth = 0;
    uint16_t colorMode = 0;
    uint64_t resourcesStart = 0;
    uint64_t resourcesEnd = 0;
    uint64_t compositeStart = 0;
};

bool AddWithin(uint64_t base, uint64_t amount, uint64_t limit, uint64_t& result) noexcept {
    if (base > limit || amount > limit - base) return false;
    result = base + amount;
    return true;
}

HRESULT ReadInfo(const StreamReader& reader, PsdInfo& info) {
    uint8_t header[30]{};
    HRESULT hr = reader.ReadAt(0, header, sizeof(header));
    if (FAILED(hr)) return hr;
    if (std::memcmp(header, "8BPS", 4) != 0) return E_FAIL;
    info.version = ReadBe16(header + 4);
    info.channels = ReadBe16(header + 12);
    info.height = ReadBe32(header + 14);
    info.width = ReadBe32(header + 18);
    info.depth = ReadBe16(header + 22);
    info.colorMode = ReadBe16(header + 24);
    if ((info.version != 1 && info.version != 2) || info.channels == 0 ||
        info.channels > 56 || info.width == 0 || info.height == 0) return E_FAIL;

    uint64_t cursor = 26;
    uint64_t next = 0;
    const uint32_t colorDataLength = ReadBe32(header + 26);
    if (!AddWithin(cursor + 4, colorDataLength, reader.Size(), next)) return E_FAIL;
    cursor = next;
    uint8_t lengthBytes[4]{};
    hr = reader.ReadAt(cursor, lengthBytes, sizeof(lengthBytes));
    if (FAILED(hr)) return hr;
    const uint32_t resourcesLength = ReadBe32(lengthBytes);
    info.resourcesStart = cursor + 4;
    if (!AddWithin(info.resourcesStart, resourcesLength, reader.Size(), info.resourcesEnd)) return E_FAIL;

    cursor = info.resourcesEnd;
    uint8_t layerLengthBytes[8]{};
    const size_t lengthSize = info.version == 2 ? 8 : 4;
    hr = reader.ReadAt(cursor, layerLengthBytes, lengthSize);
    if (FAILED(hr)) return hr;
    const uint64_t layerLength = info.version == 2 ? ReadBe64(layerLengthBytes)
                                                   : ReadBe32(layerLengthBytes);
    if (!AddWithin(cursor + lengthSize, layerLength, reader.Size(), info.compositeStart)) return E_FAIL;
    return S_OK;
}

HRESULT DecodeThumbnailResource(const StreamReader& reader, const PsdInfo& info,
                                UINT edge, HBITMAP* bitmap) {
    struct Candidate { uint64_t offset = 0; uint32_t length = 0; } oldThumb, newThumb;
    uint64_t cursor = info.resourcesStart;
    while (cursor + 12 <= info.resourcesEnd) {
        uint8_t fixed[7]{};
        HRESULT hr = reader.ReadAt(cursor, fixed, sizeof(fixed));
        if (FAILED(hr)) return hr;
        if (std::memcmp(fixed, "8BIM", 4) != 0 && std::memcmp(fixed, "MeSa", 4) != 0) break;
        const uint16_t resourceId = ReadBe16(fixed + 4);
        const uint8_t nameLength = fixed[6];
        uint64_t nameFieldLength = static_cast<uint64_t>(1) + nameLength;
        if (nameFieldLength & 1) ++nameFieldLength;
        uint64_t sizePosition = 0;
        if (!AddWithin(cursor + 6, nameFieldLength, info.resourcesEnd, sizePosition) ||
            sizePosition + 4 > info.resourcesEnd) break;
        uint8_t sizeBytes[4]{};
        hr = reader.ReadAt(sizePosition, sizeBytes, sizeof(sizeBytes));
        if (FAILED(hr)) return hr;
        const uint32_t dataLength = ReadBe32(sizeBytes);
        const uint64_t dataOffset = sizePosition + 4;
        uint64_t dataEnd = 0;
        if (!AddWithin(dataOffset, dataLength, info.resourcesEnd, dataEnd)) break;
        if (resourceId == 1033) oldThumb = {dataOffset, dataLength};
        if (resourceId == 1036) newThumb = {dataOffset, dataLength};
        cursor = dataEnd + (dataLength & 1u);
    }

    const Candidate candidates[] = {newThumb, oldThumb};
    for (const Candidate& candidate : candidates) {
        if (candidate.length <= 28 || candidate.length > 64u * 1024u * 1024u) continue;
        uint8_t thumbnailHeader[28]{};
        HRESULT hr = reader.ReadAt(candidate.offset, thumbnailHeader, sizeof(thumbnailHeader));
        if (FAILED(hr) || ReadBe32(thumbnailHeader) != 1) continue;
        std::vector<uint8_t> jpeg;
        hr = reader.ReadVector(candidate.offset + 28, candidate.length - 28, jpeg);
        if (FAILED(hr)) continue;
        hr = DecodeEncodedImage(jpeg.data(), jpeg.size(), edge, bitmap);
        if (SUCCEEDED(hr)) return hr;
    }
    return E_FAIL;
}

bool DecodePackBits(const uint8_t* encoded, size_t encodedLength,
                    uint8_t* decoded, size_t decodedLength) noexcept {
    size_t source = 0;
    size_t destination = 0;
    while (source < encodedLength && destination < decodedLength) {
        const int8_t control = static_cast<int8_t>(encoded[source++]);
        if (control >= 0) {
            const size_t count = static_cast<size_t>(control) + 1;
            if (count > encodedLength - source || count > decodedLength - destination) return false;
            std::memcpy(decoded + destination, encoded + source, count);
            source += count;
            destination += count;
        } else if (control != -128) {
            const size_t count = static_cast<size_t>(1 - control);
            if (source >= encodedLength || count > decodedLength - destination) return false;
            std::memset(decoded + destination, encoded[source++], count);
            destination += count;
        }
    }
    return destination == decodedLength;
}

HRESULT DecodeComposite(const StreamReader& reader, const PsdInfo& info,
                        UINT edge, HBITMAP* bitmap) {
    if (info.depth != 8 || (info.colorMode != 1 && info.colorMode != 3 && info.colorMode != 4))
        return E_NOTIMPL;
    const unsigned requiredChannels = info.colorMode == 1 ? 1 : (info.colorMode == 3 ? 3 : 4);
    if (info.channels < requiredChannels || info.width > 1000000 || info.height > 1000000)
        return E_FAIL;

    uint8_t compressionBytes[2]{};
    HRESULT hr = reader.ReadAt(info.compositeStart, compressionBytes, sizeof(compressionBytes));
    if (FAILED(hr)) return hr;
    const uint16_t compression = ReadBe16(compressionBytes);
    if (compression != 0 && compression != 1) return E_NOTIMPL;

    const double scale = std::min(1.0, static_cast<double>(edge) /
                                       std::max(info.width, info.height));
    const UINT outWidth = std::max<UINT>(1, static_cast<UINT>(std::lround(info.width * scale)));
    const UINT outHeight = std::max<UINT>(1, static_cast<UINT>(std::lround(info.height * scale)));
    std::vector<uint8_t> output(static_cast<size_t>(outWidth) * outHeight * 4);
    std::vector<std::vector<uint8_t>> rows(requiredChannels,
                                           std::vector<uint8_t>(info.width));

    std::vector<uint32_t> rowLengths;
    std::vector<uint64_t> rowOffsets;
    uint64_t pixelDataStart = info.compositeStart + 2;
    if (compression == 1) {
        const uint64_t rowCount64 = static_cast<uint64_t>(info.channels) * info.height;
        const size_t countBytes = info.version == 2 ? 4 : 2;
        if (rowCount64 > 16000000 || rowCount64 > SIZE_MAX / countBytes) return E_FAIL;
        const size_t rowCount = static_cast<size_t>(rowCount64);
        std::vector<uint8_t> lengths(rowCount * countBytes);
        hr = reader.ReadAt(pixelDataStart, lengths.data(), lengths.size());
        if (FAILED(hr)) return hr;
        rowLengths.resize(rowCount);
        rowOffsets.resize(rowCount);
        uint64_t cursor = pixelDataStart + lengths.size();
        for (size_t index = 0; index < rowCount; ++index) {
            const uint32_t length = countBytes == 4 ? ReadBe32(lengths.data() + index * 4)
                                                    : ReadBe16(lengths.data() + index * 2);
            if (length > static_cast<uint64_t>(info.width) * 2 + 1024) return E_FAIL;
            rowLengths[index] = length;
            rowOffsets[index] = cursor;
            if (cursor > reader.Size() || length > reader.Size() - cursor) return E_FAIL;
            cursor += length;
        }
    }

    std::vector<uint8_t> compressed;
    uint32_t previousSourceY = std::numeric_limits<uint32_t>::max();
    for (UINT outY = 0; outY < outHeight; ++outY) {
        const uint32_t sourceY = std::min<uint32_t>(info.height - 1,
            static_cast<uint32_t>((static_cast<uint64_t>(outY) * info.height) / outHeight));
        if (sourceY != previousSourceY) {
            for (unsigned channel = 0; channel < requiredChannels; ++channel) {
                if (compression == 0) {
                    const uint64_t planeBytes = static_cast<uint64_t>(info.width) * info.height;
                    const uint64_t offset = pixelDataStart + planeBytes * channel +
                                            static_cast<uint64_t>(sourceY) * info.width;
                    hr = reader.ReadAt(offset, rows[channel].data(), rows[channel].size());
                    if (FAILED(hr)) return hr;
                } else {
                    const size_t index = static_cast<size_t>(channel) * info.height + sourceY;
                    compressed.resize(rowLengths[index]);
                    hr = reader.ReadAt(rowOffsets[index], compressed.data(), compressed.size());
                    if (FAILED(hr) || !DecodePackBits(compressed.data(), compressed.size(),
                                                     rows[channel].data(), rows[channel].size()))
                        return E_FAIL;
                }
            }
            previousSourceY = sourceY;
        }

        for (UINT outX = 0; outX < outWidth; ++outX) {
            const uint32_t sourceX = std::min<uint32_t>(info.width - 1,
                static_cast<uint32_t>((static_cast<uint64_t>(outX) * info.width) / outWidth));
            uint8_t red = 0, green = 0, blue = 0;
            if (info.colorMode == 1) {
                red = green = blue = rows[0][sourceX];
            } else if (info.colorMode == 3) {
                red = rows[0][sourceX];
                green = rows[1][sourceX];
                blue = rows[2][sourceX];
            } else {
                red = static_cast<uint8_t>((static_cast<unsigned>(rows[0][sourceX]) * rows[3][sourceX]) / 255);
                green = static_cast<uint8_t>((static_cast<unsigned>(rows[1][sourceX]) * rows[3][sourceX]) / 255);
                blue = static_cast<uint8_t>((static_cast<unsigned>(rows[2][sourceX]) * rows[3][sourceX]) / 255);
            }
            const size_t destination = (static_cast<size_t>(outY) * outWidth + outX) * 4;
            output[destination] = blue;
            output[destination + 1] = green;
            output[destination + 2] = red;
            output[destination + 3] = 255;
        }
    }
    return PixelsToBitmap(output.data(), outWidth, outHeight, bitmap);
}

} // namespace

HRESULT DecodePsd(const StreamReader& reader, UINT edge, HBITMAP* bitmap) noexcept {
    try {
        PsdInfo info;
        HRESULT hr = ReadInfo(reader, info);
        if (FAILED(hr)) return hr;
        hr = DecodeComposite(reader, info, edge, bitmap);
        if (SUCCEEDED(hr)) return hr;
        return DecodeThumbnailResource(reader, info, edge, bitmap);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace artthumb
