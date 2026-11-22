// Where the game keeps its saves. The game builds every save path from one
// buffer (game::kSaveDir: Documents\My Games\THQ\...\Save), filled in WinMain
// before the window is made. A lab copy gets a folder of its own next to its
// exe, so test runs never touch the player's saves.
#pragma once

#include "mod/config.h"

namespace saves {

void install(const Config& cfg);

// From boot's watch, when the game is about to make its window (the buffer is filled by then).
void on_window();

}  // namespace saves
