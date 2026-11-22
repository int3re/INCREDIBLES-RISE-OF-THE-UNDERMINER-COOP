#include "mod/input.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "common/log.h"
#include "mod/game.h"
#include "mod/netpage.h"
#include "mod/netplay.h"
#include "mod/patch.h"
#include "mod/sim.h"
#include "mod/video.h"

namespace input {
namespace {

Config g_cfg;

// The game's record: +0 packet number, +4 digital buttons (word), +6 eight pressure
// buttons, +0x10 four sticks as 32-bit integers.
#pragma pack(push, 1)
struct GameState {
  uint32_t packet;
  uint16_t buttons;
  uint8_t analog[8];
  uint8_t pad[2];
  int32_t lx, ly, rx, ry;
};
#pragma pack(pop)
static_assert(sizeof(GameState) == 0x20, "the game's pad state is 0x20 bytes");

Pad from_game(const GameState& s) {
  Pad p;
  p.buttons = static_cast<uint8_t>(s.buttons);
  memcpy(p.analog, s.analog, 8);
  auto clamp = [](int32_t v) { return static_cast<int16_t>(v < -32768 ? -32768 : v > 32767 ? 32767 : v); };
  p.lx = clamp(s.lx);
  p.ly = clamp(s.ly);
  p.rx = clamp(s.rx);
  p.ry = clamp(s.ry);
  return p;
}

void to_game(const Pad& p, GameState& s) {
  s.buttons = p.buttons;
  memcpy(s.analog, p.analog, 8);
  s.lx = p.lx;
  s.ly = p.ly;
  s.rx = p.rx;
  s.ry = p.ry;
}

// --- [debug] script: presses by frame --------------------------------------------
struct Press {
  unsigned from, to;  // frames (video::frames()), inclusive
  int player;
  int kind;  // 0 digital bit, 1 pressure button, 2 stick
  int index;
  int value;
};
std::vector<Press> g_script;

// "<frame> key.<name>": a key of the network page; "<frame> type.<text>": typed characters.
struct KeyPress {
  unsigned frame;
  unsigned vk;
};
std::vector<KeyPress> g_keys;
size_t g_keys_done = 0;

bool parse_key(unsigned frame, const std::string& t) {
  if (t.compare(0, 5, "type.") == 0) {
    for (size_t i = 5; i < t.size(); ++i) g_keys.push_back({frame, 0x100u + static_cast<unsigned char>(t[i])});
    return true;
  }
  if (t.compare(0, 4, "key.") != 0) return false;
  std::string k = t.substr(4);
  static const struct {
    const char* name;
    unsigned vk;
  } names[] = {{"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT},
               {"enter", VK_RETURN}, {"esc", VK_ESCAPE}, {"back", VK_BACK}};
  for (const auto& n : names)
    if (k == n.name) {
      g_keys.push_back({frame, n.vk});
      return true;
    }
  if (k.size() >= 2 && k[0] == 'f') {
    int n = atoi(k.c_str() + 1);
    if (n >= 1 && n <= 12) {
      g_keys.push_back({frame, static_cast<unsigned>(VK_F1 + n - 1)});
      return true;
    }
  }
  return false;
}

constexpr int kMouseButtons = 5;  // left, right, middle, side 1, side 2: the game's mouse controls 9 ... 13
constexpr int kMouseFirst = 9;
const char* const kMouseNames[kMouseButtons] = {"left", "right", "middle", "side1", "side2"};

bool parse_token(const std::string& t, Press& p) {
  static const char* digital[] = {"up", "down", "left", "right", "start", "back", "l3", "r3"};
  static const char* pressure[] = {"a", "b", "x", "y", "black", "white", "lt", "rt"};
  static const char* sticks[] = {"lx", "ly", "rx", "ry"};
  if (t.compare(0, 6, "mouse.") == 0) {  // a button of the mouse held, as if the game's window were in front
    for (int i = 0; i < kMouseButtons; ++i)
      if (t.compare(6, std::string::npos, kMouseNames[i]) == 0) {
        p.kind = 3, p.index = i, p.value = 1;
        return true;
      }
    return false;
  }
  for (int i = 0; i < 8; ++i)
    if (t == digital[i]) {
      p.kind = 0, p.index = i, p.value = 1;
      return true;
    }
  for (int i = 0; i < 8; ++i)
    if (t == pressure[i]) {
      p.kind = 1, p.index = i, p.value = 255;
      return true;
    }
  for (int i = 0; i < 4; ++i)
    if (t.size() == 3 && t.compare(0, 2, sticks[i]) == 0 && (t[2] == '+' || t[2] == '-')) {
      p.kind = 2, p.index = i, p.value = t[2] == '+' ? 32767 : -32768;
      return true;
    }
  return false;
}

// Lines of "<frame>[-<frame>] [p2.]<control>"; '#' starts a comment. A single frame holds for 6 frames.
void load_script(const std::wstring& path) {
  FILE* f = _wfopen(path.c_str(), L"r");
  if (!f) {
    LOGW("debug: script %ls not found", path.c_str());
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char* hash = strchr(line, '#');
    if (hash) *hash = 0;
    unsigned a = 0, b = 0;
    char tok[64] = {};
    int n = sscanf(line, "%u-%u %63s", &a, &b, tok);
    if (n != 3) {
      n = sscanf(line, "%u %63s", &a, tok);
      if (n != 2) continue;
      b = a + 5;
    }
    Press p{a, b, 0, 0, 0, 0};
    std::string t = tok;
    for (char& c : t)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    // "menu.": only in a game that is not in a session (the network page); "game.": only in one that is
    bool in_session = g_cfg.net_mode != Config::Net::Off;
    if (t.compare(0, 5, "menu.") == 0) {
      if (in_session) continue;
      t.erase(0, 5);
    } else if (t.compare(0, 5, "game.") == 0) {
      if (!in_session) continue;
      t.erase(0, 5);
    }
    if (t.compare(0, 3, "p2.") == 0) {
      p.player = 1;
      t.erase(0, 3);
    } else if (t.compare(0, 3, "p1.") == 0) {
      t.erase(0, 3);
    }
    if (parse_key(a, t)) continue;
    if (parse_token(t, p))
      g_script.push_back(p);
    else
      LOGW("debug: script: unknown control '%s'", tok);
  }
  fclose(f);
  LOGI("debug: script %ls: %u presses, %u keys of the network page", path.c_str(), (unsigned)g_script.size(),
       (unsigned)g_keys.size());
}

void apply_script(int player, Pad& pad) {
  unsigned f = video::frames();
  for (const Press& p : g_script) {
    if (p.player != player || f < p.from || f > p.to) continue;
    if (p.kind == 0)
      pad.buttons |= static_cast<uint8_t>(1u << p.index);
    else if (p.kind == 1)
      pad.analog[p.index] = static_cast<uint8_t>(p.value);
    else if (p.kind == 2)
      (p.index == 0 ? pad.lx : p.index == 1 ? pad.ly : p.index == 2 ? pad.rx : pad.ry) = static_cast<int16_t>(p.value);
  }
}

// --- the mouse's buttons as controls ------------------------------------------------------------
// The game binds a control to a key, a button or an axis of the mouse, or a pad's (three numbers an
// action in its registry key: game.h). For the mouse it asks one function for the control's value -
// and that function knows the left, the right and the middle button only: the game's own names for the
// side buttons lead nowhere. Besides, in a network game the mod gives the game no mouse at all (its
// movement is this PC's alone). So a button's value is told here: as Windows holds the button, while the
// game's window is in front. It goes into the pad of the player whose control it is - in a network game
// before that pad is sent, so both games get the same.
// Not in the main menu's scene: there a click chooses what is under the game's cursor.
int g_local_role = 0;  // (in a network game: the player this PC plays)

bool mouse_button(int button) {
  uint32_t scene = sim::scene();
  if (!scene || scene == 0x4D4E5553) return false;  // 'MNUS'
  unsigned f = video::frames();
  for (const Press& p : g_script)  // (a lab script's "mouse.left", of this PC's player: the game's window is never in front there)
    if (p.kind == 3 && p.index == button && p.player == g_local_role && f >= p.from && f <= p.to) return true;
  static const int vk[kMouseButtons] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
  HWND wnd = video::window();
  return wnd && GetForegroundWindow() == wnd && (GetAsyncKeyState(vk[button]) & 0x8000) != 0;
}

// float __thiscall (int control, int player) of the port's input manager: a mouse control's value now.
using MouseControl_t = float(__thiscall*)(void* self, int control, int player);
MouseControl_t oMouseControl = nullptr;

float __fastcall hkMouseControl(void* self, void* /*edx*/, int control, int player) {
  if (control >= kMouseFirst && control < kMouseFirst + kMouseButtons) {
    bool held = mouse_button(control - kMouseFirst);
    if (tlog::enabled(tlog::Debug)) {
      static unsigned shown = 0;
      unsigned bit = 1u << (control - kMouseFirst), now = held ? shown | bit : shown & ~bit;
      if (now != shown) {
        shown = now;
        LOGD("input: mouse buttons held %02X", now);
      }
    }
    return held ? 1.0f : 0.0f;
  }
  return oMouseControl(self, control, player);
}

// --- [debug] bind: numbers of the game's registry key told from the ini ---------------------------
// The lab's copies of the game share the player's own registry key with the player's game: a test of a
// binding must not write there. "bind = p0\PADX_1=0x20009, ..." tells the game these numbers instead.
using RegNumber_t = int(__thiscall*)(void* self, const char* name, int fallback);  // (ret 8: a method of the game's settings object)
RegNumber_t oRegNumber = nullptr;
std::vector<std::pair<std::string, int>> g_binds;

int __fastcall hkRegNumber(void* self, void* /*edx*/, const char* name, int fallback) {
  if (name)
    for (const auto& b : g_binds)
      if (_stricmp(b.first.c_str(), name) == 0) return b.second;
  return oRegNumber(self, name, fallback);
}
// --- the game's "gamepad state of player <handle - 1>" -----------------------------
using GetState_t = int(__cdecl*)(int handle, GameState* state);
GetState_t oGetState = nullptr;

int __cdecl hkGetState(int handle, GameState* state) {
  int r = oGetState(handle, state);
  int player = handle - 1;
  if (player < 0 || player > 1 || !state) return r;
  Pad pad;
  if (netplay::pad(player, pad)) {
    // a network game: both PCs are told the same pad for this frame
    to_game(pad, *state);
  } else if (netpage::active()) {
    // the network page is up: the keys are the page's
    to_game(pad, *state);
  } else {
    pad = from_game(*state);
    if (!g_script.empty()) {
      apply_script(player, pad);
      to_game(pad, *state);
    }
  }
  if (tlog::enabled(tlog::Debug)) {
    static Pad shown[2];
    if (!(pad == shown[player])) {
      shown[player] = pad;
      LOGD("input: P%d buttons %02X pressure %02X %02X %02X %02X %02X %02X %02X %02X sticks %d %d %d %d", player + 1,
           pad.buttons, pad.analog[0], pad.analog[1], pad.analog[2], pad.analog[3], pad.analog[4], pad.analog[5],
           pad.analog[6], pad.analog[7], pad.lx, pad.ly, pad.rx, pad.ry);
    }
  }
  return r;
}

}  // namespace

// --- DirectInput: the mouse in a network game ----------------------------------------------
// The game moves its menu cursor by the mouse's DirectInput movement, every frame. In a network
// game that movement is this PC's alone: the mouse device reports none.
using GetDeviceState_t = HRESULT(WINAPI*)(void* device, DWORD size, void* data);
using CreateDevice_t = HRESULT(WINAPI*)(void* dinput, const GUID& guid, void** device, void* outer);
using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, const IID&, void**, void*);
GetDeviceState_t oGetDeviceState = nullptr;
CreateDevice_t oCreateDevice = nullptr;
DirectInput8Create_t oDirectInput8Create = nullptr;

HRESULT WINAPI hkGetDeviceState(void* device, DWORD size, void* data) {
  HRESULT hr = oGetDeviceState(device, size, data);
  // 16 / 20 bytes: DIMOUSESTATE / DIMOUSESTATE2 (the keyboard asks for 256)
  if (SUCCEEDED(hr) && data && (size == 16 || size == 20) && (g_cfg.net_mode != Config::Net::Off || netpage::active()))
    memset(data, 0, size);
  return hr;
}

HRESULT WINAPI hkCreateDevice(void* dinput, const GUID& guid, void** device, void* outer) {
  HRESULT hr = oCreateDevice(dinput, guid, device, outer);
  if (SUCCEEDED(hr) && device && *device && !oGetDeviceState) {
    void** vt = *reinterpret_cast<void***>(*device);
    patch::hook("IDirectInputDevice8::GetDeviceState", reinterpret_cast<uintptr_t>(vt[9]), (void*)&hkGetDeviceState,
                (void**)&oGetDeviceState);
  }
  return hr;
}

HRESULT WINAPI hkDirectInput8Create(HINSTANCE inst, DWORD version, const IID& iid, void** out, void* outer) {
  HRESULT hr = oDirectInput8Create(inst, version, iid, out, outer);
  if (SUCCEEDED(hr) && out && *out && !oCreateDevice) {
    void** vt = *reinterpret_cast<void***>(*out);
    patch::hook("IDirectInput8::CreateDevice", reinterpret_cast<uintptr_t>(vt[3]), (void*)&hkCreateDevice,
                (void**)&oCreateDevice);
  }
  return hr;
}

bool Pad::operator==(const Pad& o) const {
  return buttons == o.buttons && memcmp(analog, o.analog, 8) == 0 && lx == o.lx && ly == o.ly && rx == o.rx && ry == o.ry;
}

bool script_key(unsigned& vk) {
  if (g_keys_done >= g_keys.size() || g_keys[g_keys_done].frame > video::frames()) return false;
  vk = g_keys[g_keys_done++].vk;
  return true;
}

Pad read_local(int role) {
  g_local_role = role;
  Pad pad;
  if (oGetState) {
    GameState st{};
    oGetState(1, &st);  // this PC's "player 1" controls
    pad = from_game(st);
  }
  if (!g_script.empty()) apply_script(role, pad);
  return pad;
}

void install_devices(const Config& cfg) {
  g_cfg = cfg;
  // (the game loads <system>\dinput8.dll by its full path; loaded here first, it is the same module)
  patch::hook_api("dinput8.dll", "DirectInput8Create", (void*)&hkDirectInput8Create, (void**)&oDirectInput8Create);
}

void install(const Config& cfg) {
  g_cfg = cfg;
  if (!cfg.script.empty()) {
    std::wstring name(cfg.script.begin(), cfg.script.end());
    load_script(module_dir() + name);
  }
  static const uint8_t mouse_head[] = {0x83, 0xEC, 0x08, 0xDB, 0x81, 0xC0, 0x04, 0x00, 0x00};  // sub esp,8; fild dword [ecx+0x4C0]
  if (patch::bytes_are(game::kMouseControl, mouse_head, sizeof(mouse_head)))
    patch::hook("a mouse control's value", game::kMouseControl, (void*)&hkMouseControl, (void**)&oMouseControl);
  else
    LOGE("input: the mouse control function is not where it should be; the mouse's buttons are left to the game");
  // [debug] bind = p0\PADX_1=0x20009, p0\PADB_1=0x2000A
  for (size_t i = 0; i < cfg.binds.size();) {
    size_t j = cfg.binds.find(',', i);
    if (j == std::string::npos) j = cfg.binds.size();
    std::string one = cfg.binds.substr(i, j - i);
    size_t eq = one.find('=');
    if (eq != std::string::npos) {
      std::string name = one.substr(0, eq);
      while (!name.empty() && name.front() == ' ') name.erase(0, 1);
      while (!name.empty() && name.back() == ' ') name.pop_back();
      g_binds.push_back({name, static_cast<int>(strtoul(one.c_str() + eq + 1, nullptr, 0))});
    }
    i = j + 1;
  }
  if (!g_binds.empty()) {
    static const uint8_t reg_head[] = {0x83, 0xEC, 0x08, 0x8D, 0x04, 0x24, 0x50};  // sub esp,8; lea eax,[esp]; push eax
    if (patch::bytes_are(game::kRegNumber, reg_head, sizeof(reg_head)) &&
        patch::hook("a number of the game's registry key", game::kRegNumber, (void*)&hkRegNumber, (void**)&oRegNumber))
      LOGI("debug: %u numbers of the game's registry key are told from the ini (bind)", (unsigned)g_binds.size());
  }
  static const uint8_t head[] = {0x8B, 0x0D, 0x2C, 0xF3, 0x7F, 0x00, 0x53, 0x55, 0x56, 0x57};  // mov ecx,[0x7FF32C]; push ebx ...
  if (!patch::bytes_are(game::kPadGetState, head, sizeof(head))) {
    LOGE("input: the pad state function is not where it should be; controls are left to the game");
    return;
  }
  patch::hook("pad state of a player", game::kPadGetState, (void*)&hkGetState, (void**)&oGetState);
}

}  // namespace input
