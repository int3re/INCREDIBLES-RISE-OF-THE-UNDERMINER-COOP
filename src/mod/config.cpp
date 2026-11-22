#include "mod/config.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {

std::string get(const std::wstring& path, const wchar_t* section, const wchar_t* key, const char* fallback) {
  wchar_t buf[512];
  DWORD n = GetPrivateProfileStringW(section, key, L"\x01", buf, 512, path.c_str());
  if (n == 1 && buf[0] == L'\x01') return fallback;
  std::string s;
  for (DWORD i = 0; i < n; ++i) s += static_cast<char>(buf[i] < 128 ? buf[i] : '?');
  // "value ; comment" -> "value"
  size_t c = s.find(';');
  if (c != std::string::npos) s.erase(c);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  for (char& ch : s)
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  return s;
}

int get_int(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
  std::string s = get(path, section, key, "");
  if (s.empty()) return fallback;
  return static_cast<int>(strtol(s.c_str(), nullptr, 0));
}

bool get_bool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool fallback) {
  std::string s = get(path, section, key, "");
  if (s.empty()) return fallback;
  return s == "1" || s == "true" || s == "yes" || s == "on";
}

}  // namespace

std::wstring module_dir() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring s = path;
  size_t slash = s.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"" : s.substr(0, slash + 1);
}

bool load_config(const std::wstring& path, Config& cfg) {
  bool have = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
  cfg.log_level = tlog::parse_level(get(path, L"log", L"level", "info"), tlog::Info);
  std::string mode = get(path, L"video", L"mode", "borderless");
  cfg.mode = mode == "game" ? Config::Mode::Game : mode == "window" ? Config::Mode::Window : Config::Mode::Borderless;
  {
    std::string res = get(path, L"video", L"resolution", "auto");
    int w = 0, h = 0;
    if (res == "game")
      cfg.res_w = cfg.res_h = -1;
    else if (sscanf(res.c_str(), "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240)
      cfg.res_w = w, cfg.res_h = h;
  }
  cfg.wide = get(path, L"video", L"aspect", "wide") != "4:3";
  cfg.own_camera = get_bool(path, L"video", L"own_camera", true);
  cfg.window_w = get_int(path, L"video", L"window_width", 0);
  cfg.window_h = get_int(path, L"video", L"window_height", 0);
  cfg.vsync = get_int(path, L"video", L"vsync", -1);
  cfg.background = get_bool(path, L"video", L"background", false);
  cfg.antialiasing = get_int(path, L"video", L"antialiasing", 4);
  cfg.anisotropy = get_int(path, L"video", L"anisotropy", 16);
  cfg.sharpen = get_int(path, L"video", L"sharpen", 35);
  cfg.colors = get_int(path, L"video", L"colors", 0);
  cfg.postfx_split = get_bool(path, L"debug", L"postfx_split", false);

  std::string net = get(path, L"net", L"mode", "off");
  cfg.net_mode = net == "host" ? Config::Net::Host : net == "join" ? Config::Net::Join : Config::Net::Off;
  cfg.net_port = get_int(path, L"net", L"port", 47820);
  cfg.net_connect = get(path, L"net", L"connect", "127.0.0.1");
  cfg.net_name = get(path, L"net", L"name", "player");
  cfg.net_delay = get_int(path, L"net", L"delay", 2);
  if (cfg.net_delay < 1) cfg.net_delay = 1;
  if (cfg.net_delay > 12) cfg.net_delay = 12;
  cfg.net_wait_s = get_int(path, L"net", L"wait", 120);
  cfg.net_transport = get(path, L"net", L"transport", "auto");
  cfg.net_lobby = get(path, L"net", L"lobby", "friends");
  cfg.mock_slot = get_int(path, L"debug", L"mock_slot", 1);
  cfg.mock_dir = get(path, L"debug", L"mock_dir", "");
  cfg.mock_loss_pct = get_int(path, L"debug", L"mock_loss", 0);
  cfg.mock_latency_ms = get_int(path, L"debug", L"mock_latency", 0);
  cfg.mock_member_delay_ms = get_int(path, L"debug", L"mock_member_delay", 0);
  // (a game that waits while its window is not in front would hold the other player's game too)
  if (cfg.net_mode != Config::Net::Off) cfg.background = true;
  cfg.net_loss = get_int(path, L"debug", L"net_loss", 0);
  {
    std::string t = get(path, L"debug", L"net_outage", "");
    float a = 0, b = 0;
    if (sscanf(t.c_str(), "%f,%f", &a, &b) == 2)
      cfg.net_outage_at_ms = static_cast<unsigned>(a * 1000), cfg.net_outage_ms = static_cast<unsigned>(b * 1000);
  }
  {
    std::string t = get(path, L"debug", L"net_stall", "");
    float a = 0, b = 0;
    if (sscanf(t.c_str(), "%f,%f", &a, &b) == 2)
      cfg.net_stall_at_ms = static_cast<unsigned>(a * 1000), cfg.net_stall_ms = static_cast<unsigned>(b * 1000);
  }
  cfg.net_quit_ms = static_cast<unsigned>(get_int(path, L"debug", L"net_quit", 0)) * 1000;
  cfg.multi = get_bool(path, L"debug", L"multi", false);

  cfg.dump = get(path, L"debug", L"dump", "off");
  cfg.boot_trace = get_bool(path, L"debug", L"boot_trace", false);
  cfg.autoquit_s = get_int(path, L"debug", L"autoquit", 0);
  cfg.guard = get_bool(path, L"debug", L"guard", false);
  cfg.no_activate = get_bool(path, L"debug", L"no_activate", false);
  cfg.offscreen = get_bool(path, L"debug", L"offscreen", false);
  cfg.script = get(path, L"debug", L"script", "");
  cfg.save_dir = get(path, L"debug", L"save_dir", "");
  {
    std::string t = get(path, L"debug", L"rng_trace", "");
    unsigned a = 0, b = 0;
    if (sscanf(t.c_str(), "%u-%u", &a, &b) == 2) cfg.rng_trace_from = a, cfg.rng_trace_to = b;
  }
  cfg.rng_calls = get_bool(path, L"debug", L"rng_calls", false);
  cfg.sound_clock = get_bool(path, L"debug", L"sound_clock", true);
  cfg.mute = get_bool(path, L"debug", L"mute", false);
  cfg.no_sound = get_bool(path, L"debug", L"no_sound", false);
  cfg.im2d_log = static_cast<unsigned>(get_int(path, L"debug", L"im2d_log", 0));
  cfg.draw_log_all = get_bool(path, L"debug", L"draw_log_all", false);
  cfg.own_camera_test = get_int(path, L"debug", L"own_camera", 0);
  cfg.force_hdr = get_int(path, L"debug", L"hdr", -1);
  cfg.force_shadows = get_int(path, L"debug", L"shadows", -1);
  cfg.camera_log = static_cast<unsigned>(get_int(path, L"debug", L"camera_log", 0));
  cfg.fast = get_bool(path, L"debug", L"fast", false);
  cfg.ui_log = get_bool(path, L"debug", L"ui_log", false);
  cfg.notice_test = get_int(path, L"debug", L"notice", 0);
  cfg.ui_font = get_int(path, L"debug", L"ui_font", -1);
  cfg.sim_log = get_bool(path, L"debug", L"sim_log", false);
  cfg.shot_every = static_cast<unsigned>(get_int(path, L"debug", L"shot_every", 0));
  std::string shots = get(path, L"debug", L"shots", "");
  for (size_t i = 0; i < shots.size();) {
    size_t j = shots.find(',', i);
    if (j == std::string::npos) j = shots.size();
    unsigned v = static_cast<unsigned>(strtoul(shots.substr(i, j - i).c_str(), nullptr, 10));
    if (v) cfg.shots.push_back(v);
    i = j + 1;
  }
  return have;
}
