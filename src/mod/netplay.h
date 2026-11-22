// The network game: two PCs, each runs its own copy of the game, and both
// copies are fed the same pads frame by frame (lockstep). Player 1 is the
// host's, player 2 the guest's; each plays with their own "player 1" controls.
//
// This first form of a session starts with the process: the two games meet
// before the game's own start-up (the guest takes the host's co-op saves), and
// play the same frames from the very first one.
#pragma once
#include <cstdint>

#include "mod/config.h"
#include "mod/input.h"

namespace netplay {

// Before anything else of the mod: a game started again from the network page (netlobby) is told by
// IN2NET_* variables what it is in the session; it also waits for the game before it to be gone.
// True: this is such a game (cfg now says host or join).
bool prepare(Config& cfg);

void install(const Config& cfg);

// Before the game's start-up (boot's watch, at the game's window): host waits for the
// guest, the guest finds the host; saves are passed over.
void meet();

// The frame gate, before the game reads the pads for `frame`: this PC's pad is sent, and
// the call returns when both players' pads for the frame are known.
void begin_frame(uint32_t frame);

// What the game is told for `player` (0 or 1) in the current frame. False: no session
// decides it (the game is played alone).
bool pad(int player, input::Pad& out);

// This game is closing (or leaves the session for another reason): the other player is told.
void leave(const char* why);

bool active();
// This PC's player in a session (0 host, 1 guest); 0 outside.
int me();

}  // namespace netplay
