// The mod's menus as the game's own.
//
// The game's menus are data: every button is a pair of objects (a "ui:box" frame and a "ui:text"
// label) made at the scene's start from assets of the scene's pack, wired to each other by event
// links (focus, select, which label is above and below, which screen comes next). The mod adds its
// menus the same way: when the engine asks the menu scene's pack what it holds, it is handed more
// assets of the same kinds - copies of the game's own with other names, places and links. The
// engine then makes, draws, lights up and walks the mod's buttons with its own code, in its own
// font and colours:
//   - СЕТЕВАЯ ИГРА, a seventh button of the main menu (a copy of АВТОРЫ one row lower);
//   - the network page, a copy of the game's БОНУС page: six rows on the left, a frame with words
//     on the right, the page's name in the title frame, the same hints and the same way back (ESC).
// What the rows say and what choosing one does is the mod's (netpage).
//
// Only in a game that is not in a session: in a session both games must be made of the same objects.
#pragma once
#include <cstdint>
#include <string>

#include "mod/config.h"

namespace gameui {

constexpr int kRows = 6;

// After the game's code is unpacked (boot's ready callback).
void install(const Config& cfg);

// The network page's happenings (game thread, from inside the engine's own event handling).
struct Events {
  void (*shown)() = nullptr;         // the page has come up
  void (*left)() = nullptr;          // the page is left (back to the main menu)
  void (*chosen)(int row) = nullptr; // a row was chosen (ENTER, a click)
};
void on_events(const Events& ev);

// Once a game frame: sees the network page come and go (its frame on the right is shown and hidden
// with the page by the game itself) and tells Events.
void poll();
// True while the network page is up.
bool page_up();
// The row that has the focus (0..5), -1 when none.
int focused_row();

// Words in the release's lettering (see letters()). A row with no words is hidden.
void set_title(const std::string& s);
void set_row(int row, const std::string& s);
void set_info(const std::string& s);
// The focus goes to this row.
void focus_row(int row);

// Russian (and anything else) as the Russian release writes it: its font has Cyrillic shapes in
// the places of Latin capitals and of 3, 4, 6 ("СЕТЕВАЯ ИГРА" is stored as "CETEBAR NFPA").
// Latin letters are written small: the font's small letters are Latin.
std::string letters(const std::wstring& text);
std::string letters(const std::string& utf8);
// A player's name: in the game's second font, the one whose small letters are Latin (the menu's own
// font has none), by the engine's own text command.
std::string name(const std::string& utf8);

}  // namespace gameui
