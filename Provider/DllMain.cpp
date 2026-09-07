#include "Provider/ClassFactory.h"
#include "Provider/Module.h"
#include "Provider/Registration.h"

#include <new>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        artthumb::g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return artthumb::g_objectCount == 0 && artthumb::g_lockCount == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(
    REFCLSID clsid, REFIID iid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (clsid != artthumb::kThumbnailProviderClsid) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) artthumb::ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, object);
    factory->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllRegisterServer() {
    return artthumb::RegisterProviderForCurrentUser();
}

extern "C" HRESULT __stdcall DllUnregisterServer() {
    return artthumb::UnregisterProviderForCurrentUser();
}
