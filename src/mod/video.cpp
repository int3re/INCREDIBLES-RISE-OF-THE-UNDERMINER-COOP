#include "mod/video.h"

#include <d3d9.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "common/log.h"
#include "mod/game.h"
#include "mod/hud.h"
#include "mod/netpage.h"
#include "mod/netplay.h"
#include "mod/patch.h"
#include "mod/postfx.h"
#include "mod/sim.h"

namespace video {
namespace {

Config g_cfg;
std::atomic<HWND> g_hwnd{nullptr};
IDirect3DDevice9* g_dev = nullptr;
std::atomic<unsigned> g_frames{0};
int g_frame_w = 0, g_frame_h = 0;  // the game's back buffer
int g_want_w = 0, g_want_h = 0;    // the frame the mod asks the game for (0: the game's own choice)
bool g_copy = false;               // the swap chain can present into a part of the window
RECT g_dest{}, g_last_client{};
int g_bar_frames = 0;

bool windowed() { return g_cfg.mode != Config::Mode::Game; }

MONITORINFO primary_monitor() {
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  POINT origin{0, 0};
  GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
  return mi;
}

DWORD window_style() {
  return g_cfg.mode == Config::Mode::Window ? (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX) : WS_POPUP;
}

// Where the window goes for a frame of fw x fh.
RECT placement(int fw, int fh) {
  RECT r{};
  MONITORINFO mi = primary_monitor();
  if (g_cfg.mode == Config::Mode::Window) {
    int cw = g_cfg.window_w > 0 && g_cfg.window_h > 0 ? g_cfg.window_w : fw;
    int ch = g_cfg.window_w > 0 && g_cfg.window_h > 0 ? g_cfg.window_h : fh;
    RECT c{0, 0, cw, ch};
    AdjustWindowRectEx(&c, window_style(), FALSE, 0);
    int w = c.right - c.left, h = c.bottom - c.top;
    r.left = mi.rcWork.left + std::max(0L, ((mi.rcWork.right - mi.rcWork.left) - w) / 2);
    r.top = mi.rcWork.top + std::max(0L, ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2);
    r.right = r.left + w;
    r.bottom = r.top + h;
  } else {
    r = mi.rcMonitor;
  }
  if (g_cfg.offscreen) OffsetRect(&r, -32000 - r.left, -32000 - r.top);
  return r;
}

// The frame the mod asks the game for. aspect = wide: the monitor's own size without a frame, a
// window of the monitor's shape at three quarters of the desktop's height (or the window's own
// size). aspect = 4:3: the largest 4:3 frame that fits, shown with black bars.
void choose_frame() {
  if (!windowed() || g_cfg.res_w < 0) return;
  if (g_cfg.res_w > 0) {
    g_want_w = g_cfg.res_w;
    g_want_h = g_cfg.res_h;
    return;
  }
  MONITORINFO mi = primary_monitor();
  int mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
  int w = mw, h = mh;
  if (g_cfg.mode == Config::Mode::Window) {
    if (g_cfg.window_w > 0 && g_cfg.window_h > 0) {
      w = g_cfg.window_w;
      h = g_cfg.window_h;
    } else {
      h = (mi.rcWork.bottom - mi.rcWork.top) * 3 / 4;
      w = g_cfg.wide ? h * mw / mh : h * 4 / 3;  // a window of the monitor's own shape
    }
  }
  if (!g_cfg.wide) {
    if (w * 3 > h * 4) w = h * 4 / 3;  // wider than 4:3: bars at the sides
    if (w * 3 < h * 4) h = w * 3 / 4;  // taller: bars above and below
  } else if (w * 3 < h * 4) {
    h = w * 3 / 4;  // (a frame taller than 4:3 would be the 4:3 picture cut at the sides)
  }
  g_want_w = w & ~1;
  g_want_h = h & ~1;
}

// The frame fitted into the client area with its shape kept; the bars painted black.
const RECT* fit(HWND hwnd) {
  RECT c{};
  if (!GetClientRect(hwnd, &c) || c.right <= 0 || c.bottom <= 0 || g_frame_w <= 0) return nullptr;
  double s = std::min(double(c.right) / g_frame_w, double(c.bottom) / g_frame_h);
  int w = (int)(g_frame_w * s + 0.5), h = (int)(g_frame_h * s + 0.5);
  if (std::abs(w - c.right) <= 1 && std::abs(h - c.bottom) <= 1) {
    g_dest = c;
    return &g_dest;
  }
  g_dest = RECT{(c.right - w) / 2, (c.bottom - h) / 2, 0, 0};
  g_dest.right = g_dest.left + w;
  g_dest.bottom = g_dest.top + h;
  bool changed = memcmp(&c, &g_last_client, sizeof(c)) != 0;
  if (changed || ++g_bar_frames >= 60) {
    g_bar_frames = 0;
    g_last_client = c;
    if (HDC dc = GetDC(hwnd)) {
      HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
      RECT bars[4] = {{0, 0, c.right, g_dest.top},
                      {0, g_dest.bottom, c.right, c.bottom},
                      {0, g_dest.top, g_dest.left, g_dest.bottom},
                      {g_dest.right, g_dest.top, c.right, g_dest.bottom}};
      for (const RECT& b : bars)
        if (b.right > b.left && b.bottom > b.top) FillRect(dc, &b, black);
      ReleaseDC(hwnd, dc);
    }
  }
  return &g_dest;
}

void place(HWND hwnd, int fw, int fh) {
  if (!windowed() || !hwnd) return;
  RECT r = placement(fw, fh);
  SetWindowLongA(hwnd, GWL_STYLE, static_cast<LONG>(window_style() | (IsWindowVisible(hwnd) ? WS_VISIBLE : 0)));
  LONG ex = GetWindowLongA(hwnd, GWL_EXSTYLE) & ~WS_EX_TOPMOST;
  if (g_cfg.no_activate) ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
  SetWindowLongA(hwnd, GWL_EXSTYLE, ex);
  SetWindowPos(hwnd, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
               SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  LOGI("video: window at %ld,%ld size %ldx%ld for a frame of %dx%d", r.left, r.top, r.right - r.left, r.bottom - r.top, fw,
       fh);
}

// --- Direct3D 9 ----------------------------------------------------------------
using Direct3DCreate9_t = IDirect3D9*(WINAPI*)(UINT);
using CreateDevice_t = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                        IDirect3DDevice9**);
using Reset_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Present_t = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
Direct3DCreate9_t oDirect3DCreate9 = nullptr;
CreateDevice_t oCreateDevice = nullptr;
Reset_t oReset = nullptr;
Present_t oPresent = nullptr;

void log_params(const char* what, const D3DPRESENT_PARAMETERS& p) {
  LOGI("d3d9 %s: %ux%u fmt %d, %s, swap %d x%u, interval 0x%X, msaa %d, depth %s %d, flags 0x%X, %u Hz", what,
       p.BackBufferWidth, p.BackBufferHeight, (int)p.BackBufferFormat, p.Windowed ? "windowed" : "fullscreen",
       (int)p.SwapEffect, p.BackBufferCount, p.PresentationInterval, (int)p.MultiSampleType,
       p.EnableAutoDepthStencil ? "auto" : "own", (int)p.AutoDepthStencilFormat, (unsigned)p.Flags,
       p.FullScreen_RefreshRateInHz);
}

IDirect3D9* g_d3d = nullptr;  // CreateDevice's: multisampling is asked of it again at a Reset
UINT g_adapter = 0;
HWND g_device_hwnd = nullptr;
D3DMULTISAMPLE_TYPE g_msaa = D3DMULTISAMPLE_NONE;

// Antialiasing (MSAA) needs the DISCARD swap effect, which presents the whole back buffer onto the
// whole client area: only when the frame fills the window (no bars, no scaling).
D3DMULTISAMPLE_TYPE msaa_for(const D3DPRESENT_PARAMETERS& p) {
  if (g_cfg.antialiasing < 2 || !g_d3d || !p.Windowed || !g_device_hwnd) return D3DMULTISAMPLE_NONE;
  RECT c{};
  if (!GetClientRect(g_device_hwnd, &c) || static_cast<UINT>(c.right) != p.BackBufferWidth ||
      static_cast<UINT>(c.bottom) != p.BackBufferHeight) {
    LOGI("video: antialiasing off: the frame %ux%u does not fill the window (%ldx%ld)", p.BackBufferWidth, p.BackBufferHeight,
         c.right, c.bottom);
    return D3DMULTISAMPLE_NONE;
  }
  for (int n = g_cfg.antialiasing > 8 ? 8 : g_cfg.antialiasing; n >= 2; n /= 2) {
    auto t = static_cast<D3DMULTISAMPLE_TYPE>(n);
    D3DFORMAT bb = p.BackBufferFormat == D3DFMT_UNKNOWN ? D3DFMT_X8R8G8B8 : p.BackBufferFormat;
    if (FAILED(g_d3d->CheckDeviceMultiSampleType(g_adapter, D3DDEVTYPE_HAL, bb, TRUE, t, nullptr))) continue;
    if (p.EnableAutoDepthStencil &&
        FAILED(g_d3d->CheckDeviceMultiSampleType(g_adapter, D3DDEVTYPE_HAL, p.AutoDepthStencilFormat, TRUE, t, nullptr)))
      continue;
    return t;
  }
  LOGI("video: antialiasing is not supported by this video card");
  return D3DMULTISAMPLE_NONE;
}

bool adjust(const D3DPRESENT_PARAMETERS& game, D3DPRESENT_PARAMETERS& out) {
  if (!windowed() && g_cfg.vsync < 0) return false;
  out = game;
  if (windowed()) {
    out.Windowed = TRUE;
    out.FullScreen_RefreshRateInHz = 0;
    // Presenting into a part of the window (bars) needs the COPY swap effect: one back buffer,
    // no multisampling.
    g_copy = game.MultiSampleType == D3DMULTISAMPLE_NONE;
    g_msaa = g_copy && game.EnableAutoDepthStencil ? msaa_for(out) : D3DMULTISAMPLE_NONE;
    if (g_msaa != D3DMULTISAMPLE_NONE) {  // the whole frame onto the whole window, smoothed edges
      g_copy = false;
      out.MultiSampleType = g_msaa;
      out.MultiSampleQuality = 0;
      out.SwapEffect = D3DSWAPEFFECT_DISCARD;
      out.BackBufferCount = 1;
      out.Flags &= ~static_cast<DWORD>(D3DPRESENTFLAG_LOCKABLE_BACKBUFFER);
    } else if (g_copy) {
      out.SwapEffect = D3DSWAPEFFECT_COPY;
      out.BackBufferCount = 1;
    }
  }
  if (g_cfg.vsync >= 0)
    out.PresentationInterval = g_cfg.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
  else if (windowed())
    // The mod holds the frames at 60 a second itself (sim.cpp); a window is shown through the
    // desktop compositor, which does not tear. With the game's own wait for the vertical blank
    // on top of the mod's pace every second frame was lost (31 fps).
    out.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  return true;
}

// --- [debug] shots: the frame from the back buffer, never the desktop -------------
void save_shot(IDirect3DDevice9* dev, unsigned frame) {
  IDirect3DSurface9* bb = nullptr;
  IDirect3DSurface9* sys = nullptr;
  if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
  D3DSURFACE_DESC d{};
  bb->GetDesc(&d);
  IDirect3DSurface9* src = bb;
  IDirect3DSurface9* resolved = nullptr;  // a multisampled back buffer is read through a plain copy
  if (d.MultiSampleType != D3DMULTISAMPLE_NONE &&
      SUCCEEDED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, nullptr)) &&
      SUCCEEDED(dev->StretchRect(bb, nullptr, resolved, nullptr, D3DTEXF_NONE)))
    src = resolved;
  bool ok = (d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8R8G8B8) &&
            SUCCEEDED(dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
            SUCCEEDED(dev->GetRenderTargetData(src, sys));
  if (resolved) resolved->Release();
  D3DLOCKED_RECT lr{};
  if (ok && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
    std::wstring dir = module_dir() + L"screenshots";
    CreateDirectoryW(dir.c_str(), nullptr);
    wchar_t name[64];
    swprintf(name, 64, L"\\in2net_f%06u.bmp", frame);
    std::wstring path = dir + name;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = (LONG)d.Width;
    ih.biHeight = -(LONG)d.Height;  // top-down
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    DWORD row = d.Width * 4;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + row * d.Height;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      DWORD w = 0;
      WriteFile(f, &fh, sizeof(fh), &w, nullptr);
      WriteFile(f, &ih, sizeof(ih), &w, nullptr);
      for (UINT y = 0; y < d.Height; ++y)
        WriteFile(f, static_cast<const uint8_t*>(lr.pBits) + (size_t)y * lr.Pitch, row, &w, nullptr);
      CloseHandle(f);
      LOGI("debug: frame %u saved (%ux%u)", frame, d.Width, d.Height);
    }
    sys->UnlockRect();
  } else {
    LOGW("debug: frame %u not saved (back buffer format %d)", frame, (int)d.Format);
  }
  if (sys) sys->Release();
  bb->Release();
}

HRESULT WINAPI hkPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty) {
  unsigned f = ++g_frames;
  if (f == 1 || f == 60 || f % 3600 == 0) LOGI("video: %u frames presented", f);
  if (tlog::enabled(tlog::Debug)) {
    static DWORD t0 = GetTickCount();
    static unsigned f0 = f;
    DWORD t = GetTickCount();
    if (t - t0 >= 5000) {
      LOGD("video: %.1f fps (frame %u)", (f - f0) * 1000.0 / (t - t0), f);
      t0 = t;
      f0 = f;
    }
  }
  postfx::apply(dev);  // the finished frame: crisper, its colours fuller
  if ((g_cfg.shot_every && f % g_cfg.shot_every == 0) ||
      std::find(g_cfg.shots.begin(), g_cfg.shots.end(), f) != g_cfg.shots.end())
    save_shot(dev, f);
  if (windowed() && g_copy && !dst) {
    HWND hwnd = wnd ? wnd : g_hwnd.load();
    if (hwnd && !IsIconic(hwnd))
      if (const RECT* d = fit(hwnd)) dst = d;
  }
  return oPresent(dev, src, dst, wnd, dirty);
}

HRESULT WINAPI hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
  D3DPRESENT_PARAMETERS mine{};
  postfx::release();  // (its copy of the frame is a default-pool resource: gone before a Reset)
  if (pp) log_params("Reset (game)", *pp);
  if (pp && adjust(*pp, mine)) {
    HRESULT hr = oReset(dev, &mine);
    if (SUCCEEDED(hr)) {
      g_frame_w = (int)mine.BackBufferWidth;
      g_frame_h = (int)mine.BackBufferHeight;
      place(g_hwnd.load(), g_frame_w, g_frame_h);
      return hr;
    }
    LOGE("d3d9 Reset failed (0x%08X) with in2net's parameters; retrying with the game's", (unsigned)hr);
  }
  return oReset(dev, pp);
}

// --- [debug] im2d_log: the device's draw calls with screen-space vertices in one frame ------
using DrawPrimitive_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
using DrawIndexedPrimitive_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using DrawPrimitiveUP_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using DrawIndexedPrimitiveUP_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*,
                                                  D3DFORMAT, const void*, UINT);
DrawPrimitive_t oDrawPrimitive = nullptr;
DrawIndexedPrimitive_t oDrawIndexedPrimitive = nullptr;
DrawPrimitiveUP_t oDrawPrimitiveUP = nullptr;
DrawIndexedPrimitiveUP_t oDrawIndexedPrimitiveUP = nullptr;

bool g_after_post = false;  // after the last whole-frame pass of the picture's processing, before the first sprite

void log_draw(IDirect3DDevice9* dev, const char* what, uintptr_t ra, UINT prims, const void* verts, UINT stride) {
  if (sim::frame() != g_cfg.im2d_log) {
    g_after_post = false;
    return;
  }
  if (ra == 0x0058AA85) g_after_post = true;
  if (ra == 0x005FD3EC || ra == 0x005FD6D8) g_after_post = false;
  DWORD fvf = 0;
  dev->GetFVF(&fvf);
  if (!(fvf & D3DFVF_XYZRHW)) {
    if (!g_after_post && !g_cfg.draw_log_all) return;
    // a 3D draw between an interface element's mask and its picture: who makes it
    char line[400];
    int n = 0, hits = 0;
    const uint32_t* sp = reinterpret_cast<const uint32_t*>(_AddressOfReturnAddress());
    for (int i = 0; i < 400 && hits < 14; ++i) {
      uint32_t v = sp[i];
      if ((v >= 0x00401000 && v < 0x0069A000) || (v >= 0x01055000 && v < 0x012A2000)) {
        n += snprintf(line + n, sizeof(line) - n, " %08X", v);
        ++hits;
      }
    }
    LOGI("draw: 3D %s from 0x%08X before the sprites, %u primitives, fvf 0x%X, stack:%s", what, (unsigned)ra, prims, fvf, line);
    return;
  }
  if (verts && stride >= 8) {
    auto v = static_cast<const float*>(verts);
    auto v1 = reinterpret_cast<const float*>(static_cast<const uint8_t*>(verts) + stride);
    LOGI("draw: %s from 0x%08X, %u primitives, fvf 0x%X, stride %u, first (%.1f %.1f) (%.1f %.1f)", what, (unsigned)ra,
         prims, fvf, stride, v[0], v[1], v1[0], v1[1]);
  } else {
    DWORD scissor = 0, stencil = 0, zen = 0, zfunc = 0, zwrite = 0, atest = 0, aref = 0, afunc = 0, blend = 0, src = 0, dst = 0;
    DWORD tci0 = 0, tci1 = 0, ttf0 = 0, ttf1 = 0, cop0 = 0, cop1 = 0, aop0 = 0, aop1 = 0, cw = 0;
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
    dev->GetRenderState(D3DRS_STENCILENABLE, &stencil);
    dev->GetRenderState(D3DRS_ZENABLE, &zen);
    dev->GetRenderState(D3DRS_ZFUNC, &zfunc);
    dev->GetRenderState(D3DRS_ZWRITEENABLE, &zwrite);
    dev->GetRenderState(D3DRS_ALPHATESTENABLE, &atest);
    dev->GetRenderState(D3DRS_ALPHAREF, &aref);
    dev->GetRenderState(D3DRS_ALPHAFUNC, &afunc);
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
    dev->GetRenderState(D3DRS_SRCBLEND, &src);
    dev->GetRenderState(D3DRS_DESTBLEND, &dst);
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &cw);
    dev->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &tci0);
    dev->GetTextureStageState(1, D3DTSS_TEXCOORDINDEX, &tci1);
    dev->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &ttf0);
    dev->GetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, &ttf1);
    dev->GetTextureStageState(0, D3DTSS_COLOROP, &cop0);
    dev->GetTextureStageState(1, D3DTSS_COLOROP, &cop1);
    dev->GetTextureStageState(0, D3DTSS_ALPHAOP, &aop0);
    dev->GetTextureStageState(1, D3DTSS_ALPHAOP, &aop1);
    IDirect3DBaseTexture9 *t0 = nullptr, *t1 = nullptr;
    dev->GetTexture(0, &t0);
    dev->GetTexture(1, &t1);
    IDirect3DPixelShader9* ps = nullptr;
    dev->GetPixelShader(&ps);
    RECT sr{};
    dev->GetScissorRect(&sr);
    D3DVIEWPORT9 vp{};
    dev->GetViewport(&vp);
    LOGI("draw: %s from 0x%08X, %u primitives, fvf 0x%X | scissor %lu (%ld..%ld) stencil %lu z %lu func %lu write %lu | "
         "atest %lu ref %lu func %lu blend %lu %lu/%lu cw %lX | tex %p %p tci %lX %lX ttf %lu %lu cop %lu %lu aop %lu %lu ps %p | vp %lu,%lu %lux%lu",
         what, (unsigned)ra, prims, fvf, scissor, sr.left, sr.right, stencil, zen, zfunc, zwrite, atest, aref, afunc, blend, src,
         dst, cw, (void*)t0, (void*)t1, tci0, tci1, ttf0, ttf1, cop0, cop1, aop0, aop1, (void*)ps, vp.X, vp.Y, vp.Width,
         vp.Height);
    if (t0) t0->Release();
    if (t1) t1->Release();
    if (ps) ps->Release();
  }
}

HRESULT WINAPI hkDrawPrimitive(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT start, UINT count) {
  log_draw(d, "DrawPrimitive", reinterpret_cast<uintptr_t>(_ReturnAddress()), count, nullptr, 0);
  return oDrawPrimitive(d, t, start, count);
}
HRESULT WINAPI hkDrawIndexedPrimitive(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT base, UINT min, UINT num, UINT start,
                                      UINT count) {
  log_draw(d, "DrawIndexedPrimitive", reinterpret_cast<uintptr_t>(_ReturnAddress()), count, nullptr, 0);
  return oDrawIndexedPrimitive(d, t, base, min, num, start, count);
}
HRESULT WINAPI hkDrawPrimitiveUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT count, const void* data, UINT stride) {
  log_draw(d, "DrawPrimitiveUP", reinterpret_cast<uintptr_t>(_ReturnAddress()), count, data, stride);
  return oDrawPrimitiveUP(d, t, count, data, stride);
}
HRESULT WINAPI hkDrawIndexedPrimitiveUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT min, UINT num, UINT count,
                                        const void* index, D3DFORMAT fmt, const void* data, UINT stride) {
  log_draw(d, "DrawIndexedPrimitiveUP", reinterpret_cast<uintptr_t>(_ReturnAddress()), count, data, stride);
  return oDrawIndexedPrimitiveUP(d, t, min, num, count, index, fmt, data, stride);
}

// Anisotropic filtering: the game's bilinear minification becomes anisotropic (sharper floors and
// walls at an angle), as far as the card allows.
using SetSamplerState_t = HRESULT(WINAPI*)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
SetSamplerState_t oSetSamplerState = nullptr;
DWORD g_aniso = 0;  // 0: off

HRESULT WINAPI hkSetSamplerState(IDirect3DDevice9* dev, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) {
  if (g_aniso > 1 && type == D3DSAMP_MINFILTER && value == D3DTEXF_LINEAR) {
    oSetSamplerState(dev, sampler, D3DSAMP_MAXANISOTROPY, g_aniso);
    value = D3DTEXF_ANISOTROPIC;
  }
  return oSetSamplerState(dev, sampler, type, value);
}

void hook_device(IDirect3DDevice9* dev) {
  g_dev = dev;
  if (oPresent) return;
  void** vt = *reinterpret_cast<void***>(dev);
  if (g_cfg.anisotropy > 1) {
    D3DCAPS9 caps{};
    if (SUCCEEDED(dev->GetDeviceCaps(&caps)) && (caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) && caps.MaxAnisotropy > 1) {
      g_aniso = std::min<DWORD>(static_cast<DWORD>(g_cfg.anisotropy), caps.MaxAnisotropy);
      patch::hook("IDirect3DDevice9::SetSamplerState (anisotropic filtering)", reinterpret_cast<uintptr_t>(vt[69]),
                  (void*)&hkSetSamplerState, (void**)&oSetSamplerState);
      LOGI("video: anisotropic filtering x%u", static_cast<unsigned>(g_aniso));
    }
  }
  if (g_cfg.im2d_log) {
    patch::hook("IDirect3DDevice9::DrawPrimitive", reinterpret_cast<uintptr_t>(vt[81]), (void*)&hkDrawPrimitive,
                (void**)&oDrawPrimitive);
    patch::hook("IDirect3DDevice9::DrawIndexedPrimitive", reinterpret_cast<uintptr_t>(vt[82]),
                (void*)&hkDrawIndexedPrimitive, (void**)&oDrawIndexedPrimitive);
    patch::hook("IDirect3DDevice9::DrawPrimitiveUP", reinterpret_cast<uintptr_t>(vt[83]), (void*)&hkDrawPrimitiveUP,
                (void**)&oDrawPrimitiveUP);
    patch::hook("IDirect3DDevice9::DrawIndexedPrimitiveUP", reinterpret_cast<uintptr_t>(vt[84]),
                (void*)&hkDrawIndexedPrimitiveUP, (void**)&oDrawIndexedPrimitiveUP);
  }
  patch::hook("IDirect3DDevice9::Reset", reinterpret_cast<uintptr_t>(vt[16]), (void*)&hkReset, (void**)&oReset);
  patch::hook("IDirect3DDevice9::Present", reinterpret_cast<uintptr_t>(vt[17]), (void*)&hkPresent, (void**)&oPresent);
}

HRESULT WINAPI hkCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                              D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
  D3DPRESENT_PARAMETERS mine{};
  D3DPRESENT_PARAMETERS* use = pp;
  if (pp) {
    log_params("CreateDevice (game)", *pp);
    if (adjust(*pp, mine)) {
      use = &mine;
      log_params("CreateDevice (in2net)", mine);
    }
  }
  if (g_cfg.guard && use && !use->Windowed) {
    // Lab runs must never switch the owner's desktop to another resolution.
    LOGE("debug: guard: the game asks for a full-screen device; ending the run");
    TerminateProcess(GetCurrentProcess(), 4);
  }
  HWND hwnd = pp && pp->hDeviceWindow ? pp->hDeviceWindow : focus;
  if (hwnd) g_hwnd = hwnd;
  g_d3d = d3d;
  g_adapter = adapter;
  g_device_hwnd = hwnd;
  if (pp && use == &mine && windowed()) {
    // (the window takes its place before the device is made: antialiasing asks whether the frame fills it)
    place(hwnd, static_cast<int>(mine.BackBufferWidth), static_cast<int>(mine.BackBufferHeight));
    adjust(*pp, mine);
    log_params("CreateDevice (in2net, window placed)", mine);
  }
  if (use && use->Windowed) place(hwnd, (int)use->BackBufferWidth, (int)use->BackBufferHeight);
  HRESULT hr = oCreateDevice(d3d, adapter, type, focus, flags, use, out);
  if (FAILED(hr) && use != pp && !g_cfg.guard) {
    LOGE("d3d9 CreateDevice failed (0x%08X) with in2net's parameters; retrying with the game's", (unsigned)hr);
    hr = oCreateDevice(d3d, adapter, type, focus, flags, pp, out);
  }
  if (FAILED(hr)) {
    LOGE("d3d9 CreateDevice failed: 0x%08X", (unsigned)hr);
    return hr;
  }
  if (use) {
    g_frame_w = (int)use->BackBufferWidth;
    g_frame_h = (int)use->BackBufferHeight;
  }
  if (out && *out) hook_device(*out);
  return hr;
}

// The game takes its frame's size from the list of the monitor's display modes. The mod's own
// size (any 4:3 size: the device is a windowed one) stands in for the first mode of the list.
using EnumAdapterModes_t = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DFORMAT, UINT, D3DDISPLAYMODE*);
EnumAdapterModes_t oEnumAdapterModes = nullptr;
HRESULT WINAPI hkEnumAdapterModes(IDirect3D9* d3d, UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE* out) {
  HRESULT hr = oEnumAdapterModes(d3d, adapter, format, mode, out);
  if (SUCCEEDED(hr) && out && mode == 0 && g_want_w > 0) {
    out->Width = static_cast<UINT>(g_want_w);
    out->Height = static_cast<UINT>(g_want_h);
  }
  return hr;
}

IDirect3D9* WINAPI hkDirect3DCreate9(UINT sdk) {
  IDirect3D9* d3d = oDirect3DCreate9(sdk);
  LOGI("d3d9: Direct3DCreate9(%u) -> %p, asked from 0x%08X", sdk, (void*)d3d, (unsigned)(uintptr_t)_ReturnAddress());
  if (d3d && !oCreateDevice) {
    void** vt = *reinterpret_cast<void***>(d3d);
    patch::hook("IDirect3D9::CreateDevice", reinterpret_cast<uintptr_t>(vt[16]), (void*)&hkCreateDevice,
                (void**)&oCreateDevice);
    if (g_want_w > 0)
      patch::hook("IDirect3D9::EnumAdapterModes", reinterpret_cast<uintptr_t>(vt[7]), (void*)&hkEnumAdapterModes,
                  (void**)&oEnumAdapterModes);
  }
  return d3d;
}

// --- the frame's size ---------------------------------------------------------------
// (a member function: `this` in ecx, the two pointers on the stack, `ret 8`)
using GetDisplayRes_t = void(__thiscall*)(void*, long*, long*);
GetDisplayRes_t oGetDisplayRes = nullptr;

void __fastcall hkGetDisplayRes(void* self, void* /*edx*/, long* w, long* h) {
  oGetDisplayRes(self, w, h);
  if (!w || !h || g_cfg.res_w < 0) return;
  long gw = *w, gh = *h;
  if (g_want_w > 0) {
    *w = g_want_w;
    *h = g_want_h;
  }
  static bool said = false;
  if (!said) {
    said = true;
    LOGI("video: frame %ldx%ld (the game's options say %ldx%ld)", *w, *h, gw, gh);
  }
}

// The game centres the mouse cursor in its window every frame (the port's input code). That is the
// game's business only while its window is the one in front: a game that runs in the background
// (a network game always does) would keep pulling the cursor out of the player's other window, and a
// lab window, which lies off the screen, pulls it into the desktop's corner.
BOOL WINAPI hkSetCursorPos(int x, int y) {
  HWND hwnd = g_hwnd.load();
  if (g_cfg.no_activate || g_cfg.offscreen || !hwnd || GetForegroundWindow() != hwnd) {
    static bool said = false;
    if (!said) LOGI("video: the game's window is not in front: the mouse cursor is left where it is");
    said = true;
    return TRUE;
  }
  return SetCursorPos(x, y);
}

}  // namespace

void install_game(const Config&) {
  {
    void* real = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("user32.dll"), "SetCursorPos"));
    patch::slot("the game's SetCursorPos", game::kIatSetCursorPos, reinterpret_cast<uintptr_t>(real), (void*)&hkSetCursorPos);
  }
  static const uint8_t head[] = {0x8B, 0x0D, 0xC0, 0xF2, 0x7F, 0x00, 0x83, 0xEC, 0x10};  // mov ecx, [0x7FF2C0]; sub esp, 0x10
  if (!patch::bytes_are(game::kGetDisplayRes, head, sizeof(head))) {
    LOGE("video: the game's choice of resolution is not where it should be; the game's own size is used");
    return;
  }
  patch::hook("the frame's size", game::kGetDisplayRes, (void*)&hkGetDisplayRes, (void**)&oGetDisplayRes);
}

HWND window() { return g_hwnd.load(); }
IDirect3DDevice9* device() { return g_dev; }
unsigned frames() { return g_frames.load(); }

bool window_args(DWORD& ex, DWORD& style, int& x, int& y, int& w, int& h) {
  if (!windowed()) return false;
  // The size the game asked for is its frame's; the device's creation places the window for good.
  RECT r = placement(w, h);
  ex &= ~static_cast<DWORD>(WS_EX_TOPMOST);
  if (g_cfg.no_activate) ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
  style = (style & ~static_cast<DWORD>(WS_POPUP | WS_OVERLAPPEDWINDOW)) | window_style();
  x = r.left;
  y = r.top;
  w = r.right - r.left;
  h = r.bottom - r.top;
  return true;
}

// --- the window's messages -------------------------------------------------------
// The game's own procedure minimizes the window when it is not in front and maximizes
// it when it is again (it knows only an exclusive full screen), and holds its "in
// front" flag: with the flag off the game loop waits and no control is read. In the
// mod's window modes activation is handled here and never reaches the game.
WNDPROC g_game_proc = nullptr;

void set_active(bool active) {
  *reinterpret_cast<uint8_t*>(game::kAppActive) = active ? 1 : 0;
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_ACTIVATE && windowed()) {
    bool front = LOWORD(wp) != WA_INACTIVE;
    bool run = front || g_cfg.background || g_cfg.no_activate;
    LOGI("video: the window is %s%s; the game %s", front ? "active" : "not active",
         GetForegroundWindow() == hwnd ? " and in front of all" : "", run ? "runs" : "waits");
    set_active(run);
    // (what Windows' default handling of the message does, and what the game's own maximizing
    // of its window did: the keyboard goes to the game's window)
    if (front) SetFocus(hwnd);
    if (g_cfg.mode == Config::Mode::Borderless) {
      // one hidden / shown step each way (ShowCursor counts)
      static bool hidden = false;
      if (front != hidden) {
        ShowCursor(front ? FALSE : TRUE);
        hidden = front;
      }
    }
    return 0;
  }
  if (msg == WM_CLOSE || msg == WM_DESTROY) netplay::leave("this game is closing");
  // The network page's keys (and the key that opens it) are not the game's.
  if (msg == WM_KEYDOWN && netpage::on_key(static_cast<unsigned>(wp))) return 0;
  if (msg == WM_CHAR && netpage::on_char(static_cast<wchar_t>(wp))) return 0;
  if (netpage::active() && (msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)))
    return DefWindowProcA(hwnd, msg, wp, lp);
  // A network game is played with what goes over the network: the pads. The game's menus also
  // follow the mouse and take typed characters - on this PC only, so the two games would part.
  if (g_cfg.net_mode != Config::Net::Off && ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_CHAR))
    return DefWindowProcA(hwnd, msg, wp, lp);
  return CallWindowProcA(g_game_proc, hwnd, msg, wp, lp);
}

void window_made(HWND hwnd) {
  g_hwnd = hwnd;
  if (g_game_proc) return;
  // (in every screen mode: the network page takes its keys here; activation is the mod's only in its own modes)
  g_game_proc = reinterpret_cast<WNDPROC>(SetWindowLongA(hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(&window_proc)));
  if (!windowed()) return;
  if (g_cfg.no_activate) set_active(true);  // a lab window is never in front: the game runs all the same
}

namespace {
DWORD WINAPI quit_watch(void*) {
  Sleep(4000);
  LOGW("video: the game did not close by itself; ending it");
  TerminateProcess(GetCurrentProcess(), 0);
  return 0;
}
}  // namespace

void request_quit() {
  static bool asked = false;
  if (asked) return;
  asked = true;
  if (HWND hwnd = g_hwnd.load()) PostMessageA(hwnd, WM_CLOSE, 0, 0);
  if (HANDLE t = CreateThread(nullptr, 0, quit_watch, nullptr, 0, nullptr)) CloseHandle(t);
}

int show_command(int cmd) {
  if (g_cfg.no_activate && cmd != SW_HIDE) return SW_SHOWNOACTIVATE;
  return cmd;
}

void install(const Config& cfg) {
  g_cfg = cfg;
  choose_frame();
  LOGI("video: mode %s%s%s", cfg.mode == Config::Mode::Game ? "game (exclusive full screen)"
                              : cfg.mode == Config::Mode::Window ? "window" : "borderless",
       cfg.offscreen ? ", off screen" : "", cfg.no_activate ? ", never activated" : "");
  // The game loads <system>\d3d9.dll by its full path and asks for Direct3DCreate9 by name:
  // the export itself is hooked (the dll is loaded here first, it stays the same module).
  patch::hook_api("d3d9.dll", "Direct3DCreate9", (void*)&hkDirect3DCreate9, (void**)&oDirect3DCreate9);
  // The game paces its frames by GetTickCount and Sleep: with Windows' default 15.6 ms timer it
  // runs at 32 frames a second. A 1 ms timer for the whole run.
  if (HMODULE winmm = LoadLibraryA("winmm.dll"))
    if (auto begin = reinterpret_cast<UINT(WINAPI*)(UINT)>(GetProcAddress(winmm, "timeBeginPeriod"))) begin(1);
}

}  // namespace video
