// The players' controls. The PC port is the Xbox game on a stand-in for the
// Xbox input library: once a frame, for every player, the engine asks one
// function (game::kPadGetState) for that player's gamepad state, which the
// port fills from the keyboard and DirectInput devices by the key bindings.
// The mod sits on that function: what the game is told for a player can be
// replaced (a lab script; later a network session's frame).
#pragma once
#include <cstdint>

#include "mod/config.h"

namespace input {

// A player's gamepad for one frame (the game's state record without its packet number).
struct Pad {
  enum : uint8_t { Up = 1, Down = 2, Left = 4, Right = 8, Start = 0x10, Back = 0x20, LThumb = 0x40, RThumb = 0x80 };
  enum { A, B, X, Y, Black, White, LT, RT };
  uint8_t buttons = 0;      // digital: the bits above
  uint8_t analog[8] = {};   // pressure buttons, 0..255, indexed by the enum above
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;  // sticks, -32768..32767
  bool operator==(const Pad& o) const;
};

// InitializeASI: hooks on dinput8.dll only.
void install_devices(const Config& cfg);

// After the game's code is unpacked (boot's ready callback).
void install(const Config& cfg);

// [debug] script: the next key of a "<frame> key.<name>" / "<frame> type.<text>" line that is due
// (a virtual-key code; 0x100 + a character for typed text). False: none now.
bool script_key(unsigned& vk);

// This PC's own controls now (the "player 1" keys and devices), plus the lab script's presses
// for player `role`: what this PC contributes to a network game.
Pad read_local(int role);

}  // namespace input
