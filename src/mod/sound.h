// Sound on the game's clock.
//
// The engine learns that a sound has ended by asking DirectSound (GetStatus,
// GetCurrentPosition) - real time of the sound card, never the same frame on
// two PCs - and that changes what the game does next: a voice is free or not,
// a random variation is drawn or not, a line of dialogue lets the next event
// go. Here the game's own DirectSound buffers answer those two questions from
// the game's clock (sim.h): a buffer "plays" for as many game milliseconds as
// its length says. What is heard stays DirectSound's business.
#pragma once

#include "mod/config.h"

namespace sound {

// InitializeASI (hooks on dsound.dll only).
void install(const Config& cfg);

// Once a game frame (the frame gate): streams that drifted from the game's clock are put right.
void on_frame();

}  // namespace sound
