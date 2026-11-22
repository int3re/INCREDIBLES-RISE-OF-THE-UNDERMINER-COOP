// The camera's view.
//
// The game keeps the horizontal angle of its camera and takes the vertical one from the
// frame's shape: on a wide frame the picture is the 4:3 one with its top and bottom cut off.
// Here the vertical angle stays what it is in 4:3 and the view grows at the sides instead.
//
// The view is also part of the game's logic (what is in view is kept alive, effects in view
// draw random numbers), so in a network game it cannot differ between two PCs. The mod keeps
// two views: one for the logic, the same on every PC (16:9), and this PC's own, put into the
// camera only while a frame is drawn.
#pragma once

#include "mod/config.h"

namespace camera {

// After the game's code is unpacked (boot's ready callback).
void install(const Config& cfg);

}  // namespace camera
