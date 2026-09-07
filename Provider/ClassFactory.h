#pragma once

#include <unknwn.h>

namespace artthumb {

class ClassFactory final : public IClassFactory {
public:
    ClassFactory() noexcept;
    IFACEMETHODIMP QueryInterface(REFIID iid, void** object) noexcept override;
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override;
    IFACEMETHODIMP_(ULONG) Release() noexcept override;
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** object) noexcept override;
    IFACEMETHODIMP LockServer(BOOL lock) noexcept override;
private:
    ~ClassFactory();
    volatile long refCount_ = 1;
};

} // namespace artthumb
