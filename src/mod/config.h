// in2net.ini, read once at start-up. Sections: [log], [video], [debug].
// Unknown keys are ignored; a missing file means defaults.
#pragma once
#include <string>
#include <vector>

#include "common/log.h"

struct Config {
  // [log]
  tlog::Level log_level = tlog::Info;

  // [video]
  // mode = game: the game's own exclusive full screen.
  //        borderless: a window without a frame over the whole monitor (no mode change, Alt+Tab at once).
  //        window: an ordinary window.
  enum class Mode { Game, Borderless, Window };
  Mode mode = Mode::Borderless;
  // resolution: the size of the frame the game renders. auto (0 x 0): the monitor's in borderless mode, the
  // window's in window mode; game (-1): what the game's own options say.
  int res_w = 0, res_h = 0;
  // aspect = wide: the frame has the shape of the monitor (or window), the view is wider at the sides,
  // as tall as in 4:3. aspect = 4:3: the game's own 4:3 picture, black bars at the sides.
  bool wide = true;
  // own_camera = 1: in a network game the camera follows this PC's hero (0: the game's one camera for both).
  bool own_camera = true;
  // Size of the window's client area in window mode; 0 = the size of the game's frame.
  int window_w = 0, window_h = 0;
  // background = 1: the game goes on while its window is not in front (0: it waits, as the game does).
  bool background = false;
  // vsync = 1 | 0; -1 = as the game asks.
  int vsync = -1;
  // A better picture than the game's own (none of it is the game's logic):
  // antialiasing = 0 | 2 | 4 | 8: smoothed edges (MSAA), when the frame fills the window;
  // anisotropy = 0 | 2 | 4 | 8 | 16: textures at an angle (floors, walls) stay sharp;
  // sharpen, colors = 0..100: the finished frame made crisper, its colours fuller (postfx).
  int antialiasing = 4;
  int anisotropy = 16;
  int sharpen = 35;
  int colors = 0;

  // [net]
  // mode = off | host | join. host: wait for the second player on `port`; join: go to `connect`.
  enum class Net { Off, Host, Join };
  Net net_mode = Net::Off;
  int net_port = 47820;
  std::string net_connect = "127.0.0.1";  // host[:port]
  std::string net_name = "player";
  int net_delay = 2;    // frames between reading a pad and the frame that uses it
  int net_wait_s = 120; // how long to wait for the other player at the start
  // transport = auto: Steam when it runs, and the address (ENet) besides; steam | enet: only that one;
  // mock: the lab's stand-in for Steam (two players on one PC, no account).
  std::string net_transport = "auto";
  std::string net_lobby = "friends";  // the host's Steam lobby: friends | public
  // A game started again into a session from the network page (netplay::prepare, IN2NET_* variables):
  // how the two meet ("steam": by SteamID, "enet": by address), whom the host lets in.
  std::string net_via;
  unsigned long long net_peer = 0;
  bool net_restarted = false;

  // [debug] - for the lab only; never set in a player's ini.
  // net_loss = N: N percent of this game's unreliable packets are dropped; net_outage = <at>,<for>:
  // all of them from <at> seconds into the session, for <for> seconds.
  int net_loss = 0;
  unsigned net_outage_at_ms = 0, net_outage_ms = 0;
  // net_stall = <at>,<for>: this game's own thread stands still for <for> seconds, <at> seconds
  // into the session (a level that loads for long, a window being dragged).
  unsigned net_stall_at_ms = 0, net_stall_ms = 0;
  // net_quit = N: this game leaves the session N seconds into it (as if its player had closed the game's session).
  unsigned net_quit_ms = 0;
  // The lab's Steam (transport = mock): this copy's slot (its fake SteamID), the folder the copies
  // share, loss and latency of its messages.
  int mock_slot = 1;
  std::string mock_dir;
  int mock_loss_pct = 0;
  int mock_latency_ms = 0;
  int mock_send_rate = 0;
  bool mock_lobby = true;
  int mock_member_delay_ms = 0;
  // multi = 1: more than one copy of the game may run at once.
  bool multi = false;
  // dump = window: write the unpacked image (in2net-dump.bin / .txt) when the game is
  // about to make its window, then end the process: no window is ever shown.
  std::string dump = "off";
  // boot_trace = 1: log the first calls of the start-up APIs that come from the exe.
  bool boot_trace = false;
  // autoquit = N: end the process N seconds after the mod started (0 = never).
  int autoquit_s = 0;
  // guard = 1: end the process instead of letting it change the display mode or make a
  // full-screen device (lab runs that must never reach the desktop).
  bool guard = false;
  // no_activate = 1: the game's window is shown without taking the focus, never on top.
  bool no_activate = false;
  // offscreen = 1: the window is placed outside the desktop.
  bool offscreen = false;
  // shots = 60, 600, ...: frames (counted at Present) saved to screenshots\in2net_f<frame>.bmp.
  std::vector<unsigned> shots;
  // script = <file next to the exe>: presses by frame, lines of "<frame>[-<frame>] [p2.]<control>".
  std::string script;
  // save_dir = <name>: the game's saves go to this folder next to the exe (a lab copy's own).
  std::string save_dir;
  // fast = 1: frames are not held to 60 a second (the game's clock is not real time anyway).
  bool fast = false;
  // sim_log = 1: one line per game frame in in2net-sim.log (to compare two runs).
  bool sim_log = false;
  // rng_trace = <from>-<to>: for these game frames every draw of a random number goes to in2net-rng.log.
  unsigned rng_trace_from = 0, rng_trace_to = 0;
  // rng_calls = 1: the caller of every random draw is kept and written to in2net-rngcalls.bin at autoquit.
  bool rng_calls = false;
  // sound_clock = 0: sounds end by the sound card's real time, as in the unmodified game (to compare).
  bool sound_clock = true;
  // mute = 1: the copy's sound is muted in Windows' mixer (lab runs).
  bool mute = false;
  // no_sound = 1: the game gets no DirectSound (silent).
  bool no_sound = false;
  // draw_log_all = 1: with im2d_log, every 3D draw call of that frame too, with its call chain.
  bool draw_log_all = false;
  // own_camera = 1 | 2: the own camera of player 1 / 2 without a network game.
  int own_camera_test = 0;
  // hdr = 0 | 1, shadows = 0 | 1: the game's display options forced (two lab copies share one registry).
  int force_hdr = -1, force_shadows = -1;
  // camera_log = N: every Nth update of the main camera its place, aim and the heroes' places go to the log.
  unsigned camera_log = 0;
  // im2d_log = N: every 2D draw call of game frame N goes to the log.
  unsigned im2d_log = 0;
  // notice = N: a test notice is on the screen for the first N seconds.
  int notice_test = 0;
  // postfx_split = 1: sharpening and colours on the right half of the frame only (to compare).
  bool postfx_split = false;
  // ui_log = 1: the menu objects' events go to the log (the first few hundred).
  bool ui_log = false;
  // ui_font = N: the network page's frame of words in the game's font N, filled with every letter (to see the fonts).
  int ui_font = -1;
  // shot_every = N: every Nth frame saved as well (0 = off).
  unsigned shot_every = 0;
};

// Folder of the exe, with a trailing backslash.
std::wstring module_dir();

// Returns false if the file is missing (defaults are used).
bool load_config(const std::wstring& path, Config& cfg);
