#include "Provider/ClassFactory.h"

#include "Provider/Module.h"
#include "Provider/ThumbnailProvider.h"

#include <new>

namespace artthumb {

ClassFactory::ClassFactory() noexcept { ObjectCreated(); }
ClassFactory::~ClassFactory() { ObjectDestroyed(); }

HRESULT ClassFactory::QueryInterface(REFIID iid, void** object) noexcept {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
    *object = static_cast<IClassFactory*>(this);
    AddRef();
    return S_OK;
}

ULONG ClassFactory::AddRef() noexcept {
    return static_cast<ULONG>(InterlockedIncrement(&refCount_));
}

ULONG ClassFactory::Release() noexcept {
    const auto count = static_cast<ULONG>(InterlockedDecrement(&refCount_));
    if (count == 0) delete this;
    return count;
}

HRESULT ClassFactory::CreateInstance(IUnknown* outer, REFIID iid, void** object) noexcept {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    auto* provider = new (std::nothrow) ThumbnailProvider();
    if (!provider) return E_OUTOFMEMORY;
    HRESULT hr = provider->QueryInterface(iid, object);
    provider->Release();
    return hr;
}

HRESULT ClassFactory::LockServer(BOOL lock) noexcept {
    if (lock) InterlockedIncrement(&g_lockCount);
    else InterlockedDecrement(&g_lockCount);
    return S_OK;
}

} // namespace artthumb
