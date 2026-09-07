#pragma once

#include <windows.h>

namespace artthumb {

HRESULT RegisterProviderForCurrentUser() noexcept;
HRESULT UnregisterProviderForCurrentUser() noexcept;

} // namespace artthumb
