// A word from the mod on the screen during play ("the second player has left"), written by the game:
// its own font, drawn by its own string drawing inside its own frame, with the drop shadow its
// own texts have. One or two lines at the top of the frame for a few seconds; nothing of the mod's
// own is drawn over the game.
//
// What is on the screen is this PC's only and no part of the game's state: in a session one
// player may see a notice the other does not.
#pragma once
#include <string>

#include "mod/config.h"

namespace notice {

void install(const Config& cfg);

// Any thread. Russian text ('\n' between two lines); it is put into the release's lettering here.
void say(const std::wstring& text, double seconds);

// Game thread, inside the game's frame while its camera is open (camera.cpp, before the main
// camera's update ends).
void draw();

}  // namespace notice
