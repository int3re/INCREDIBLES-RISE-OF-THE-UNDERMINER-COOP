#include "mod/notice.h"

#include <windows.h>

#include <cstring>
#include <mutex>
#include <vector>

#include "common/log.h"
#include "mod/game.h"
#include "mod/gameui.h"
#include "mod/patch.h"
#include "mod/sim.h"

namespace notice {
namespace {

Config g_cfg;

// The engine's font for a string (game.h: kFont...).
struct XFont {
  uint32_t id;
  float width, height;  // of a letter, in parts of the frame's width / height
  float space;
  uint32_t color;       // bytes r, g, b, a
  uint32_t shadow;      // the same letters drawn first, moved by (drop_x, drop_y) pixels
  float drop_x, drop_y;
  float clip[4];        // x, y, w, h in parts of the frame
};
static_assert(sizeof(XFont) == 0x30, "the engine's font record is 0x30 bytes");

using Start_t = void(__cdecl*)(const XFont*);
using Render_t = void(__thiscall*)(const XFont*, const char* text, uint32_t size, float x, float y);
using Stop_t = void(__cdecl*)();

std::mutex g_mu;
std::vector<std::string> g_lines;  // in the release's lettering
ULONGLONG g_from = 0, g_until = 0;
bool g_checked = false, g_ok = false;

constexpr uint32_t kFont = 0;  // the menus' font

// A font's table: +0 its asset (0 = not loaded), +8 the glyph of every character (0xFF = none),
// glyph i's width at float index 0x2C7 + 4 i (in letter widths).
const uint8_t* table(uint32_t font) { return reinterpret_cast<const uint8_t*>(game::kFontTables + font * game::kFontTableSize); }

float width_of(const std::string& s, const XFont& f) {
  const uint8_t* t = table(f.id);
  float w = 0;
  for (unsigned char c : s) {
    uint8_t glyph = t[8 + c];
    if (glyph == 0xFF) continue;
    float gw;
    memcpy(&gw, t + (0x2C7 + glyph * 4u) * 4u, 4);
    w += gw * f.width + f.space;
  }
  return w;
}

}  // namespace

void install(const Config& cfg) {
  g_cfg = cfg;
  if (cfg.notice_test > 0) say(L"ВТОРОЙ ИГРОК ВЫШЕЛ ИЗ ИГРЫ\nДАЛЬШЕ ВЫ ИГРАЕТЕ ОДИН", cfg.notice_test);
}

void say(const std::wstring& text, double seconds) {
  std::vector<std::string> lines;
  size_t at = 0;
  while (at <= text.size() && lines.size() < 3) {
    size_t end = text.find(L'\n', at);
    if (end == std::wstring::npos) end = text.size();
    lines.push_back(gameui::letters(text.substr(at, end - at)));
    at = end + 1;
  }
  std::lock_guard<std::mutex> lk(g_mu);
  g_lines = std::move(lines);
  g_from = GetTickCount64();
  g_until = g_from + static_cast<ULONGLONG>(seconds * 1000);
}

void draw() {
  std::vector<std::string> lines;
  ULONGLONG from, until, now = GetTickCount64();
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_lines.empty()) return;
    if (now >= g_until) {
      g_lines.clear();
      return;
    }
    lines = g_lines;
    from = g_from;
    until = g_until;
  }
  if (!g_checked) {
    g_checked = true;
    static const uint8_t start_head[] = {0x8B, 0x15, 0xA0, 0x9B, 0x7F, 0x00};  // mov edx, [0x7F9BA0]
    static const uint8_t render_head[] = {0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x28};  // mov eax, [esp+4]; sub esp, 0x28
    static const uint8_t stop_head[] = {0xA1, 0xD0, 0x31, 0x76, 0x00};  // mov eax, [0x7631D0]
    g_ok = patch::bytes_are(game::kFontStart, start_head, sizeof(start_head)) &&
           patch::bytes_are(game::kFontRender, render_head, sizeof(render_head)) &&
           patch::bytes_are(game::kFontStop, stop_head, sizeof(stop_head));
    if (!g_ok) LOGE("notice: the game's string drawing is not where it should be; nothing is shown on the screen");
  }
  if (!g_ok || patch::read<uint32_t>(reinterpret_cast<uintptr_t>(table(kFont))) == 0) return;  // (the font is not loaded yet)

  // in and out in a third of a second
  float a = 1.0f;
  if (now - from < 300) a = (now - from) / 300.0f;
  if (until - now < 400) a = (until - now) / 400.0f;
  uint32_t alpha = static_cast<uint32_t>(a * 255.0f + 0.5f) << 24;

  XFont f{};
  f.id = kFont;
  f.width = 17.0f / 640.0f;   // (the menus' buttons: 20)
  f.height = 17.0f / 480.0f;
  f.space = 0;
  f.color = alpha | 0x003CC8FF;  // r FF, g C8, b 3C: the menus' lit orange
  f.shadow = alpha;              // black
  f.drop_x = f.drop_y = 2.0f;
  f.clip[0] = 0, f.clip[1] = 0, f.clip[2] = 1, f.clip[3] = 1;

  auto start = reinterpret_cast<Start_t>(game::kFontStart);
  auto render = reinterpret_cast<Render_t>(game::kFontRender);
  auto stop = reinterpret_cast<Stop_t>(game::kFontStop);
  start(&f);
  // in the menus: above the title's frame (and above the title screen's logo); in a level: under the heroes' panels
  float y = sim::scene() == 0x4D4E5553 ? 0.07f : 0.245f;
  for (const std::string& s : lines) {
    if (!s.empty()) render(&f, s.c_str(), static_cast<uint32_t>(s.size()), 0.5f - width_of(s, f) * 0.5f, y);
    y += f.height * 1.25f;
  }
  stop();
}

}  // namespace notice
