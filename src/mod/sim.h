// The game's time, made the same on every PC.
//
// The engine steps its world by the real time a frame took (timeGetTime), and
// paces frames with GetTickCount + Sleep. For a network game every PC must
// step the same world by the same amount, so the mod gives the game a clock of
// its own: it advances by exactly 1/60 s per game frame (a frame = one pass of
// the engine's "read all pads" at the top of its loops) and by nothing else,
// and the mod itself keeps the frames at 60 a second.
#pragma once
#include <cstdint>

#include "mod/config.h"

namespace sim {

// After the game's code is unpacked (boot's ready callback).
void install(const Config& cfg);

// Game frames so far (passes of the frame gate).
uint32_t frame();

// Before the process is ended by the lab's autoquit: the debug logs are written out.
void flush();

// True while the game's own settings screen runs (its loop is outside the game's frames).
bool in_settings_screen();

// A picture is about to be shown (the device's Present). Pictures of a loop that passes no frame gate
// - the game's settings screen - are held to 60 a second here.
void on_present();

// The scene the game is in, as its four letters ('MNUS' = 0x4D4E5553: the title and the main menu); 0 = none.
uint32_t scene();

// The game's clock, milliseconds.
uint64_t now_ms();

}  // namespace sim
