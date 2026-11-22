// Screen for incrotu.exe. The game knows only an exclusive full screen on top
// of everything. The mod makes its Direct3D device a windowed one and places
// the game's window itself: without a frame over the whole monitor
// (borderless) or as an ordinary window. RenderWare still believes in its
// full-screen mode; only the size of the frame matters to it.
//
// Also the lab's eyes: frames saved from the back buffer, never from the desktop.
#pragma once
#include <windows.h>

#include "mod/config.h"

struct IDirect3DDevice9;

namespace video {

// InitializeASI: the Direct3D hooks (system dlls only; no address of the game).
void install(const Config& cfg);

// After the game's code is unpacked: the game's own choice of the frame's size.
void install_game(const Config& cfg);

// From boot's watch on CreateWindowExA, for the game's own window: the style and place
// the mod wants. Returns false to leave the arguments alone.
bool window_args(DWORD& ex, DWORD& style, int& x, int& y, int& w, int& h);
// The game's window exists.
void window_made(HWND hwnd);
// ShowWindow of the game's window: the command to use instead.
int show_command(int cmd);

HWND window();
IDirect3DDevice9* device();
// The game is asked to close (as by its window's cross); if it has not gone in a few seconds, it is ended.
void request_quit();
// Frames presented so far.
unsigned frames();

}  // namespace video
