// incrotu.exe is packed: when Ultimate ASI Loader brings the mod in, the game's
// code is not in memory yet. boot watches the start-up calls of Windows APIs
// and reports the first one that comes from the game's own code section: from
// then on the image is unpacked and its addresses can be patched.
//
// It is also the lab's guard: with [debug] guard / dump the process ends before
// it shows a window or changes the display mode.
#pragma once
#include <cstdint>

#include "mod/config.h"

namespace boot {

// Called once, on the game's main thread, inside the first API call made by the
// game's code. `api` names the call, `caller` is its return address.
using Ready = void (*)(const char* api, uintptr_t caller);

void install(const Config& cfg, Ready on_ready);

// True once the game's code was seen running.
bool ready();

}  // namespace boot
