// Facts about incrotu.exe (The Incredibles: Rise of the Underminer, PC, 2005).
// Every address here is for the one build named by kExeSha256 (the project's own notes on the game tell how each was found).
#pragma once
#include <cstddef>
#include <cstdint>

namespace game {

// SHA-256 of incrotu.exe on disk (the packed file, 4 429 824 bytes).
inline constexpr uint8_t kExeSha256[32] = {
    0x64, 0x0A, 0xA7, 0x31, 0x6D, 0x36, 0x21, 0x8D, 0x4B, 0xD0, 0x81, 0x73, 0xCB, 0xA7, 0xC5, 0xBF,
    0x79, 0xB3, 0x74, 0xC8, 0xC7, 0xD9, 0x6A, 0x06, 0x91, 0x28, 0xBE, 0x29, 0x6C, 0x70, 0xF3, 0x20};

// The image: base 0x400000. The exe on disk is packed, the game's own code and
// data appear in these ranges while the start-up stub runs.
inline constexpr uintptr_t kImageBase = 0x00400000;
inline constexpr uintptr_t kTextBegin = 0x00401000;  // .text   (the game's code)
inline constexpr uintptr_t kTextEnd = 0x0069A000;
inline constexpr uintptr_t kRdataBegin = 0x0069B000;  // .rdata
inline constexpr uintptr_t kRdataEnd = 0x006D7000;
inline constexpr uintptr_t kDataBegin = 0x006D7000;  // .data + bss
inline constexpr uintptr_t kDataEnd = 0x00802000;

// --- window ---------------------------------------------------------
inline constexpr uintptr_t kWndProc = 0x00439260;    // the game's window procedure
inline constexpr uintptr_t kAppActive = 0x007FF2A9;  // byte: the window is in front (set by WM_ACTIVATE)
inline constexpr uintptr_t kAppQuit = 0x007FF2A8;    // byte: the game is closing
inline constexpr uintptr_t kHwnd = 0x007FF29C;       // HWND of the game's window

// void __thiscall (long* width, long* height) (ret 8): the frame's size, read from the registry value
// listbox_displayres ("640x480" when there is none).
inline constexpr uintptr_t kGetDisplayRes = 0x00674F10;

// char[0x104]: the folder of the saves; every save path is made from it.
inline constexpr uintptr_t kSaveDir = 0x00729420;
inline constexpr size_t kSaveDirSize = 0x104;

// --- camera ---------------------------------------------------------
// void __cdecl (RwCamera*, float fov_degrees): view window x = tan(fov / 2), y = x * height / width
// (the frame's size from kFrameWidth / kFrameHeight): the horizontal angle is fixed.
inline constexpr uintptr_t kCameraSetFov = 0x00514080;
inline constexpr uintptr_t kRwCameraSetViewWindow = 0x005D4500;  // RwCamera* __cdecl (RwCamera*, const RwV2d*)
inline constexpr uintptr_t kRwCameraBeginUpdate = 0x005D4180;    // RwCamera* __cdecl (RwCamera*)
inline constexpr uintptr_t kRwCameraEndUpdate = 0x005D4160;      // RwCamera* __cdecl (RwCamera*)
inline constexpr uintptr_t kMainCamera = 0x00744660;             // RwCamera*: the game's own camera
inline constexpr uintptr_t kFrameWidth = 0x006DBC18;             // int: the frame's size as the game knows it
inline constexpr uintptr_t kFrameHeight = 0x006DBC1C;

// RenderWare's engine instance (pointer) and its immediate-mode 2D calls in it.
inline constexpr uintptr_t kRwEngineInstance = 0x007F9BA0;
inline constexpr uintptr_t kRwIm2DRenderPrimitive = 0x30;         // int (type, verts, count)
inline constexpr uintptr_t kRwIm2DRenderIndexedPrimitive = 0x34;  // int (type, verts, count, indices, index_count)

// void __cdecl (model*, const float rect[4], const vec3* from, const vec3* to): draws a 3D model
// into a rectangle of the screen (x, y, width, height as fractions of the frame): the 3D parts of
// the interface (the fill of the health bars).
inline constexpr uintptr_t kModelRender2D = 0x0053EB80;

// The game's globals (pointer) and the two players in them: entity -> +0x4C -> matrix, position at +0x30.
inline constexpr uintptr_t kGlobalsPtr = 0x006DBC24;
inline constexpr uintptr_t kGlobalsPlayers = 0x4C8;  // entity*[2]
inline constexpr uintptr_t kEntMatrix = 0x4C;
// The two entities are the two heroes in a fixed order; which player plays which differs by level
// (ScenePlayerMapping in in2.ini): entity +0x1E4 -> its controller, controller +0x10 = pad number.
inline constexpr uintptr_t kEntController = 0x1E4;
inline constexpr uintptr_t kControllerPad = 0x10;
// entity +0x1D4: flags; the camera follows a hero when bit 17 is set, or bit 19 is clear and
// [kCameraAllHeroes] is not 0 - and bit 20 is clear either way (the game's 0x4E16D0).
inline constexpr uintptr_t kEntFlags = 0x1D4;
inline constexpr uintptr_t kCameraAllHeroes = 0x00729D58;
inline constexpr uintptr_t kMatrixPos = 0x30;
// The point the camera is aimed at, after smoothing (vec3): the middle between the two heroes
// in a game for two, the hero in a game for one. kCameraTargetRaw: the same before smoothing.
inline constexpr uintptr_t kCameraFocus = 0x00729D4C;
inline constexpr uintptr_t kCameraFocus2 = 0x00729D40;
inline constexpr uintptr_t kCameraTargetRaw = 0x00736080;

// --- the game's own settings screen and display options -------------------
// void __cdecl (const char* xml): runs the settings screen (ОПЦИИ) to its end in a loop of its
// own, inside one game frame, with the mouse and the keyboard read directly.
inline constexpr uintptr_t kOptionsScreen = 0x005A5DC0;
inline constexpr uintptr_t kDistanceClipping = 0x007F64F2;  // byte: "distance clipping" of the display options
inline constexpr uintptr_t kHdr = 0x006F6629;               // byte: HDR on
inline constexpr uintptr_t kShadowsOff = 0x0078834C;        // byte: shadows off

// --- input --------------------------------------------------------
// int __cdecl (int handle, state*): the port's stand-in for the Xbox XInputGetState;
// handle = player + 1, state = 0x20 bytes (packet, buttons, 8 pressure buttons, 4 sticks).
inline constexpr uintptr_t kPadGetState = 0x0067D120;
// What a control is bound to: three numbers an action in the game's registry key, "p<player>\<ACTION>_<0..2>",
// each (device << 16) | code - device 1 the keyboard (a DirectInput key number), 2 the mouse, 3 ... 12 pads.
// The mouse's codes (the game's name table at 0x704DE8): 0 ... 8 the wheel and the axes, 9 left, 10 right,
// 11 middle, 12 ... 16 "button 4" ... "button 8".
// float __thiscall (int code, int player) (ret 8) of the input manager: a mouse control's value now. It knows
// the buttons 9, 10 and 11 only (and, called for any control, moves the menus' cursor by the mouse's movement).
inline constexpr uintptr_t kMouseControl = 0x0067DAE0;
// int __thiscall (const char* name, int fallback) (ret 8) of the game's settings object: a number of the game's
// registry key (the bindings, the sound
// levels "slider_soundvolume" / "slider_musicvolume", the display options).
inline constexpr uintptr_t kRegNumber = 0x00674850;

// The engine's pads: mPad[4], 0x138 bytes each; +0x2C buttons held, +0x30 pressed, +0x34 released,
// +0x38 sticks (4 signed bytes), +0x3C state, +0x44 port (short).
inline constexpr uintptr_t kPads = 0x007664C8;
inline constexpr uintptr_t kPadSize = 0x138;
inline constexpr uintptr_t kPadOn = 0x2C;
// void __cdecl (): updates all four pads and the players' pad assignment; called once at the
// top of every pass of the game loop and of the menu loop. The mod's frame gate.
inline constexpr uintptr_t kInputUpdate = 0x0043C360;

// --- time -------------------------------------------------------------
inline constexpr uintptr_t kTimeGet = 0x0059B660;      // u64 __cdecl (): timeGetTime() in ms
inline constexpr uintptr_t kTimeDiffSec = 0x0059B690;  // float __cdecl (u64 t0, u64 t1)
inline constexpr uintptr_t kFrameLimiter = 0x00424A30; // void __cdecl (): 17 ms a frame by GetTickCount + Sleep
inline constexpr uintptr_t kLoopLimiterJle = 0x00428FC3;  // the same limiter inside the game loop: jle over its Sleep
inline constexpr uintptr_t kTimeElapsed = 0x0071C458;  // float: the frame's step, seconds
inline constexpr uintptr_t kIatGetTickCount = 0x0069B06C;  // the game's import slot
// The port's input code (0x67DFD0) puts the mouse cursor back into the middle of the game's window every
// frame through this slot - also while the window is not in front, and for a window that is off the
// screen the middle is the desktop's top left corner.
constexpr uintptr_t kIatSetCursorPos = 0x69B258;

// --- assets and objects (the engine is Heavy Iron's: packs of assets, objects made from them) ---
// int count(u32 type); void* by_type(u32 type, int index, u32* size); void* by_id(u32 id, u32* size).
// Types are four letters as a number ('DYNA' = 0x44594E41), ids are hashes of the assets' names.
inline constexpr uintptr_t kAssetCountByType = 0x00558F20;
inline constexpr uintptr_t kAssetFindByType = 0x00558FC0;  // (its second instruction is one of the protection's splices)
inline constexpr uintptr_t kAssetFind = 0x00558E50;
// void __thiscall send_links(xBase* this, xBase* from, u32 event): every link of the object whose source
// event is `event` is sent on (ret 8). An object: +4 id, +8 type, +9 link count, +0xA flags, +0xC links,
// +0x10 its event function, +0x14 its asset.
inline constexpr uintptr_t kSendLinks = 0x004E6430;
// void event(xBase* from, u32 from_event, xBase* to, u32 to_event, const float* params, void* widget,
//            u32 widget_id, int force) - cdecl.
inline constexpr uintptr_t kEntEvent = 0x00524D80;
// The scene's objects by id: a table of {id, object} pairs, open addressing; its size (a power of two).
inline constexpr uintptr_t kObjectTable = 0x0072A4C0;
inline constexpr uintptr_t kObjectTableSize = 0x006DD3C8;

// --- the engine's own string drawing (its "xfont") ---
// A font record, 0x30 bytes: id, letter width and height (parts of the frame's width / height), extra
// space, colour (bytes r g b a), shadow colour, the shadow's shift in pixels (x, y), clip rect (x, y, w, h).
// start(font*) picks the font's texture; render(text, size, x, y) is a member (this = font*, ret 0x10) and
// only fills the engine's vertex buffer; stop() draws it. Must run inside a camera's update.
inline constexpr uintptr_t kFontStart = 0x00528720;
inline constexpr uintptr_t kFontRender = 0x0052A5D0;
inline constexpr uintptr_t kFontStop = 0x005284A0;
// The fonts' tables, 0x1A1C bytes each: +0 the font's asset (0: not loaded), +8 character -> glyph (0xFF:
// none), glyph widths as floats from index 0x2C7, four floats a glyph.
inline constexpr uintptr_t kFontTables = 0x0074A248;
inline constexpr uintptr_t kFontTableSize = 0x1A1C;
inline constexpr uintptr_t kAsyncLoading = 0x00729C00;  // int: loading screen on a second thread (ini AsyncLoadingScreen)
// void __cdecl (float progress, int, colour*): draws "Loading... %" when the loading is not on a second
// thread; a console leftover - on the PC its font is never made and it crashes inside RenderWare.
inline constexpr uintptr_t kSyncLoadingDraw = 0x00425160;

// --- state --------------------------------------------------------------
inline constexpr uintptr_t kScenePtr = 0x00729C30;  // -> the current scene's id ('MNUS', 'UT01' ...)
// Random numbers: MT19937, seeded once at start with the key {0x123, 0x234, 0x345, 0x456}.
inline constexpr uintptr_t kRngState = 0x00778828;  // 624 dwords
inline constexpr uintptr_t kRngNext = 0x00778820;   // -> the next word
inline constexpr uintptr_t kRngLeft = 0x006E9ED8;   // words left before the state is renewed
// The generator in one piece but for kRngLeft: the pointer, the state and the "seeded" flag behind it.
inline constexpr uintptr_t kRngBlock = 0x00778820;
inline constexpr size_t kRngBlockSize = 0x9CC;
inline constexpr uintptr_t kRngSeed = 0x0046C480;
inline constexpr uintptr_t kRandFloat = 0x00546680;  // float (): 0..1; every draw goes through this one
inline constexpr uintptr_t kRandInt = 0x00546630;    // u32 (): or this one   // void __cdecl (): seeds it with the key above

}  // namespace game
