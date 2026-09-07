#include "Provider/Module.h"

namespace artthumb {

HMODULE g_module = nullptr;
volatile long g_objectCount = 0;
volatile long g_lockCount = 0;

void ObjectCreated() noexcept { InterlockedIncrement(&g_objectCount); }
void ObjectDestroyed() noexcept { InterlockedDecrement(&g_objectCount); }

} // namespace artthumb
