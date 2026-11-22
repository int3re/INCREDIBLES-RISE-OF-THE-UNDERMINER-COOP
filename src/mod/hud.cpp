#include "mod/hud.h"

#include <windows.h>

#include <d3d9.h>
#include <intrin.h>

#include <cmath>
#include <vector>

#include "common/log.h"
#include "mod/game.h"
#include "mod/patch.h"
#include "mod/video.h"

namespace hud {
namespace {

Config g_cfg;
unsigned g_frame = 0;
bool g_between = false;

// RwIm2DVertex of the Direct3D 9 driver.
struct Vertex {
  float x, y, z, rhw;
  uint32_t color;
  float u, v;
};
static_assert(sizeof(Vertex) == 28, "RwIm2DVertex is 28 bytes");

// rwPRIMTYPE: 1 line list, 2 polyline, 3 triangle list, 4 triangle strip, 5 triangle fan, 6 point list
using RenderPrimitive_t = int(__cdecl*)(int type, Vertex* verts, int count);
using RenderIndexed_t = int(__cdecl*)(int type, Vertex* verts, int count, uint16_t* indices, int index_count);
RenderPrimitive_t oPrimitive = nullptr;
RenderIndexed_t oIndexed = nullptr;

float frame_w() { return static_cast<float>(patch::read<int>(game::kFrameWidth)); }
float frame_h() { return static_cast<float>(patch::read<int>(game::kFrameHeight)); }

void log_call(const char* what, uintptr_t ra, int type, const Vertex* v, int n, int indices) {
  if (!g_cfg.im2d_log || g_frame != g_cfg.im2d_log) return;
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < n; ++i) {
    x0 = v[i].x < x0 ? v[i].x : x0;
    x1 = v[i].x > x1 ? v[i].x : x1;
    y0 = v[i].y < y0 ? v[i].y : y0;
    y1 = v[i].y > y1 ? v[i].y : y1;
  }
  LOGI("im2d: %s from 0x%08X type %d, %d vertices%s, x %.0f..%.0f y %.0f..%.0f", what, (unsigned)ra, type, n,
       indices ? " (indexed)" : "", x0, x1, y0, y1);
  if (n == 4)
    LOGI("im2d:    (%.1f %.1f uv %.3f %.3f) (%.1f %.1f uv %.3f %.3f) (%.1f %.1f uv %.3f %.3f) (%.1f %.1f uv %.3f %.3f) colour %08X",
         v[0].x, v[0].y, v[0].u, v[0].v, v[1].x, v[1].y, v[1].u, v[1].v, v[2].x, v[2].y, v[2].u, v[2].v, v[3].x, v[3].y,
         v[3].u, v[3].v, v[0].color);
}

// --- back into 4:3 ---------------------------------------------------------------------
// One shape (a quad, mostly): left as it is when it spans the frame's whole width (a
// background, a fade, the film bars) or lies wholly outside the frame (parked there: it must
// stay outside); otherwise its x goes through  x' = W/2 + (x - W/2) * k,  k = (4/3) / (W/H).
std::vector<Vertex> g_copy;
float g_k = 1.0f, g_w = 0.0f, g_h = 0.0f;

bool wide_frame() {
  if (!g_cfg.wide || g_cfg.mode == Config::Mode::Game) return false;
  g_w = frame_w();
  g_h = frame_h();
  if (g_w <= 0 || g_h <= 0 || g_w * 3.0f <= g_h * 4.0f + 1.0f) return false;
  g_k = (g_h * 4.0f / 3.0f) / g_w;
  return true;
}

void fit_shape(Vertex* v, int n) {
  float x0 = v[0].x, x1 = v[0].x, y0 = v[0].y, y1 = v[0].y;
  for (int i = 1; i < n; ++i) {
    x0 = v[i].x < x0 ? v[i].x : x0;
    x1 = v[i].x > x1 ? v[i].x : x1;
    y0 = v[i].y < y0 ? v[i].y : y0;
    y1 = v[i].y > y1 ? v[i].y : y1;
  }
  if (x0 <= 1.0f && x1 >= g_w - 1.0f) return;                           // the whole width
  if (x1 <= 0.0f || x0 >= g_w || y1 <= 0.0f || y0 >= g_h) return;       // outside the frame
  float c = g_w * 0.5f;
  for (int i = 0; i < n; ++i) v[i].x = c + (v[i].x - c) * g_k;
}

// The call's vertices, copied and fitted `per` at a time (0: all as one shape).
Vertex* fitted(const Vertex* verts, int count, int per) {
  g_copy.assign(verts, verts + count);
  if (per <= 0 || count % per != 0) per = count;
  for (int i = 0; i + per <= count; i += per) fit_shape(g_copy.data() + i, per);
  return g_copy.data();
}

int __cdecl hkPrimitive(int type, Vertex* verts, int count) {
  log_call("primitive", reinterpret_cast<uintptr_t>(_ReturnAddress()), type, verts, count, 0);
  if (count == 4 && verts) g_between = verts[0].color == 0x01FFFFFF;
  if (count <= 0 || !verts || !wide_frame()) return oPrimitive(type, verts, count);
  // a triangle list is a batch of quads (text: six vertices a glyph); a strip or fan is one shape
  return oPrimitive(type, fitted(verts, count, type == 3 ? 6 : 0), count);
}

int __cdecl hkIndexed(int type, Vertex* verts, int count, uint16_t* indices, int index_count) {
  log_call("indexed", reinterpret_cast<uintptr_t>(_ReturnAddress()), type, verts, count, index_count);
  if (count <= 0 || !verts || !wide_frame()) return oIndexed(type, verts, count, indices, index_count);
  // indexed quads: four vertices each when the numbers say so
  int per = (type == 3 && count % 4 == 0 && index_count == count / 4 * 6) ? 4 : 0;
  return oIndexed(type, fitted(verts, count, per), count, indices, index_count);
}

}  // namespace

// The interface's 3D parts: a model drawn into a rectangle given in fractions of the frame.
using ModelRender2D_t = void(__cdecl*)(void* model, const float* rect, const void* from, const void* to);
ModelRender2D_t oModelRender2D = nullptr;

void __cdecl hkModelRender2D(void* model, const float* rect, const void* from, const void* to) {
  if (!rect || !wide_frame()) {
    oModelRender2D(model, rect, from, to);
    return;
  }
  float r[4] = {rect[0], rect[1], rect[2], rect[3]};
  bool whole = r[0] <= 0.001f && r[0] + r[2] >= 0.999f;
  bool outside = r[0] + r[2] <= 0.0f || r[0] >= 1.0f;
  if (whole || outside) {
    oModelRender2D(model, r, from, to);
    return;
  }
  r[0] = 0.5f + (r[0] - 0.5f) * g_k;
  r[2] *= g_k;
  // These models are longer than what is seen of them (a bar's fill slides under its frame);
  // the rest used to be cut off by the edge of the 4:3 screen. The same edge, by a scissor.
  IDirect3DDevice9* dev = video::device();
  DWORD was = FALSE;
  RECT old{};
  if (dev) {
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &was);
    dev->GetScissorRect(&old);
    float half = g_h * 4.0f / 3.0f * 0.5f;
    RECT clip{static_cast<LONG>(g_w * 0.5f - half), 0, static_cast<LONG>(g_w * 0.5f + half + 0.5f), static_cast<LONG>(g_h)};
    dev->SetScissorRect(&clip);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
  }
  oModelRender2D(model, r, from, to);
  if (dev) {
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, was);
    dev->SetScissorRect(&old);
  }
}

bool between_passes() { return g_between; }

void on_frame(unsigned frame) {
  g_frame = frame;
  auto inst = patch::read<uint8_t*>(game::kRwEngineInstance);
  if (!inst) return;
  // The functions themselves are hooked, not the pointers to them: parts of the game keep the
  // addresses and call them directly.
  auto prim = patch::read<uintptr_t>(reinterpret_cast<uintptr_t>(inst) + game::kRwIm2DRenderPrimitive);
  auto indexed = patch::read<uintptr_t>(reinterpret_cast<uintptr_t>(inst) + game::kRwIm2DRenderIndexedPrimitive);
  if (prim && !oPrimitive && patch::in_image(prim, 16)) {
    LOGI("hud: RenderWare's 2D calls are at 0x%08X and 0x%08X", (unsigned)prim, (unsigned)indexed);
    patch::hook("RwIm2DRenderPrimitive", prim, (void*)&hkPrimitive, (void**)&oPrimitive);
  }
  if (indexed && !oIndexed && patch::in_image(indexed, 16))
    patch::hook("RwIm2DRenderIndexedPrimitive", indexed, (void*)&hkIndexed, (void**)&oIndexed);
}

void install(const Config& cfg) {
  g_cfg = cfg;
  if (!cfg.wide || cfg.mode == Config::Mode::Game) return;
  static const uint8_t head[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0x54, 0x01, 0x00, 0x00};
  if (patch::bytes_are(game::kModelRender2D, head, sizeof(head)))
    patch::hook("3D parts of the interface", game::kModelRender2D, (void*)&hkModelRender2D, (void**)&oModelRender2D);
  else
    LOGE("hud: the interface's model drawing is not where it should be; bar fills stay where the game puts them");
}

}  // namespace hud
