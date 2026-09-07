#pragma once

#include <unknwn.h>
#include <cstddef>
#include <utility>

namespace artthumb {

template <typename T>
class ComPtr final {
public:
    ComPtr() noexcept = default;
    ComPtr(std::nullptr_t) noexcept {}
    explicit ComPtr(T* value) noexcept : value_(value) {}
    ComPtr(const ComPtr& other) noexcept : value_(other.value_) {
        if (value_) value_->AddRef();
    }
    ComPtr(ComPtr&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ~ComPtr() { Reset(); }

    ComPtr& operator=(const ComPtr& other) noexcept {
        if (this != &other) {
            T* next = other.value_;
            if (next) next->AddRef();
            Reset();
            value_ = next;
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }

    T* Get() const noexcept { return value_; }
    T** Put() noexcept {
        Reset();
        return &value_;
    }
    void** PutVoid() noexcept { return reinterpret_cast<void**>(Put()); }
    T* operator->() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
    void Reset(T* next = nullptr) noexcept {
        if (value_) value_->Release();
        value_ = next;
    }
    T* Detach() noexcept { return std::exchange(value_, nullptr); }

private:
    T* value_ = nullptr;
};

} // namespace artthumb
