// The mod's settings the launcher's pages edit: see gameset.h.
#include "launcher/gameset.h"

#include <windows.h>

#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <utility>
#include <vector>

#include "launcher/launcher.h"

namespace lz {
namespace gset {
namespace {

const wchar_t kIni[] = L"in2net.ini";

struct Choice {
  const wchar_t* name;
  const wchar_t* value;  // as in2net.ini has it
};
struct Info {
  const wchar_t* label;
  const char* key;
  Kind kind;
  const wchar_t* section;
  const wchar_t* ini;
  const wchar_t* fallback;  // the value when the ini has none: the mod's default
  std::vector<Choice> choices;
};
// The values are the ones the mod reads (src/mod/config.cpp, dist/in2net.ini).
const Info kInfo[kRows] = {
    {L"Окно игры", "mode", Kind::Choice, L"video", L"mode", L"borderless",
     {{L"На весь монитор", L"borderless"}, {L"В окне", L"window"}, {L"Полный экран игры", L"game"}}},
    {L"Разрешение", "resolution", Kind::Choice, L"video", L"resolution", L"auto", {}},
    {L"Формат кадра", "aspect", Kind::Pair, L"video", L"aspect", L"wide", {{L"Широкий", L"wide"}, {L"4:3", L"4:3"}}},
    {L"Верт. синхронизация", "vsync", Kind::Choice, L"video", L"vsync", L"-1",
     {{L"Сама", L"-1"}, {L"Выключена", L"0"}, {L"Включена", L"1"}}},
    {L"Камера в сети", "own_camera", Kind::Pair, L"video", L"own_camera", L"1", {{L"Своя", L"1"}, {L"Общая", L"0"}}},
    {L"Игра в фоне", "background", Kind::Pair, L"video", L"background", L"0", {{L"Ждёт", L"0"}, {L"Идёт", L"1"}}},
    {L"Сглаживание краёв", "antialiasing", Kind::Choice, L"video", L"antialiasing", L"4",
     {{L"Выключено", L"0"}, {L"2x", L"2"}, {L"4x", L"4"}, {L"8x", L"8"}}},
    {L"Чёткость текстур", "anisotropy", Kind::Choice, L"video", L"anisotropy", L"16",
     {{L"Как в игре", L"0"}, {L"4x", L"4"}, {L"8x", L"8"}, {L"16x", L"16"}}},
    {L"Резкость кадра", "sharpen", Kind::Choice, L"video", L"sharpen", L"35",
     {{L"Выключена", L"0"}, {L"Лёгкая", L"20"}, {L"Средняя", L"35"}, {L"Сильная", L"55"}, {L"Очень сильная", L"80"}}},
    {L"Цвета", "colors", Kind::Choice, L"video", L"colors", L"0",
     {{L"Как в игре", L"0"}, {L"Чуть ярче", L"20"}, {L"Ярче", L"40"}, {L"Сочные", L"65"}}},
    {L"Соединение", "transport", Kind::Choice, L"net", L"transport", L"auto",
     {{L"Steam и адрес", L"auto"}, {L"Только Steam", L"steam"}, {L"Только адрес", L"enet"}}},
    {L"Игру в Steam видят", "lobby", Kind::Pair, L"net", L"lobby", L"friends", {{L"Друзья", L"friends"}, {L"Все", L"public"}}},
    {L"Задержка ввода", "delay", Kind::Choice, L"net", L"delay", L"2",
     {{L"1 кадр", L"1"}, {L"2 кадра", L"2"}, {L"3 кадра", L"3"}, {L"4 кадра", L"4"}, {L"5 кадров", L"5"}, {L"6 кадров", L"6"},
      {L"8 кадров", L"8"}, {L"10 кадров", L"10"}, {L"12 кадров", L"12"}}},
};

struct Res {
  std::wstring name, value;
};

struct State {
  std::wstring dir, ini;  // the folder; in2net.ini where the game reads it
  bool mod = false, canned = false;
  int cur[kRows] = {};
  std::vector<Res> res;
  std::wstring error;
  bool kept = false;  // a copy of the ini was put aside before this run's first change
};
State g;

std::wstring env(const wchar_t* name) {
  wchar_t b[MAX_PATH * 2];
  const DWORD n = GetEnvironmentVariableW(name, b, MAX_PATH * 2);
  return n && n < MAX_PATH * 2 ? std::wstring(b, n) : std::wstring();
}

// --- the game's own view of its folder ---------------------------------------------------------------
// incrotu.exe has no manifest, so in a folder its user may not write to (Program Files) Windows gives it
// copies of the files it writes, under <LocalAppData>\VirtualStore\<the path without its drive>, and
// shows it those copies from then on. The launcher reads and writes the file the game sees.

std::wstring store_root() {
  const std::wstring lab = env(L"IN2NET_LAUNCHER_VSTORE");
  if (!lab.empty()) return lab;
  PWSTR local = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) dir = local;
  CoTaskMemFree(local);
  return dir.empty() ? dir : core::join(dir, L"VirtualStore");
}

std::wstring stored(const std::wstring& dir) {
  const std::wstring root = store_root();
  if (root.empty() || dir.size() < 3 || dir[1] != L':') return L"";
  return core::join(core::join(root, dir.substr(3)), kIni);
}

bool is_protected(const std::wstring& dir) {
  if (!env(L"IN2NET_LAUNCHER_VSTORE").empty()) return true;  // (lab: any folder)
  for (const wchar_t* name : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432", L"ProgramData", L"SystemRoot"}) {
    const std::wstring p = env(name);
    if (p.empty() || dir.size() <= p.size() || _wcsnicmp(dir.c_str(), p.c_str(), p.size()) != 0) continue;
    if (dir[p.size()] == L'\\' || dir[p.size()] == L'/') return true;
  }
  return false;
}

bool writable(const std::wstring& file) {
  HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  CloseHandle(f);
  return true;
}

std::wstring seen(const std::wstring& dir) {
  const std::wstring copy = stored(dir);
  return !copy.empty() && core::exists(copy) ? copy : core::join(dir, kIni);
}

// Before a change: where it goes, and a copy of the file as it was (once a run).
bool ready() {
  const std::wstring own = core::join(g.dir, kIni);
  if (g.ini == own && is_protected(g.dir) && (!env(L"IN2NET_LAUNCHER_VSTORE").empty() || !writable(own))) {
    // The folder is closed to this user: the change goes where Windows would put the game's own.
    const std::wstring copy = stored(g.dir);
    const size_t slash = copy.find_last_of(L'\\');
    const wchar_t* closed = L"Папка игры закрыта для записи: запустите лаунчер от имени администратора.";
    if (copy.empty() || slash == std::wstring::npos) {
      g.error = closed;
      return false;
    }
    SHCreateDirectoryExW(nullptr, copy.substr(0, slash).c_str(), nullptr);
    if (!CopyFileW(own.c_str(), copy.c_str(), TRUE)) {
      g.error = closed;
      return false;
    }
    g.ini = copy;
  }
  if (!g.kept) {
    g.kept = true;
    CopyFileW(g.ini.c_str(), core::join(core::downloads_dir(), std::wstring(kIni) + L".before-launcher").c_str(), FALSE);
  }
  return true;
}

// --- the ini ---------------------------------------------------------------------------------------------

std::wstring get(const wchar_t* section, const wchar_t* key) {
  wchar_t buf[512] = {};
  GetPrivateProfileStringW(section, key, L"", buf, 512, g.ini.c_str());
  std::wstring v(buf);
  const size_t semi = v.find(L';');  // (an inline comment)
  if (semi != std::wstring::npos) v.resize(semi);
  while (!v.empty() && iswspace(v.back())) v.pop_back();
  size_t b = 0;
  while (b < v.size() && iswspace(v[b])) ++b;
  v = v.substr(b);
  for (auto& c : v) c = towlower(c);
  return v;
}

bool put(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
  if (g.canned) return true;
  if (!ready()) return false;
  if (WritePrivateProfileStringW(section, key, value.c_str(), g.ini.c_str())) return true;
  g.error = L"in2net.ini не записался (ошибка " + std::to_wstring(GetLastError()) + L").";
  return false;
}

bool is_number(const std::wstring& v) { return !v.empty() && (iswdigit(v[0]) || v[0] == L'-' || v[0] == L'.'); }

// The choice a value of the ini means: the one that has it, the nearest for a number between the
// choices (an ini written by hand), else the mod's default.
int index_of(const Info& info, std::wstring v) {
  const std::wstring key = info.ini;
  if (key == L"mode" && (v == L"windowed")) v = L"window";
  if (key == L"own_camera" || key == L"background") v = v == L"1" || v == L"true" || v == L"yes" || v == L"on" ? L"1" : v.empty() ? v : L"0";
  if (v.empty()) v = info.fallback;
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t i = 0; i < info.choices.size(); ++i)
      if (v == info.choices[i].value) return static_cast<int>(i);
    if (is_number(v) && is_number(info.choices[0].value)) {
      const double want = _wtof(v.c_str());
      int at = 0;
      for (size_t i = 1; i < info.choices.size(); ++i)
        if (std::fabs(_wtof(info.choices[i].value) - want) < std::fabs(_wtof(info.choices[at].value) - want)) at = static_cast<int>(i);
      return at;
    }
    v = info.fallback;  // (a word the mod does not know: it plays with its default)
  }
  return 0;
}

// РАЗРЕШЕНИЕ: "by the monitor", "as the game's own options say", then the monitor's modes, and what the
// file has if it is none of them.
void make_resolutions(const std::wstring& now) {
  std::vector<std::pair<int, int>> sizes;
  if (g.canned) {
    sizes = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
  } else {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i)
      if (dm.dmBitsPerPel == 32 && dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480)
        sizes.push_back({static_cast<int>(dm.dmPelsWidth), static_cast<int>(dm.dmPelsHeight)});
  }
  int w = 0, h = 0;
  const bool own = swscanf_s(now.c_str(), L"%dx%d", &w, &h) == 2 && w >= 320 && h >= 240 && w <= 16384 && h <= 16384;
  if (own) sizes.push_back({w, h});
  std::sort(sizes.begin(), sizes.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
    return a.first * a.second < b.first * b.second || (a.first * a.second == b.first * b.second && a.first < b.first);
  });
  sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
  g.res.clear();
  g.res.push_back({L"Как монитор", L"auto"});
  g.res.push_back({L"Как в ОПЦИЯХ игры", L"game"});
  g.cur[kResolution] = now == L"game" ? 1 : 0;
  for (const auto& s : sizes) {
    if (own && s.first == w && s.second == h) g.cur[kResolution] = static_cast<int>(g.res.size());
    g.res.push_back({std::to_wstring(s.first) + L" × " + std::to_wstring(s.second),
                     std::to_wstring(s.first) + L"x" + std::to_wstring(s.second)});
  }
}

void read_file() {
  g.mod = g.canned || (!g.dir.empty() && core::exists(g.ini));
  for (int r = 0; r < kRows; ++r) {
    const Info& info = kInfo[r];
    const std::wstring v = g.mod && !g.canned ? get(info.section, info.ini) : std::wstring();
    if (r == kResolution)
      make_resolutions(v);
    else
      g.cur[r] = index_of(info, v);
  }
}

}  // namespace

void load(const std::wstring& game_dir) {
  g.canned = false;
  g.error.clear();
  if (g.dir != game_dir) g.kept = false;
  g.dir = game_dir;
  g.ini = game_dir.empty() ? std::wstring() : seen(game_dir);
  read_file();
}

void load_canned() {
  g = State{};
  g.canned = true;
  read_file();
}

bool has_mod() { return g.mod; }
Kind kind(Row r) { return kInfo[r].kind; }
const wchar_t* label(Row r) { return kInfo[r].label; }
const char* key(Row r) { return kInfo[r].key; }

bool usable(Row r) {
  if (!g.mod) return false;
  // "the game's own full screen": the mod leaves the picture alone, its frame is the game's
  const bool own_screen = std::wstring(kInfo[kMode].choices[static_cast<size_t>(g.cur[kMode])].value) == L"game";
  if (own_screen && (r == kResolution || r == kAspect || r == kAntialias)) return false;
  return true;
}

int count(Row r) { return r == kResolution ? static_cast<int>(g.res.size()) : static_cast<int>(kInfo[r].choices.size()); }
int value(Row r) { return g.cur[r]; }

std::wstring name(Row r, int v) {
  if (v < 0 || v >= count(r)) return L"";
  return r == kResolution ? g.res[static_cast<size_t>(v)].name : kInfo[r].choices[static_cast<size_t>(v)].name;
}

bool set(Row r, int v) {
  const int n = count(r);
  if (!usable(r) || n <= 0) {
    g.error = L"Эта настройка сейчас не действует.";
    return false;
  }
  v = ((v % n) + n) % n;  // (the arrows go round)
  const std::wstring value = r == kResolution ? g.res[static_cast<size_t>(v)].value : kInfo[r].choices[static_cast<size_t>(v)].value;
  if (!put(kInfo[r].section, kInfo[r].ini, value)) return false;
  g.cur[r] = v;
  return true;
}

bool reset(int page) {
  const int from = page == 1 ? 0 : kFirstNet, to = page == 1 ? kFirstNet : kRows;
  if (!g.mod) return true;
  for (int r = from; r < to; ++r) {
    const Info& info = kInfo[r];
    if (!put(info.section, info.ini, info.fallback)) return false;
  }
  read_file();
  return true;
}

const std::wstring& error() { return g.error; }
std::wstring ini_file() { return g.ini; }

}  // namespace gset
}  // namespace lz
