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

bool parse_token(const std::string& t, Press& p) {
  static const char* digital[] = {"up", "down", "left", "right", "start", "back", "l3", "r3"};
  static const char* pressure[] = {"a", "b", "x", "y", "black", "white", "lt", "rt"};
  static const char* sticks[] = {"lx", "ly", "rx", "ry"};
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
    else
      (p.index == 0 ? pad.lx : p.index == 1 ? pad.ly : p.index == 2 ? pad.rx : pad.ry) = static_cast<int16_t>(p.value);
  }
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
  static const uint8_t head[] = {0x8B, 0x0D, 0x2C, 0xF3, 0x7F, 0x00, 0x53, 0x55, 0x56, 0x57};  // mov ecx,[0x7FF32C]; push ebx ...
  if (!patch::bytes_are(game::kPadGetState, head, sizeof(head))) {
    LOGE("input: the pad state function is not where it should be; controls are left to the game");
    return;
  }
  patch::hook("pad state of a player", game::kPadGetState, (void*)&hkGetState, (void**)&oGetState);
}

}  // namespace input
