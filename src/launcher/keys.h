// The keys of the launcher's УПРАВЛЕНИЕ page. They are the game's own: incrotu.exe keeps what its ОПЦИИ
// screen sets in the registry, HKEY_CURRENT_USER\Software\THQ\the incredibles rise of the underminer,
// under p0 (player 1) and p1 (player 2): three numbers for an action, <ACTION>_0 .. _2 (the first key,
// a second one, a third), each (device << 16) | code - device 1 the keyboard (code: the key's DirectInput
// number, 0xFF none), 2 the mouse, 3 and on the pads; a number that is not there means the default of
// data\Inc2OptionsMenu_win.xml. The page sets the first number of each action - its key - and the second:
// a button of the mouse the action may have besides (the mouse's codes: 9 left, 10 right, 11 middle, 12 and
// 13 the side ones, which only the mod makes work). The game reads them at its start.
#pragma once
#include <string>

namespace lz {
namespace keys {

constexpr int kPlayers = 2;
constexpr int kActions = 21;  // (the last of the first group, the screenshot, is player 1's alone)
constexpr int kGroups = 3;
constexpr int kMouseButtons = 5;  // left, right, middle, side 1, side 2

void use_root(const std::wstring& subkey);  // tests: another key of HKEY_CURRENT_USER (--keys)
const std::wstring& root();                 // the game's key of HKEY_CURRENT_USER (its sound levels are in it too)
void load();         // what the registry has now
void load_canned();  // for a picture: the game's defaults; nothing is read or written

const wchar_t* group_name(int group);
int group(int action);
const wchar_t* label(int action);
const char* id(int action);          // its name for the lab: forward, jump, ...
bool has(int player, int action);
bool fixed(int player, int action);  // the game keeps this one to itself (player 1's pause is Esc)
int code(int player, int action);    // the key's DirectInput number; 0: none; -1: not a key (a mouse's or a pad's button)
std::wstring shown(int player, int action);  // what the cell says
std::wstring name(int code);
bool allowed(int code);              // the game's key table has it

// Gives the action the key and writes it. The action that had the key (of either player: the keyboard
// is one) takes this one's former key. False: not written, error() says why.
bool set(int player, int action, int code);
bool reset(int player);  // the game's defaults for that player (no button of the mouse)

// The button of the mouse an action has besides its key: 0 ... kMouseButtons - 1; -1: none.
int mouse(int player, int action);
const wchar_t* mouse_name(int button);  // "ЛКМ", "ПКМ", ...
// Gives the action the button and writes it (whoever had it, of either player, loses it: the mouse is one).
// The button the action has already: taken off it. False: not written, error() says why.
bool set_mouse(int player, int action, int button);
const std::wstring& error();
// After set() or reset(): the actions that were left without a key by it ("Игрок 1: Толчок"), empty if none.
const std::wstring& freed();

}  // namespace keys
}  // namespace lz
