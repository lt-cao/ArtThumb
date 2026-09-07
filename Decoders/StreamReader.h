#pragma once

#include "Provider/ComPtr.h"

#include <objidl.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace artthumb {

class StreamReader final {
public:
    explicit StreamReader(IStream* stream) noexcept;
    bool IsValid() const noexcept { return stream_ && sizeKnown_; }
    uint64_t Size() const noexcept { return size_; }

    HRESULT ReadAt(uint64_t offset, void* destination, size_t byteCount) const noexcept;
    HRESULT ReadVector(uint64_t offset, size_t byteCount, std::vector<uint8_t>& result) const;
    HRESULT ReadPrefix(size_t maximumBytes, std::vector<uint8_t>& result) const;
    HRESULT ReadTail(size_t maximumBytes, std::vector<uint8_t>& result, uint64_t& baseOffset) const;
    IStream* Stream() const noexcept { return stream_.Get(); }

private:
    ComPtr<IStream> stream_;
    uint64_t size_ = 0;
    bool sizeKnown_ = false;
};

uint16_t ReadBe16(const uint8_t* value) noexcept;
uint32_t ReadBe32(const uint8_t* value) noexcept;
uint64_t ReadBe64(const uint8_t* value) noexcept;
uint32_t ReadLe32(const uint8_t* value) noexcept;

} // namespace artthumb
