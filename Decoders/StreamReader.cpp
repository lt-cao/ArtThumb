#include "Decoders/StreamReader.h"

#include <algorithm>
#include <limits>

namespace artthumb {

StreamReader::StreamReader(IStream* stream) noexcept : stream_(stream) {
    if (stream) stream->AddRef();
    STATSTG stat{};
    if (stream && SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart >= 0) {
        size_ = static_cast<uint64_t>(stat.cbSize.QuadPart);
        sizeKnown_ = true;
    }
}

HRESULT StreamReader::ReadAt(uint64_t offset, void* destination, size_t byteCount) const noexcept {
    if (!stream_ || (!destination && byteCount != 0)) return E_INVALIDARG;
    if (offset > size_ || byteCount > size_ - offset) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
    if (byteCount > std::numeric_limits<ULONG>::max()) return E_INVALIDARG;
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    HRESULT hr = stream_->Seek(position, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return hr;
    ULONG bytesRead = 0;
    hr = stream_->Read(destination, static_cast<ULONG>(byteCount), &bytesRead);
    if (FAILED(hr)) return hr;
    return bytesRead == byteCount ? S_OK : HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
}

HRESULT StreamReader::ReadVector(uint64_t offset, size_t byteCount,
                                 std::vector<uint8_t>& result) const {
    result.clear();
    if (offset > size_ || byteCount > size_ - offset) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
    result.resize(byteCount);
    HRESULT hr = ReadAt(offset, result.data(), result.size());
    if (FAILED(hr)) result.clear();
    return hr;
}

HRESULT StreamReader::ReadPrefix(size_t maximumBytes, std::vector<uint8_t>& result) const {
    return ReadVector(0, static_cast<size_t>(std::min<uint64_t>(size_, maximumBytes)), result);
}

HRESULT StreamReader::ReadTail(size_t maximumBytes, std::vector<uint8_t>& result,
                               uint64_t& baseOffset) const {
    const size_t length = static_cast<size_t>(std::min<uint64_t>(size_, maximumBytes));
    baseOffset = size_ - length;
    return ReadVector(baseOffset, length, result);
}

uint16_t ReadBe16(const uint8_t* value) noexcept {
    return static_cast<uint16_t>((value[0] << 8) | value[1]);
}

uint32_t ReadBe32(const uint8_t* value) noexcept {
    return (static_cast<uint32_t>(value[0]) << 24) |
           (static_cast<uint32_t>(value[1]) << 16) |
           (static_cast<uint32_t>(value[2]) << 8) |
           static_cast<uint32_t>(value[3]);
}

uint64_t ReadBe64(const uint8_t* value) noexcept {
    return (static_cast<uint64_t>(ReadBe32(value)) << 32) | ReadBe32(value + 4);
}

uint32_t ReadLe32(const uint8_t* value) noexcept {
    return static_cast<uint32_t>(value[0]) |
           (static_cast<uint32_t>(value[1]) << 8) |
           (static_cast<uint32_t>(value[2]) << 16) |
           (static_cast<uint32_t>(value[3]) << 24);
}

} // namespace artthumb
