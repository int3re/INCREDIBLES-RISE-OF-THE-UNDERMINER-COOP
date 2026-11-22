// The game's 2D layer (interface, text, menus) on a wide frame.
//
// Everything 2D is laid out for a 4:3 screen and scaled to the frame's width: on a wide frame
// it comes out stretched. All of it is drawn through RenderWare's two immediate-mode 2D calls;
// the mod stands in front of them and puts every quad that does not span the whole frame back
// into its 4:3 shape around the middle of the frame. Whole-frame quads (fades, backgrounds)
// stay whole.
#pragma once

#include "mod/config.h"

namespace hud {

void install(const Config& cfg);

// Once a game frame (the frame gate): takes the 2D calls over as soon as RenderWare has them.
void on_frame(unsigned frame);

// [debug]: true between the two passes of an interface element (its mask and its picture).
bool between_passes();

}  // namespace hud
