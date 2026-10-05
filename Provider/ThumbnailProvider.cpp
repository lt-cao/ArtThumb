#include "Provider/ThumbnailProvider.h"

#include "Decoders/ThumbnailPipeline.h"
#include "Provider/Module.h"

#include <algorithm>
#include <new>

namespace artthumb {

ThumbnailProvider::ThumbnailProvider() noexcept { ObjectCreated(); }

ThumbnailProvider::~ThumbnailProvider() {
    if (stream_) stream_->Release();
    ObjectDestroyed();
}

HRESULT ThumbnailProvider::QueryInterface(REFIID iid, void** object) noexcept {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_IInitializeWithStream) {
        *object = static_cast<IInitializeWithStream*>(this);
    } else if (iid == __uuidof(IThumbnailProvider)) {
        *object = static_cast<IThumbnailProvider*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG ThumbnailProvider::AddRef() noexcept {
    return static_cast<ULONG>(InterlockedIncrement(&refCount_));
}

ULONG ThumbnailProvider::Release() noexcept {
    const auto count = static_cast<ULONG>(InterlockedDecrement(&refCount_));
    if (count == 0) delete this;
    return count;
}

HRESULT ThumbnailProvider::Initialize(IStream* stream, DWORD) noexcept {
    if (!stream) return E_INVALIDARG;
    if (stream_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    stream_ = stream;
    stream_->AddRef();
    return S_OK;
}

HRESULT ThumbnailProvider::GetThumbnail(UINT edge, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) noexcept {
    if (!stream_ || !bitmap || !alpha || edge == 0) return E_INVALIDARG;
    *bitmap = nullptr;
    *alpha = WTSAT_UNKNOWN;
    try {
        const UINT boundedEdge = std::min<UINT>(edge, 4096);
        ThumbnailKind kind = ThumbnailKind::Unknown;
        HRESULT hr = DecodeThumbnail(stream_, boundedEdge, bitmap, &kind);
        if (SUCCEEDED(hr) && *bitmap) {
            *alpha = WTSAT_ARGB;
        }
        return hr;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace artthumb
