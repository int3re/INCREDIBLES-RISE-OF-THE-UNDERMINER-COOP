// The game's keys: see keys.h.
#include "launcher/keys.h"

#include <windows.h>

#include <cstring>

#include "launcher/launcher.h"

namespace lz {
namespace keys {
namespace {

const wchar_t kRoot[] = L"Software\\THQ\\the incredibles rise of the underminer";
constexpr DWORD kKeyboard = 1, kNoKey = 0xFF;
constexpr DWORD kMouse = 2, kMouseFirst = 9;  // (the game's table at 0x704DE8: 9 MS_LEFT, 10 MS_RIGHT, 11 MS_MIDDLE, 12 MS_BTN4, 13 MS_BTN5)
const wchar_t* const kMouseNames[kMouseButtons] = {L"ЛКМ", L"ПКМ", L"СКМ", L"Бок.1", L"Бок.2"};

struct Action {
  const wchar_t* reg;    // the game's name of it
  const char* id;
  const wchar_t* label;  // as the game's ОПЦИИ screen calls it
  int group;
  int def[kPlayers];     // data\Inc2OptionsMenu_win.xml: defaultprimary (0: the player has no such action)
};
const Action kActionList[kActions] = {
    {L"THUMBLFORWARD", "forward", L"Вперёд", 0, {0x11, 0xC8}},
    {L"THUMBLBACK", "back", L"Назад", 0, {0x1F, 0xD0}},
    {L"THUMBLLEFT", "left", L"Влево", 0, {0x1E, 0xCB}},
    {L"THUMBLRIGHT", "right", L"Вправо", 0, {0x20, 0xCD}},
    {L"PADA", "jump", L"Прыжок", 0, {0x39, 0x52}},
    {L"PADSTART", "pause", L"Пауза", 0, {0x01, 0x9D}},
    {L"SCREENSHOT", "shot", L"Снимок экрана", 0, {0x3E, 0}},
    {L"PADX", "punch", L"Толчок", 1, {0x18, 0x50}},
    {L"PADB", "grab", L"Взять / мороз", 1, {0x17, 0x4F}},
    {L"PADY", "action", L"Действие", 1, {0x19, 0x51}},
    {L"PADLEFTTRIGGER", "super_l", L"Суперудар левый", 1, {0x25, 0x47}},
    {L"PADRIGHTTRIGGER", "super_r", L"Суперудар правый", 1, {0x26, 0x49}},
    {L"PADBACK", "upgrade", L"Улучшить", 1, {0x2A, 0x0E}},
    {L"THUMBRFORWARD", "dodge_forward", L"Уклон вперёд", 2, {0x09, 0x48}},
    {L"THUMBRBACK", "dodge_back", L"Уклон назад", 2, {0x0A, 0x4C}},
    {L"THUMBRLEFT", "dodge_left", L"Уклон влево", 2, {0x08, 0x4B}},
    {L"THUMBRRIGHT", "dodge_right", L"Уклон вправо", 2, {0x0B, 0x4D}},
    {L"DPAD_UP", "ai_swap", L"Смена ИИ", 2, {0x02, 0xC7}},
    {L"DPAD_DOWN", "ai_guard", L"Охрана ИИ", 2, {0x03, 0xCF}},
    {L"DPAD_LEFT", "ai_attack", L"Агрессия ИИ", 2, {0x04, 0xD3}},
    {L"DPAD_RIGHT", "ai_follow", L"ИИ следовать", 2, {0x05, 0xD1}},
};
const wchar_t* kGroupNames[kGroups] = {L"ДВИЖЕНИЕ", L"БОЙ", L"УКЛОН И НАПАРНИК"};

// The keys the game knows (its table at 0x704788), by their DirectInput numbers.
struct KeyName {
  int code;
  const wchar_t* name;
};
const KeyName kKeyNames[] = {
    {0x1E, L"A"}, {0x30, L"B"}, {0x2E, L"C"}, {0x20, L"D"}, {0x12, L"E"}, {0x21, L"F"}, {0x22, L"G"}, {0x23, L"H"},
    {0x17, L"I"}, {0x24, L"J"}, {0x25, L"K"}, {0x26, L"L"}, {0x32, L"M"}, {0x31, L"N"}, {0x18, L"O"}, {0x19, L"P"},
    {0x10, L"Q"}, {0x13, L"R"}, {0x1F, L"S"}, {0x14, L"T"}, {0x16, L"U"}, {0x2F, L"V"}, {0x11, L"W"}, {0x2D, L"X"},
    {0x15, L"Y"}, {0x2C, L"Z"}, {0x0B, L"0"}, {0x02, L"1"}, {0x03, L"2"}, {0x04, L"3"}, {0x05, L"4"}, {0x06, L"5"},
    {0x07, L"6"}, {0x08, L"7"}, {0x09, L"8"}, {0x0A, L"9"}, {0x3B, L"F1"}, {0x3C, L"F2"}, {0x3D, L"F3"}, {0x3E, L"F4"},
    {0x3F, L"F5"}, {0x40, L"F6"}, {0x41, L"F7"}, {0x42, L"F8"}, {0x43, L"F9"}, {0x44, L"F10"}, {0x57, L"F11"},
    {0x58, L"F12"}, {0x64, L"F13"}, {0x65, L"F14"}, {0x66, L"F15"}, {0x52, L"Цифр. 0"}, {0x4F, L"Цифр. 1"},
    {0x50, L"Цифр. 2"}, {0x51, L"Цифр. 3"}, {0x4B, L"Цифр. 4"}, {0x4C, L"Цифр. 5"}, {0x4D, L"Цифр. 6"}, {0x47, L"Цифр. 7"},
    {0x48, L"Цифр. 8"}, {0x49, L"Цифр. 9"}, {0x45, L"Num Lock"}, {0xB5, L"Цифр. /"}, {0x37, L"Цифр. *"},
    {0x4A, L"Цифр. −"}, {0x4E, L"Цифр. +"}, {0x9C, L"Цифр. Enter"}, {0x53, L"Цифр. ,"}, {0x46, L"Scroll Lock"},
    {0xC5, L"Pause"}, {0xD2, L"Insert"}, {0xD3, L"Delete"}, {0xC7, L"Home"}, {0xCF, L"End"}, {0xC9, L"Page Up"},
    {0xD1, L"Page Down"}, {0xC8, L"Стрелка вверх"}, {0xD0, L"Стрелка вниз"}, {0xCB, L"Стрелка влево"},
    {0xCD, L"Стрелка вправо"}, {0x01, L"Esc"}, {0x0F, L"Tab"}, {0x3A, L"Caps Lock"}, {0x2A, L"Левый Shift"},
    {0x1D, L"Левый Ctrl"}, {0x38, L"Левый Alt"}, {0x0C, L"−"}, {0x0D, L"="}, {0x0E, L"Backspace"}, {0x1C, L"Enter"},
    {0x1A, L"["}, {0x1B, L"]"}, {0x27, L";"}, {0x2B, L"\\"}, {0x33, L","}, {0x34, L"."}, {0x35, L"/"}, {0x39, L"Пробел"},
    {0x36, L"Правый Shift"}, {0x9D, L"Правый Ctrl"}, {0xB8, L"Правый Alt"},
};

struct State {
  std::wstring root = kRoot;
  bool canned = false, kept = false;
  DWORD first[kPlayers][kActions] = {};   // <ACTION>_0 as the game sees it
  DWORD second[kPlayers][kActions] = {};  // <ACTION>_1: none, a button of the mouse, or what the game's own screen put there
  std::wstring error, freed;
};
State g;

DWORD of_key(int code) { return (kKeyboard << 16) | static_cast<DWORD>(code ? code : kNoKey); }
DWORD default_of(int player, int action) { return of_key(kActionList[action].def[player]); }

std::wstring player_key(int player) { return g.root + L"\\p" + std::to_wstring(player); }
std::wstring value_name(int action, int slot) { return std::wstring(kActionList[action].reg) + L"_" + std::to_wstring(slot); }

bool read(int player, const std::wstring& name, DWORD& out) {
  DWORD type = 0, size = sizeof(DWORD), v = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, player_key(player).c_str(), name.c_str(), RRF_RT_REG_DWORD, &type, &v, &size) != ERROR_SUCCESS)
    return false;
  out = v;
  return true;
}

bool write(int player, const std::wstring& name, DWORD v) {
  if (g.canned) return true;
  HKEY key = nullptr;
  LONG e = RegCreateKeyExW(HKEY_CURRENT_USER, player_key(player).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
  if (e == ERROR_SUCCESS) {
    e = RegSetValueExW(key, name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(key);
  }
  if (e == ERROR_SUCCESS) return true;
  g.error = L"Клавиша не записалась в настройки игры (ошибка " + std::to_wstring(e) + L").";
  return false;
}

// The keys as they were before this run's first change, put aside as text.
void keep() {
  if (g.kept || g.canned) return;
  g.kept = true;
  std::string text = "The game's keys before the launcher changed them: [" + core::narrow(g.root) + "]\r\n";
  for (int p = 0; p < kPlayers; ++p)
    for (int a = 0; a < kActions; ++a)
      for (int slot = 0; slot < 3; ++slot) {
        DWORD v = 0;
        if (!read(p, value_name(a, slot), v)) continue;
        char line[96];
        sprintf_s(line, "p%d\\%s=0x%lx\r\n", p, core::narrow(value_name(a, slot)).c_str(), v);
        text += line;
      }
  HANDLE f = CreateFileW(core::join(core::downloads_dir(), L"keys.before-launcher.txt").c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD done = 0;
  WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &done, nullptr);
  CloseHandle(f);
}

// The key a number means: 0 none, -1 something that is not a key.
int key_of(DWORD v) {
  const DWORD device = (v >> 16) & 0xFF, c = v & 0xFF;
  if (device != kKeyboard) return -1;
  return c == kNoKey ? 0 : static_cast<int>(c);
}

bool put(int player, int action, int code) {
  const DWORD v = of_key(code);
  if (!write(player, value_name(action, 0), v)) return false;
  g.first[player][action] = v;
  if (!code) g.freed += (g.freed.empty() ? L"" : L", ") + (L"игрок " + std::to_wstring(player + 1)) + L": " + kActionList[action].label;
  return true;
}

// The button of the mouse a number means: -1 if it is not one.
int mouse_of(DWORD v) {
  const DWORD device = (v >> 16) & 0xFF, c = v & 0xFF;
  return device == kMouse && c >= kMouseFirst && c < kMouseFirst + kMouseButtons ? static_cast<int>(c - kMouseFirst) : -1;
}

bool put_second(int player, int action, DWORD v) {
  if (!write(player, value_name(action, 1), v)) return false;
  g.second[player][action] = v;
  return true;
}

}  // namespace

void use_root(const std::wstring& subkey) { g.root = subkey; }
const std::wstring& root() { return g.root; }

void load() {
  g.canned = false;
  g.error.clear();
  for (int p = 0; p < kPlayers; ++p)
    for (int a = 0; a < kActions; ++a) {
      DWORD v = 0;
      const bool there = read(p, value_name(a, 0), v);
      const DWORD device = (v >> 16) & 0xFF;
      // (the game takes a number only if its device is one it knows: else the default stays)
      g.first[p][a] = there && v != 0xFFFFFFFF && device >= 1 && device < 13 ? v : default_of(p, a);
      DWORD second = 0;
      g.second[p][a] = read(p, value_name(a, 1), second) ? second : of_key(0);
    }
}

void load_canned() {
  g.canned = true;
  g.error.clear();
  for (int p = 0; p < kPlayers; ++p)
    for (int a = 0; a < kActions; ++a) {
      g.first[p][a] = default_of(p, a);
      g.second[p][a] = of_key(0);
    }
  // (the picture shows what the page is for: player 1's blows on the mouse)
  static const struct {
    const char* id;
    int button;
  } shown[] = {{"punch", 0}, {"grab", 1}, {"super_l", 3}, {"super_r", 4}};
  for (const auto& s : shown)
    for (int a = 0; a < kActions; ++a)
      if (strcmp(kActionList[a].id, s.id) == 0) g.second[0][a] = (kMouse << 16) | (kMouseFirst + static_cast<DWORD>(s.button));
}

const wchar_t* group_name(int group) { return kGroupNames[group]; }
int group(int action) { return kActionList[action].group; }
const wchar_t* label(int action) { return kActionList[action].label; }
const char* id(int action) { return kActionList[action].id; }
bool has(int player, int action) { return kActionList[action].def[player] != 0; }
bool fixed(int player, int action) { return player == 0 && kActionList[action].def[0] == 0x01; }
int code(int player, int action) { return key_of(g.first[player][action]); }

std::wstring name(int code) {
  for (const KeyName& k : kKeyNames)
    if (k.code == code) return k.name;
  return L"";
}

bool allowed(int code) { return code > 0 && !name(code).empty(); }

std::wstring key_shown(int player, int action) {
  const DWORD v = g.first[player][action];
  const int c = key_of(v);
  if (c > 0) {
    const std::wstring n = name(c);
    return n.empty() ? L"Клавиша " + std::to_wstring(c) : n;
  }
  if (c == 0) return L"—";
  return ((v >> 16) & 0xFF) == 2 ? L"Мышь" : L"Геймпад";
}

// What a cell says: the key, and the mouse's button after it if the action has one ("O / ЛКМ").
std::wstring shown(int player, int action) {
  const std::wstring key = key_shown(player, action);
  const int button = mouse(player, action);
  if (button < 0) return key;
  return code(player, action) == 0 ? std::wstring(kMouseNames[button]) : key + L" / " + kMouseNames[button];
}

int mouse(int player, int action) { return mouse_of(g.second[player][action]); }
const wchar_t* mouse_name(int button) { return button >= 0 && button < kMouseButtons ? kMouseNames[button] : L""; }

bool set_mouse(int player, int action, int button) {
  g.error.clear();
  g.freed.clear();
  if (!has(player, action) || fixed(player, action) || button < 0 || button >= kMouseButtons) {
    g.error = L"Этому действию кнопку мыши не дать.";
    return false;
  }
  keep();
  const bool had = mouse(player, action) == button;
  for (int p = 0; p < kPlayers; ++p)  // whoever has the button loses it (this action too, if it is pressed again)
    for (int a = 0; a < kActions; ++a)
      if (has(p, a) && mouse(p, a) == button && !put_second(p, a, of_key(0))) return false;
  if (had) return true;
  return put_second(player, action, (kMouse << 16) | (kMouseFirst + static_cast<DWORD>(button)));
}

bool set(int player, int action, int code) {
  g.error.clear();
  g.freed.clear();
  if (!has(player, action) || fixed(player, action)) {
    g.error = L"Эту клавишу игра не отдаёт.";
    return false;
  }
  if (!allowed(code)) {
    g.error = L"Такой клавиши игра не знает.";
    return false;
  }
  if (code == 0x01) {  // (the game's own: the pause of player 1, "back" in its menus)
    g.error = L"Esc — пауза и «назад» в меню: игра его не отдаёт.";
    return false;
  }
  keep();
  const int was = key_of(g.first[player][action]);
  if (was == code) return true;
  // the key's former owners: the first takes this action's former key, a second (two had it) is left without
  bool given = false;
  for (int p = 0; p < kPlayers; ++p)
    for (int a = 0; a < kActions; ++a) {
      if ((p == player && a == action) || !has(p, a)) continue;
      if (key_of(g.first[p][a]) == code) {
        if (!put(p, a, !given && was > 0 ? was : 0)) return false;
        given = true;
      }
      // (a second key of the game's own screen that is the same one: none)
      if (key_of(g.second[p][a]) == code && !put_second(p, a, of_key(0))) return false;
    }
  return put(player, action, code);
}

bool reset(int player) {
  g.error.clear();
  g.freed.clear();
  keep();
  for (int a = 0; a < kActions; ++a) {
    if (!has(player, a)) continue;
    const int def = kActionList[a].def[player];
    // (the other player's action on that key is left without one: the keyboard is one for both)
    const int other = 1 - player;
    for (int b = 0; b < kActions; ++b)
      if (has(other, b) && key_of(g.first[other][b]) == def && !put(other, b, 0)) return false;
    if (!put(player, a, def)) return false;
    if (g.second[player][a] != of_key(0) && !put_second(player, a, of_key(0))) return false;  // (the game's default: no second one)
  }
  return true;
}

const std::wstring& error() { return g.error; }
const std::wstring& freed() { return g.freed; }

}  // namespace keys
}  // namespace lz
