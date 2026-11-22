#include "mod/notice.h"
#include "mod/camera.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <unordered_map>

#include "common/log.h"
#include "mod/game.h"
#include "mod/netplay.h"
#include "mod/patch.h"
#include "mod/sim.h"

namespace camera {
namespace {

Config g_cfg;

struct V2 {
  float x, y;
};

// RwCamera fields.
constexpr uintptr_t kSync = 0x10;         // RwCamera* (*)(RwCamera*): rebuilds the view matrix and the frustum
constexpr uintptr_t kViewWindow = 0x68;   // RwV2d, then its reciprocal at +0x70

using SetViewWindow_t = void*(__cdecl*)(void* camera, const V2* view_window);
using SetFov_t = void(__cdecl*)(void* camera, float fov);
using CameraFn_t = void*(__cdecl*)(void* camera);
SetFov_t oSetFov = nullptr;
CameraFn_t oBeginUpdate = nullptr;
CameraFn_t oEndUpdate = nullptr;

// tan(fov / 2) of every camera the game has given a view angle.
std::unordered_map<void*, float> g_tan;

float frame_aspect() {
  int w = patch::read<int>(game::kFrameWidth), h = patch::read<int>(game::kFrameHeight);
  return w > 0 && h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 4.0f / 3.0f;
}

// The view the game's logic works with, on every PC: the 4:3 view's height, a 16:9 width. What
// is in view decides what the game keeps alive and how many random numbers its effects draw,
// so two PCs with different screens must not differ here.
V2 logic_view(float t) { return {0.75f * t * (16.0f / 9.0f), 0.75f * t}; }

// The view this PC draws: its own frame's shape. Wider than 4:3 (and the mod's wide mode on):
// the 4:3 view's height, the frame's width. Otherwise the game's own: x = tan, y = x * h / w.
V2 render_view(float t) {
  float aspect = frame_aspect();
  if (g_cfg.wide && g_cfg.mode != Config::Mode::Game && aspect > 4.0f / 3.0f + 0.01f) return {0.75f * t * aspect, 0.75f * t};
  return {t, t / aspect};
}

// The view window written straight into the camera and its frustum rebuilt at once (RenderWare's
// own setter only marks the camera for a later rebuild).
void apply(void* cam, const V2& vw) {
  auto p = static_cast<uint8_t*>(cam);
  float f[4] = {vw.x, vw.y, 1.0f / vw.x, 1.0f / vw.y};
  memcpy(p + kViewWindow, f, sizeof(f));
  auto sync = patch::read<CameraFn_t>(reinterpret_cast<uintptr_t>(p) + kSync);
  if (sync) sync(cam);
}

void __cdecl hkSetFov(void* cam, float fov) {
  float t = static_cast<float>(std::tan(static_cast<double>(fov) * 0.5 * 3.14159265358979323846 / 180.0));
  g_tan[cam] = t;
  V2 vw = logic_view(t);
  reinterpret_cast<SetViewWindow_t>(game::kRwCameraSetViewWindow)(cam, &vw);
}

// --- drawing: this PC's own view, and nothing of it left behind ---------------------------
// Random numbers drawn while a frame is drawn (and what is drawn depends on the view) must not
// move the game's own sequence: the generator is put back after the camera's update.
struct Rng {
  uint8_t block[game::kRngBlockSize];
  uint32_t left;
};
Rng g_rng;
int g_depth = 0;

struct V3 {
  float x, y, z;
};

uintptr_t hero(int i) {
  auto globals = patch::read<uintptr_t>(game::kGlobalsPtr);
  return globals ? patch::read<uintptr_t>(globals + game::kGlobalsPlayers + 4 * i) : 0;
}

bool player_pos(int i, V3& out) {
  auto ent = hero(i);
  if (!ent) return false;
  auto mat = patch::read<uintptr_t>(ent + game::kEntMatrix);
  if (!mat) return false;
  out = patch::read<V3>(mat + game::kMatrixPos);
  return true;
}

// The pad (player number) that plays hero i; -1 when it has no controller.
int hero_pad(int i) {
  auto ent = hero(i);
  auto ctrl = ent ? patch::read<uintptr_t>(ent + game::kEntController) : 0;
  return ctrl ? patch::read<int>(ctrl + game::kControllerPad) : -1;
}

// The game's own test of whether its camera follows hero i now (not while he is out of the game).
bool hero_followed(int i) {
  auto ent = hero(i);
  if (!ent) return false;
  uint32_t flags = patch::read<uint32_t>(ent + game::kEntFlags);
  if (!(flags & (1u << 17)) && ((flags & (1u << 19)) || patch::read<uint32_t>(game::kCameraAllHeroes) == 0)) return false;
  return !(flags & (1u << 20));
}

void log_geometry(void* cam) {
  static unsigned n = 0;
  if (!g_cfg.camera_log || cam != patch::read<void*>(game::kMainCamera) || ++n % g_cfg.camera_log) return;
  auto frame = patch::read<uintptr_t>(reinterpret_cast<uintptr_t>(cam) + 4);
  if (!frame) return;
  V3 pos = patch::read<V3>(frame + 0x50 + 0x30), at = patch::read<V3>(frame + 0x50 + 0x20);
  V3 mpos = patch::read<V3>(frame + 0x10 + 0x30);
  V3 f = patch::read<V3>(game::kCameraFocus), f2 = patch::read<V3>(game::kCameraFocus2), raw = patch::read<V3>(game::kCameraTargetRaw);
  V3 p0{}, p1{};
  bool h0 = player_pos(0, p0), h1 = player_pos(1, p1);
  LOGI("camera: heroes' pads %d %d, followed %d %d", hero_pad(0), hero_pad(1), hero_followed(0), hero_followed(1));
  LOGI("camera: pos %.1f %.1f %.1f (modelling %.1f %.1f %.1f) at %.2f %.2f %.2f | focus %.1f %.1f %.1f | focus2 %.1f %.1f %.1f | raw %.1f %.1f %.1f | P1 %s %.1f %.1f %.1f | P2 %s %.1f %.1f %.1f",
       pos.x, pos.y, pos.z, mpos.x, mpos.y, mpos.z, at.x, at.y, at.z, f.x, f.y, f.z, f2.x, f2.y, f2.z, raw.x, raw.y, raw.z,
       h0 ? "" : "-", p0.x, p0.y, p0.z, h1 ? "" : "-", p1.x, p1.y, p1.z);
}

// --- this player's own camera ------------------------------------------------------------------
// In a game for two the game's camera is aimed at the middle between the heroes and backs away
// as they part. On this PC only one of them is played: while a frame is drawn the camera stays
// where the game put it (a place the game knows to be free of walls), but is turned to this
// PC's hero and its view narrowed until the hero is as large as when the two stand together -
// a cameraman with a zoom lens. The game's own camera - the one its logic uses - is put back
// afterwards.
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(V3 a) { return std::sqrt(dot(a, a)); }

// RwFrame: the modelling matrix and the world matrix; a matrix's rows.
constexpr uintptr_t kFrameModelling = 0x10, kFrameLtm = 0x50, kMatRight = 0x00, kMatUp = 0x10, kMatAt = 0x20, kMatPos = 0x30;

struct Basis {
  V3 right, up, at;
};

Basis read_basis(uintptr_t matrix) {
  return {patch::read<V3>(matrix + kMatRight), patch::read<V3>(matrix + kMatUp), patch::read<V3>(matrix + kMatAt)};
}

void write_basis(uintptr_t matrix, const Basis& b) {
  memcpy(reinterpret_cast<void*>(matrix + kMatRight), &b.right, sizeof(V3));
  memcpy(reinterpret_cast<void*>(matrix + kMatUp), &b.up, sizeof(V3));
  memcpy(reinterpret_cast<void*>(matrix + kMatAt), &b.at, sizeof(V3));
}

int own_player() {
  if (g_cfg.own_camera_test > 0) return g_cfg.own_camera_test - 1;  // the lab: without a session
  if (!g_cfg.own_camera || !netplay::active()) return -1;
  return netplay::me();
}

struct OwnView {
  uint32_t frame = 0xFFFFFFFF;  // the game frame it was worked out for
  bool on = false;
  Basis basis{};                // the camera's axes while this PC's frame is drawn
  float zoom = 1.0f;            // its view window's factor (below 1: nearer)
  float weight = 0.0f;          // 0 the game's camera .. 1 this player's
  V3 look{};                    // smoothed: the point of this PC's hero the camera looks at
  float base = 0.0f;            // the camera's distance from its aim while the heroes stand together (0: not seen yet)
  bool turned = false;          // the camera's frame holds `basis` now
  Basis saved_modelling{}, saved_ltm{};
} g_own;

void work_out_own_view(uintptr_t frame) {
  g_own.on = false;
  // this PC's hero: the one this PC's pad plays in this level
  int player = own_player(), me = -1;
  if (player >= 0) me = hero_pad(0) == player ? 0 : hero_pad(1) == player ? 1 : -1;
  V3 p[2];
  // (with one hero out of the game the game's camera is on the other alone: nothing to change)
  bool both = player_pos(0, p[0]) && player_pos(1, p[1]) && hero_followed(0) && hero_followed(1);
  V3 cam = patch::read<V3>(frame + kFrameLtm + kMatPos);
  Basis was = read_basis(frame + kFrameLtm);
  bool follow = false;
  V3 aim{};
  float dist = 0.0f, apart = 0.0f;
  if (me >= 0 && both) {
    V3 middle = (p[0] + p[1]) * 0.5f;
    V3 to_middle = middle - cam;
    float md = len(to_middle);
    apart = len(p[0] - p[1]);
    // the game's camera is on the heroes (not on a scene of its own): the middle is in front of it, not far
    follow = md > 0.5f && md < 80.0f && dot(to_middle, was.at) / md > 0.85f;
    // what the camera is aimed at: the point of its axis at the heroes' height (the middle itself
    // is a little off the axis); the middle when the axis does not come down to that height
    aim = middle;
    if (was.at.y < -0.05f) {
      float t = (middle.y - cam.y) / was.at.y;
      if (t > 1.0f && t < 120.0f) aim = cam + was.at * t;
    }
    dist = len(aim - cam);
  }
  if (follow) {
    if (g_own.base <= 0.0f)
      g_own.base = dist;
    else if (apart < 6.0f)
      g_own.base += (dist - g_own.base) * 0.05f;
    else if (dist < g_own.base)
      g_own.base = dist;
  }
  bool fresh = g_own.weight <= 0.0f;
  g_own.weight += ((follow ? 1.0f : 0.0f) - g_own.weight) * 0.08f;
  if (g_own.weight < 0.002f) {
    g_own.weight = 0.0f;
    return;
  }
  if (follow) {
    V3 hero = p[me];
    hero.y += 1.0f;  // his middle, not his feet
    g_own.look = fresh ? hero : g_own.look + (hero - g_own.look) * 0.25f;
  }
  V3 target = aim + (g_own.look - aim) * g_own.weight;
  V3 to = target - cam;
  float d = len(to);
  if (d < 0.5f) return;
  V3 at = to * (1.0f / d);
  // the camera's own handedness, and no roll: right = +-(up x at) with the world's up
  float hand = dot(was.right, cross(was.up, was.at)) >= 0.0f ? 1.0f : -1.0f;
  V3 right = cross(V3{0.0f, 1.0f, 0.0f}, at) * hand;
  float rl = len(right);
  if (rl < 0.05f) return;  // (looking straight down or up: leave the game's camera)
  right = right * (1.0f / rl);
  V3 up = cross(at, right) * hand;
  g_own.basis = {right, up, at};
  // nearer: the view narrowed by as much as the hero is farther than when the two are together
  float hero_dist = len(g_own.look - cam);
  float scale = g_own.base > 0.0f && hero_dist > g_own.base ? g_own.base / hero_dist : 1.0f;
  if (scale < 0.35f) scale = 0.35f;
  g_own.zoom = 1.0f + (scale - 1.0f) * g_own.weight;
  g_own.on = true;
}

void turn_camera(void* cam) {
  if (cam != patch::read<void*>(game::kMainCamera)) return;
  auto frame = patch::read<uintptr_t>(reinterpret_cast<uintptr_t>(cam) + 4);
  if (!frame) return;
  uint32_t now = sim::frame();
  if (g_own.frame != now) {
    g_own.frame = now;
    work_out_own_view(frame);
  }
  if (!g_own.on || g_own.turned) return;
  g_own.saved_modelling = read_basis(frame + kFrameModelling);
  g_own.saved_ltm = read_basis(frame + kFrameLtm);
  write_basis(frame + kFrameModelling, g_own.basis);
  write_basis(frame + kFrameLtm, g_own.basis);
  g_own.turned = true;
}

void put_camera_back(void* cam) {
  if (!g_own.turned || cam != patch::read<void*>(game::kMainCamera)) return;
  auto frame = patch::read<uintptr_t>(reinterpret_cast<uintptr_t>(cam) + 4);
  if (frame) {
    write_basis(frame + kFrameModelling, g_own.saved_modelling);
    write_basis(frame + kFrameLtm, g_own.saved_ltm);
  }
  g_own.turned = false;
}
void* __cdecl hkBeginUpdate(void* cam) {
  log_geometry(cam);
  auto it = g_tan.find(cam);
  if (it != g_tan.end()) {
    if (g_depth++ == 0) {
      memcpy(g_rng.block, reinterpret_cast<const void*>(game::kRngBlock), sizeof(g_rng.block));
      g_rng.left = patch::read<uint32_t>(game::kRngLeft);
    }
    turn_camera(cam);
    V2 vw = render_view(it->second);
    if (g_own.turned) vw = {vw.x * g_own.zoom, vw.y * g_own.zoom};
    apply(cam, vw);
  }
  return oBeginUpdate(cam);
}

void* __cdecl hkEndUpdate(void* cam) {
  // The mod's word on the screen is written by the game into its own frame, last of all.
  if (cam == patch::read<void*>(game::kMainCamera)) notice::draw();
  void* r = oEndUpdate(cam);
  auto it = g_tan.find(cam);
  if (it != g_tan.end()) {
    put_camera_back(cam);
    apply(cam, logic_view(it->second));
    if (g_depth > 0 && --g_depth == 0) {
      memcpy(reinterpret_cast<void*>(game::kRngBlock), g_rng.block, sizeof(g_rng.block));
      *reinterpret_cast<uint32_t*>(game::kRngLeft) = g_rng.left;
    }
  }
  return r;
}

}  // namespace

void install(const Config& cfg) {
  g_cfg = cfg;
  static const uint8_t fov_head[] = {0x83, 0xEC, 0x08, 0xD9, 0x44, 0x24, 0x10};  // sub esp, 8; fld [esp+0x10]
  static const uint8_t cam_head[] = {0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08};  // push ebp; mov ebp, esp; push ecx; mov eax, [ebp+8]
  if (!patch::bytes_are(game::kCameraSetFov, fov_head, sizeof(fov_head)) ||
      !patch::bytes_are(game::kRwCameraBeginUpdate, cam_head, sizeof(cam_head)) ||
      !patch::bytes_are(game::kRwCameraEndUpdate, cam_head, sizeof(cam_head))) {
    LOGE("camera: the game's camera functions are not where they should be; the view is left to the game");
    return;
  }
  patch::hook("view angle", game::kCameraSetFov, (void*)&hkSetFov, (void**)&oSetFov);
  patch::hook("RwCameraBeginUpdate", game::kRwCameraBeginUpdate, (void*)&hkBeginUpdate, (void**)&oBeginUpdate);
  patch::hook("RwCameraEndUpdate", game::kRwCameraEndUpdate, (void*)&hkEndUpdate, (void**)&oEndUpdate);
}

}  // namespace camera
