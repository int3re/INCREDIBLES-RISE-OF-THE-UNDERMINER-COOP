#include "mod/boot.h"

#include <windows.h>

#include <intrin.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "common/log.h"
#ifdef IN2NET_LAB_DUMP
#include "mod/dump.h"
#endif
#include "mod/game.h"
#include "mod/netplay.h"
#include "mod/patch.h"
#include "mod/saves.h"
#include "mod/sim.h"
#include "mod/video.h"

namespace boot {
namespace {

Config g_cfg;
Ready g_on_ready = nullptr;
volatile LONG g_ready = 0;
volatile LONG g_dumped = 0;
uintptr_t g_image_end = 0;

bool from_image(uintptr_t ra) { return ra >= game::kImageBase && ra < g_image_end; }
bool from_game_code(uintptr_t ra) { return ra >= game::kTextBegin && ra < game::kTextEnd; }

const char* section_of(uintptr_t ra) {
  auto base = reinterpret_cast<const uint8_t*>(game::kImageBase);
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  auto sec = IMAGE_FIRST_SECTION(nt);
  static char names[32][9];
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && i < 32; ++i) {
    uintptr_t b = game::kImageBase + sec[i].VirtualAddress;
    if (ra >= b && ra < b + sec[i].Misc.VirtualSize) {
      memcpy(names[i], sec[i].Name, 8);
      names[i][8] = 0;
      return names[i];
    }
  }
  return "?";
}

[[noreturn]] void end_process(const char* why) {
  LOGI("boot: ending the process: %s", why);
  TerminateProcess(GetCurrentProcess(), 0);
  for (;;) Sleep(1000);
}

// The lab's picture of the game's memory ([debug] dump): only in a build made with the lab's dumper
// (CMake: IN2NET_LAB_DUMP), which is not part of the public source or of a release.
void dump_once(const char* reason, uintptr_t caller) {
#ifdef IN2NET_LAB_DUMP
  if (g_cfg.dump == "off") return;
  if (InterlockedExchange(&g_dumped, 1) != 0) return;
  dump::write_image(module_dir(), reason, caller);
#else
  (void)reason;
  (void)caller;
  (void)g_dumped;
#endif
}

// Every hooked start-up API reports here.
void note(const char* api, uintptr_t ra, int& seen) {
  if (!from_image(ra)) return;
  if (g_cfg.boot_trace && seen < 6) {
    ++seen;
    LOGI("boot: %s called from 0x%08X (%s)", api, (unsigned)ra, section_of(ra));
  }
  if (from_game_code(ra) && InterlockedExchange(&g_ready, 1) == 0) {
    LOGI("boot: the game's code runs: %s called from 0x%08X", api, (unsigned)ra);
    if (g_on_ready) g_on_ready(api, ra);
  }
}

// --- kernel32 ---------------------------------------------------------------
BOOL(WINAPI* oGetVersionExA)(LPOSVERSIONINFOA) = nullptr;
BOOL WINAPI hkGetVersionExA(LPOSVERSIONINFOA p) {
  static int seen = 0;
  note("GetVersionExA", reinterpret_cast<uintptr_t>(_ReturnAddress()), seen);
  return oGetVersionExA(p);
}

DWORD(WINAPI* oGetVersion)() = nullptr;
DWORD WINAPI hkGetVersion() {
  static int seen = 0;
  note("GetVersion", reinterpret_cast<uintptr_t>(_ReturnAddress()), seen);
  return oGetVersion();
}

LPSTR(WINAPI* oGetCommandLineA)() = nullptr;
LPSTR WINAPI hkGetCommandLineA() {
  static int seen = 0;
  note("GetCommandLineA", reinterpret_cast<uintptr_t>(_ReturnAddress()), seen);
  return oGetCommandLineA();
}

void(WINAPI* oGetStartupInfoA)(LPSTARTUPINFOA) = nullptr;
void WINAPI hkGetStartupInfoA(LPSTARTUPINFOA p) {
  static int seen = 0;
  note("GetStartupInfoA", reinterpret_cast<uintptr_t>(_ReturnAddress()), seen);
  oGetStartupInfoA(p);
}

// The game allows one copy of itself (a named mutex). [debug] multi: every copy gets a mutex
// of its own, so the lab can run two.
HANDLE(WINAPI* oCreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR) = nullptr;
HANDLE WINAPI hkCreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name) {
  if (name && strcmp(name, "Incredi2Mutex") == 0) {
    char own[64];
    snprintf(own, sizeof(own), "Incredi2Mutex-%lu", GetCurrentProcessId());
    return oCreateMutexA(sa, owner, own);
  }
  return oCreateMutexA(sa, owner, name);
}

// --- user32 -----------------------------------------------------------------
ATOM(WINAPI* oRegisterClassA)(const WNDCLASSA*) = nullptr;
ATOM WINAPI hkRegisterClassA(const WNDCLASSA* wc) {
  static int seen = 0;
  uintptr_t ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
  note("RegisterClassA", ra, seen);
  if (from_image(ra) && wc)
    LOGI("boot: window class '%s' wndproc 0x%08X style 0x%X", IS_INTRESOURCE(wc->lpszClassName) ? "#" : wc->lpszClassName,
         (unsigned)(uintptr_t)wc->lpfnWndProc, wc->style);
  return oRegisterClassA(wc);
}

ATOM(WINAPI* oRegisterClassExA)(const WNDCLASSEXA*) = nullptr;
ATOM WINAPI hkRegisterClassExA(const WNDCLASSEXA* wc) {
  static int seen = 0;
  uintptr_t ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
  note("RegisterClassExA", ra, seen);
  if (from_image(ra) && wc)
    LOGI("boot: window class '%s' wndproc 0x%08X style 0x%X", IS_INTRESOURCE(wc->lpszClassName) ? "#" : wc->lpszClassName,
         (unsigned)(uintptr_t)wc->lpfnWndProc, wc->style);
  return oRegisterClassExA(wc);
}

HWND(WINAPI* oCreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) = nullptr;
HWND WINAPI hkCreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h, HWND parent,
                              HMENU menu, HINSTANCE inst, LPVOID param) {
  static int seen = 0;
  uintptr_t ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
  note("CreateWindowExA", ra, seen);
  if (from_image(ra)) {
    LOGI("boot: window '%s' class '%s' style 0x%08X ex 0x%08X at %d,%d %dx%d", title ? title : "",
         IS_INTRESOURCE(cls) ? "#" : cls, style, ex, x, y, w, h);
    if (g_cfg.dump == "window") {
      dump_once("CreateWindowExA", ra);
      end_process("the image is dumped ([debug] dump = window)");
    }
    saves::on_window();
    netplay::meet();
    if (video::window_args(ex, style, x, y, w, h))
      LOGI("boot: window made as style 0x%08X ex 0x%08X at %d,%d %dx%d", style, ex, x, y, w, h);
    HWND made = oCreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    if (made) video::window_made(made);
    return made;
  }
  return oCreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
}

HWND(WINAPI* oCreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) = nullptr;
HWND WINAPI hkCreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND parent,
                              HMENU menu, HINSTANCE inst, LPVOID param) {
  static int seen = 0;
  uintptr_t ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
  note("CreateWindowExW", ra, seen);
  if (from_image(ra)) {
    LOGI("boot: window '%ls' class '%ls' style 0x%08X ex 0x%08X at %d,%d %dx%d", title ? title : L"",
         IS_INTRESOURCE(cls) ? L"#" : cls, style, ex, x, y, w, h);
    if (g_cfg.dump == "window") {
      dump_once("CreateWindowExW", ra);
      end_process("the image is dumped ([debug] dump = window)");
    }
  }
  return oCreateWindowExW(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
}

BOOL(WINAPI* oShowWindow)(HWND, int) = nullptr;
BOOL WINAPI hkShowWindow(HWND wnd, int cmd) {
  if (g_cfg.dump != "off" && cmd != SW_HIDE) {
    dump_once("ShowWindow (guard)", reinterpret_cast<uintptr_t>(_ReturnAddress()));
    end_process("dump: the game was about to show a window");
  }
  if (wnd && wnd == video::window()) cmd = video::show_command(cmd);
  return oShowWindow(wnd, cmd);
}

LONG(WINAPI* oChangeDisplaySettingsA)(DEVMODEA*, DWORD) = nullptr;
LONG WINAPI hkChangeDisplaySettingsA(DEVMODEA* mode, DWORD flags) {
  if (g_cfg.guard) {
    if (!mode) return DISP_CHANGE_SUCCESSFUL;  // "back to the desktop's own mode": nothing was changed
    dump_once("ChangeDisplaySettingsA (guard)", reinterpret_cast<uintptr_t>(_ReturnAddress()));
    end_process("guard: the game was about to change the display mode");
  }
  return oChangeDisplaySettingsA(mode, flags);
}

LONG(WINAPI* oChangeDisplaySettingsExA)(LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID) = nullptr;
LONG WINAPI hkChangeDisplaySettingsExA(LPCSTR dev, DEVMODEA* mode, HWND wnd, DWORD flags, LPVOID param) {
  if (g_cfg.guard) {
    if (!mode) return DISP_CHANGE_SUCCESSFUL;  // "back to the desktop's own mode": nothing was changed
    dump_once("ChangeDisplaySettingsExA (guard)", reinterpret_cast<uintptr_t>(_ReturnAddress()));
    end_process("guard: the game was about to change the display mode");
  }
  return oChangeDisplaySettingsExA(dev, mode, wnd, flags, param);
}

LONG(WINAPI* oChangeDisplaySettingsExW)(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID) = nullptr;
LONG WINAPI hkChangeDisplaySettingsExW(LPCWSTR dev, DEVMODEW* mode, HWND wnd, DWORD flags, LPVOID param) {
  if (g_cfg.guard) {
    if (!mode) return DISP_CHANGE_SUCCESSFUL;  // "back to the desktop's own mode": nothing was changed
    dump_once("ChangeDisplaySettingsExW (guard)", reinterpret_cast<uintptr_t>(_ReturnAddress()));
    end_process("guard: the game was about to change the display mode");
  }
  return oChangeDisplaySettingsExW(dev, mode, wnd, flags, param);
}

DWORD WINAPI autoquit_thread(void*) {
  Sleep(static_cast<DWORD>(g_cfg.autoquit_s) * 1000);
  dump_once("autoquit", 0);
  sim::flush();
  end_process("[debug] autoquit");
}

}  // namespace

bool ready() { return g_ready != 0; }

void install(const Config& cfg, Ready on_ready) {
  g_cfg = cfg;
  g_on_ready = on_ready;
  auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  g_image_end = reinterpret_cast<uintptr_t>(base) + nt->OptionalHeader.SizeOfImage;

#define HOOK(dll, fn) patch::hook_api(dll, #fn, hk##fn, reinterpret_cast<void**>(&o##fn))
  HOOK("kernel32.dll", GetVersionExA);
  HOOK("kernel32.dll", GetVersion);
  HOOK("kernel32.dll", GetCommandLineA);
  HOOK("kernel32.dll", GetStartupInfoA);
  if (cfg.multi) HOOK("kernel32.dll", CreateMutexA);
  HOOK("user32.dll", RegisterClassA);
  HOOK("user32.dll", RegisterClassExA);
  HOOK("user32.dll", CreateWindowExA);
  HOOK("user32.dll", CreateWindowExW);
  HOOK("user32.dll", ShowWindow);
  if (cfg.guard) {
    HOOK("user32.dll", ChangeDisplaySettingsA);
    HOOK("user32.dll", ChangeDisplaySettingsExA);
    HOOK("user32.dll", ChangeDisplaySettingsExW);
  }
#undef HOOK

  if (cfg.autoquit_s > 0) {
    HANDLE t = CreateThread(nullptr, 0, autoquit_thread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
  }
}

}  // namespace boot
