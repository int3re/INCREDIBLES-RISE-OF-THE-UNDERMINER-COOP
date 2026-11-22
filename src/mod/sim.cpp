#include "mod/sim.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

#include "common/log.h"
#include "mod/game.h"
#include "mod/hud.h"
#include "mod/netpage.h"
#include "mod/netplay.h"
#include "mod/patch.h"
#include "mod/sound.h"

namespace sim {
namespace {

Config g_cfg;
std::atomic<uint32_t> g_frame{0};
DWORD g_main_thread = 0;

// --- the clock ------------------------------------------------------------------
// frame * 1000 / 60, rounded, plus the "stall" milliseconds: code that waits for the
// clock in a loop of its own (no game frame passes) would wait for ever, so after
// kFreeCalls askings within one frame every further asking moves the clock by 1 ms.
// The clock is then still a pure function of what the game did, not of real time.
const uint32_t kFreeCalls = 4000;
std::atomic<uint64_t> g_stall_ms{0};
uint32_t g_calls = 0;  // main thread only

bool g_modal = false;  // the game's settings screen runs: a loop outside the frames, on real time

uint64_t clock_ms() { return (static_cast<uint64_t>(g_frame.load()) * 1000 + 30) / 60 + g_stall_ms.load(); }

uint64_t asked() {
  if (g_modal) return GetTickCount64();  // (kernel32's own: the screen keeps its own time)
  if (GetCurrentThreadId() == g_main_thread || !g_main_thread) {
    if (++g_calls > kFreeCalls) g_stall_ms.fetch_add(1);
  }
  return clock_ms();
}

// iTimeGet: unsigned 64-bit milliseconds in edx:eax.
using TimeGet_t = uint64_t(__cdecl*)();
TimeGet_t oTimeGet = nullptr;
uint64_t __cdecl hkTimeGet() { return asked(); }

// iTimeDiffSec(t0, t1): seconds as a float in st(0). A whole number of frames gives
// exactly n / 60 (the clock's milliseconds are rounded).
using TimeDiffSec_t = float(__cdecl*)(uint64_t, uint64_t);
TimeDiffSec_t oTimeDiffSec = nullptr;
float __cdecl hkTimeDiffSec(uint64_t t0, uint64_t t1) {
  int64_t d = static_cast<int64_t>(t1 - t0);
  int64_t frames = (d * 60 + 500) / 1000;
  if (d >= 0 && std::llabs(d * 60 - frames * 1000) <= 60) return static_cast<float>(frames) / 60.0f;
  return static_cast<float>(d) * 0.001f;
}

// GetTickCount as the game's code sees it (its import slot).
DWORD(WINAPI* oGetTickCount)() = nullptr;
DWORD WINAPI hkGetTickCount() { return static_cast<DWORD>(asked()); }

// --- the frame gate ------------------------------------------------------------------
using InputUpdate_t = void(__cdecl*)();
InputUpdate_t oInputUpdate = nullptr;
FILE* g_log = nullptr;

void pace() {
  static LARGE_INTEGER freq{}, next{};
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  LONGLONG period = freq.QuadPart / 60;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (!next.QuadPart || now.QuadPart - next.QuadPart > period * 4) next = now;  // start / long stall
  for (;;) {
    QueryPerformanceCounter(&now);
    LONGLONG left = next.QuadPart - now.QuadPart;
    if (left <= 0) break;
    if (left * 1000 / freq.QuadPart >= 2)
      Sleep(1);
    else
      YieldProcessor();
  }
  next.QuadPart += period;
}

uint32_t scene_id() {
  auto p = patch::read<const uint32_t*>(game::kScenePtr);
  return p ? *p : 0;
}

void log_frame() {
  if (!g_log) return;
  uint32_t f = g_frame.load();
  uint32_t sc = scene_id();
  char name[5] = {static_cast<char>(sc >> 24), static_cast<char>(sc >> 16), static_cast<char>(sc >> 8), static_cast<char>(sc), 0};
  for (char& c : name)
    if (c && (c < 32 || c > 126)) c = '?';
  uintptr_t next = patch::read<uint32_t>(game::kRngNext);
  fprintf(g_log, "%u %s rng %d %d pads %08X %08X dt %.6f stall %llu\n", f, sc ? name : "-",
          patch::read<int32_t>(game::kRngLeft), next ? static_cast<int>((next - game::kRngState) / 4) : -1,
          patch::read<uint32_t>(game::kPads + game::kPadOn), patch::read<uint32_t>(game::kPads + game::kPadSize + game::kPadOn),
          patch::read<float>(game::kTimeElapsed), static_cast<unsigned long long>(g_stall_ms.load()));
  if (f % 60 == 0) fflush(g_log);
}

// --- [debug] rng_trace: who draws random numbers ------------------------------------------
// Every draw decrements game::kRngLeft. For the frames asked for, the page that holds it is
// made read-only: each write faults, the faulting instruction (and the code addresses on top
// of the stack) is written to in2net-rng.log, the write is let through by one single step.
// Two runs' traces show the first draw one of them makes and the other does not.
FILE* g_rng_log = nullptr;
bool g_rng_armed = false;
uintptr_t g_rng_page = game::kRngLeft & ~0xFFFu;

void rng_protect(bool on) {
  DWORD old;
  VirtualProtect(reinterpret_cast<void*>(g_rng_page), 0x1000, on ? PAGE_READONLY : PAGE_READWRITE, &old);
}

LONG CALLBACK rng_veh(EXCEPTION_POINTERS* ep) {
  const EXCEPTION_RECORD* r = ep->ExceptionRecord;
  CONTEXT* c = ep->ContextRecord;
  if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && g_rng_armed && r->ExceptionInformation[0] == 1 &&
      (r->ExceptionInformation[1] & ~0xFFFu) == g_rng_page) {
    if (r->ExceptionInformation[1] == game::kRngLeft && g_rng_log && GetCurrentThreadId() == g_main_thread) {
      fprintf(g_rng_log, "%u %08X", g_frame.load(), static_cast<unsigned>(c->Eip));
      const uint32_t* sp = reinterpret_cast<const uint32_t*>(c->Esp);
      int shown = 0;
      for (int i = 0; i < 48 && shown < 5; ++i) {
        uint32_t v = sp[i];
        if ((v >= game::kTextBegin && v < game::kTextEnd) || (v >= 0x01055000 && v < 0x012A2000)) {
          fprintf(g_rng_log, " %08X", v);
          ++shown;
        }
      }
      fputc('\n', g_rng_log);
    }
    rng_protect(false);
    c->EFlags |= 0x100;  // one step, then the page is closed again
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && g_rng_armed) {
    rng_protect(true);
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void rng_trace_frame(uint32_t f) {
  if (!g_cfg.rng_trace_to) return;
  bool want = f >= g_cfg.rng_trace_from && f <= g_cfg.rng_trace_to;
  if (want && !g_rng_armed) {
    if (!g_rng_log) {
      g_rng_log = _wfopen((module_dir() + L"in2net-rng.log").c_str(), L"w");
      AddVectoredExceptionHandler(1, rng_veh);
    }
    g_rng_armed = true;
    rng_protect(true);
  } else if (!want && g_rng_armed) {
    g_rng_armed = false;
    rng_protect(false);
    if (g_rng_log) fflush(g_rng_log);
  }
}

// --- [debug] rng_calls: every draw's caller, at full speed --------------------------------
// All draws go through two functions (game::kRandFloat, game::kRandInt). Their callers'
// return addresses are kept in memory and written to in2net-rngcalls.bin at the end
// (pairs of dwords: frame, return address).
std::vector<uint32_t> g_calls_log;
void* oRandFloat = nullptr;
void* oRandInt = nullptr;

void __cdecl note_draw(uint32_t ret) {
  if (GetCurrentThreadId() != g_main_thread) ret |= 0x80000000u;  // a draw from another thread
  if (g_calls_log.size() < g_calls_log.capacity()) {
    g_calls_log.push_back(g_frame.load());
    g_calls_log.push_back(ret);
  }
}

__declspec(naked) void hkRandFloat() {
  __asm {
    push eax
    push ecx
    push edx
    push dword ptr [esp + 12]
    call note_draw
    add esp, 4
    pop edx
    pop ecx
    pop eax
    jmp dword ptr [oRandFloat]
  }
}

__declspec(naked) void hkRandInt() {
  __asm {
    push eax
    push ecx
    push edx
    push dword ptr [esp + 12]
    call note_draw
    add esp, 4
    pop edx
    pop ecx
    pop eax
    jmp dword ptr [oRandInt]
  }
}

void __cdecl hkInputUpdate() {
  if (!g_main_thread) g_main_thread = GetCurrentThreadId();
  log_frame();  // the frame that has just been played
  if (!g_cfg.fast) pace();
  ++g_frame;
  g_calls = 0;
  rng_trace_frame(g_frame.load());
  sound::on_frame();
  hud::on_frame(g_frame.load());
  netplay::begin_frame(g_frame.load());
  netpage::tick();
  // Levels are loaded inside the frame that asks for them: the engine's own loading screen
  // runs frames on a second thread for as long as the disk takes - never the same count twice.
  *reinterpret_cast<uint32_t*>(game::kAsyncLoading) = 0;
  // "Distance clipping" of the display options moves the camera's far plane - a part of the game's
  // logic: in a network game it is off on every PC.
  if (g_cfg.net_mode != Config::Net::Off) *reinterpret_cast<uint8_t*>(game::kDistanceClipping) = 0;
  if (g_cfg.force_hdr >= 0) *reinterpret_cast<uint8_t*>(game::kHdr) = static_cast<uint8_t>(g_cfg.force_hdr);
  if (g_cfg.force_shadows >= 0) *reinterpret_cast<uint8_t*>(game::kShadowsOff) = g_cfg.force_shadows ? 0 : 1;
  oInputUpdate();
}

// The game's settings screen (ОПЦИИ): a loop of its own inside one game frame, driven by the
// mouse and the keyboard of this PC alone. In a network game both PCs would open it at the same
// frame and each would sit in it for as long as its player likes, with nobody answering the
// network: it is not opened there. Alone it runs on real time, and leaves the game's clock as
// it found it.
using OptionsScreen_t = void(__cdecl*)(const char* xml);
OptionsScreen_t oOptionsScreen = nullptr;

void __cdecl hkOptionsScreen(const char* xml) {
  if (g_cfg.net_mode != Config::Net::Off) {
    static bool said = false;
    if (!said) LOGI("sim: the game's settings screen is not opened in a network game");
    said = true;
    return;
  }
  uint64_t stall = g_stall_ms.load();
  g_modal = true;
  oOptionsScreen(xml);
  g_modal = false;
  g_stall_ms.store(stall);
  g_calls = 0;
}

// The engine's own frame limiter (17 ms by GetTickCount + Sleep): with vertical sync it
// drops every second frame (32 fps). The mod paces frames itself.
void __cdecl hkFrameLimiter() {}

// The console build's progress picture for loading inside the frame: broken on the PC (see game.h).
void __cdecl hkSyncLoadingDraw(float, int, void*) {}

}  // namespace

void flush() {
  if (g_log) fflush(g_log);
  if (g_rng_log) fflush(g_rng_log);
  if (!g_calls_log.empty()) {
    FILE* f = _wfopen((module_dir() + L"in2net-rngcalls.bin").c_str(), L"wb");
    if (f) {
      fwrite(g_calls_log.data(), 4, g_calls_log.size(), f);
      fclose(f);
    }
  }
}

bool in_settings_screen() { return g_modal; }
uint32_t frame() { return g_frame.load(); }
uint32_t scene() { return scene_id(); }
uint64_t now_ms() { return clock_ms(); }

void install(const Config& cfg) {
  g_cfg = cfg;
  static const uint8_t jle[] = {0x7E, 0x09};
  static const uint8_t jmp[] = {0xEB, 0x09};
  if (!patch::replace("the game loop's own frame limiter", game::kLoopLimiterJle, jle, jmp, 2)) {
    LOGE("sim: this is not the game loop in2net knows; the game's time is left alone");
    return;
  }
  static void* unused = nullptr;
  patch::hook("frame limiter", game::kFrameLimiter, (void*)&hkFrameLimiter, &unused);
  static void* unused2 = nullptr;
  patch::hook("loading progress of the console build", game::kSyncLoadingDraw, (void*)&hkSyncLoadingDraw, &unused2);
  patch::hook("iTimeGet", game::kTimeGet, (void*)&hkTimeGet, (void**)&oTimeGet);
  patch::hook("the game's settings screen", game::kOptionsScreen, (void*)&hkOptionsScreen, (void**)&oOptionsScreen);
  patch::hook("iTimeDiffSec", game::kTimeDiffSec, (void*)&hkTimeDiffSec, (void**)&oTimeDiffSec);
  // (the exe's import directory is the packer's; the game's own table is written directly)
  {
    void* real = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetTickCount"));
    oGetTickCount = reinterpret_cast<DWORD(WINAPI*)()>(real);
    patch::slot("the game's GetTickCount", game::kIatGetTickCount, reinterpret_cast<uintptr_t>(real), (void*)&hkGetTickCount);
  }
  patch::hook("read all pads (frame gate)", game::kInputUpdate, (void*)&hkInputUpdate, (void**)&oInputUpdate);
  if (cfg.rng_calls) {
    g_calls_log.reserve(2 * 12000000);
    patch::hook("random float", game::kRandFloat, (void*)&hkRandFloat, &oRandFloat);
    patch::hook("random integer", game::kRandInt, (void*)&hkRandInt, &oRandInt);
  }
  if (cfg.sim_log) g_log = _wfopen((module_dir() + L"in2net-sim.log").c_str(), L"w");
  LOGI("sim: the game's clock is the mod's: 1/60 s a frame%s", cfg.fast ? ", frames not paced ([debug] fast)" : "");
}

}  // namespace sim
