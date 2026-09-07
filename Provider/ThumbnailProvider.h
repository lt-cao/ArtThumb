#pragma once

#include <shobjidl.h>
#include <thumbcache.h>
#include <string>

namespace artthumb {

class ThumbnailProvider final : public IInitializeWithStream, public IThumbnailProvider {
public:
    ThumbnailProvider() noexcept;

    IFACEMETHODIMP QueryInterface(REFIID iid, void** object) noexcept override;
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override;
    IFACEMETHODIMP_(ULONG) Release() noexcept override;

    IFACEMETHODIMP Initialize(IStream* stream, DWORD mode) noexcept override;
    IFACEMETHODIMP GetThumbnail(UINT edge, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) noexcept override;

private:
    ~ThumbnailProvider();
    volatile long refCount_ = 1;
    IStream* stream_ = nullptr;
    std::wstring extension_;
};

} // namespace artthumb
