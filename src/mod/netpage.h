// СЕТЕВАЯ ИГРА: the network page, opened from the main menu's seventh button. The page and the
// button are the game's own objects (gameui): the game draws them and walks them. What the rows
// say and what choosing one does is here; what happens between the two players is netlobby's.
//
// Game thread only (the frame gate and the window's messages are both the game's thread).
#pragma once

#include "mod/config.h"

namespace netpage {

void install(const Config& cfg);

// Once a game frame (the frame gate).
void tick();

// The start is given and the game is about to start again: the game is told no key, no button, no mouse.
bool active();

// The window's WM_KEYDOWN / WM_CHAR (left and right pick a friend, typing fills the address row).
// True: the page took it, the game does not see it.
bool on_key(unsigned vk);
bool on_char(wchar_t ch);

}  // namespace netpage
