#include "mod/netpage.h"

#include <windows.h>

#include <string>
#include <vector>

#include "common/log.h"
#include "mod/gameui.h"
#include "mod/input.h"
#include "mod/netlobby.h"

namespace netpage {
namespace {

using netlobby::State;

Config g_cfg;
int g_friend = 0;          // which friend the invite row shows
std::string g_address;     // the address row's text
std::wstring g_note;       // a word about the last key (the info frame's last line)
bool g_had_guest = false;

enum class Act { None, Create, Join, Address, Start, Invite, Leave };

struct Row {
  std::string text;  // in the game's lettering
  Act act = Act::None;
  uint64_t id = 0;
};

std::string L(const std::wstring& s) { return gameui::letters(s); }
std::string N(const std::string& utf8) { return gameui::name(utf8); }

std::wstring wide(const std::string& utf8) {
  if (utf8.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &w[0], n);
  return w;
}

std::vector<Row> rows() {
  const netlobby::View& v = netlobby::view();
  std::vector<Row> r;
  switch (v.state) {
    case State::Menu: {
      r.push_back({L(L"СОЗДАТЬ ИГРУ"), Act::Create});
      int shown = 0;
      for (const netlobby::Game& g : v.games) {
        if (shown++ >= 3) break;
        if (g.ok)
          r.push_back({L(L"ИГРА: ") + N(g.host), Act::Join, g.lobby});
        else
          r.push_back({N(g.host) + ": " + gameui::letters(g.why), Act::None});
      }
      r.push_back({L(L"АДРЕС: ") + g_address + "_", Act::Address});
      break;
    }
    case State::Hosting:
      r.push_back({L(L"НАЧАТЬ ИГРУ"), Act::Start});
      if (v.steam && !v.friends.empty()) {
        if (g_friend >= static_cast<int>(v.friends.size())) g_friend = 0;
        const netlobby::Friend& f = v.friends[static_cast<size_t>(g_friend)];
        r.push_back({L(L"ПРИГЛАСИТЬ: ") + N(f.name), Act::Invite, f.id});
      }
      r.push_back({L(L"ЗАКРЫТЬ ИГРУ"), Act::Leave});
      break;
    case State::Joining:
      r.push_back({L(L"ОТМЕНА"), Act::Leave});
      break;
    case State::Joined:
      r.push_back({L(L"ВЫЙТИ ИЗ ИГРЫ"), Act::Leave});
      break;
    default:
      break;
  }
  return r;
}

// The words in the frame on the right. {n} is the engine's new line.
std::string info() {
  const netlobby::View& v = netlobby::view();
  std::string s;
  switch (v.state) {
    case State::Menu:
      s = v.steam ? L(L"СТИМ:") + "{n}" + N(v.me) : L(L"СТИМ НЕ ЗАПУЩЕН");
      s += "{n}{n}";
      if (v.steam && v.games.empty()) s += L(L"ИГРЫ ДРУЗЕЙ ПОЯВЯТСЯ В СПИСКЕ") + "{n}{n}";
      s += L(L"БЕЗ СТИМА: НАБЕРИТЕ АДРЕС ХОСТА И НАЖМИТЕ ВВОД");
      break;
    case State::Hosting:
      s = L(L"ИГРОК 1:") + "{n}" + N(v.me) + "{n}{n}" + L(L"ИГРОК 2:") + "{n}" + (v.other_in ? N(v.other) : L(L"ЖДЁМ..."));
      if (!v.invited.empty()) s += "{n}{n}" + L(L"ПРИГЛАШЁН:") + "{n}" + N(v.invited);
      if (!g_note.empty()) s += "{n}{n}{c=FFFFFF00}" + L(g_note) + "{~:c}";
      if (!v.addresses.empty()) s += "{n}{n}" + L(L"АДРЕС ИГРЫ:") + "{n}" + v.addresses;
      if (!v.error.empty()) s += "{n}{n}{c=FFFFFF00}" + gameui::letters(v.error) + "{~:c}";
      return s;
    case State::Joining:
      s = L(L"ПОДКЛЮЧЕНИЕ...");
      break;
    case State::Joined:
      s = L(L"ИГРОК 1:") + "{n}" + N(v.other) + "{n}{n}" + L(L"ИГРОК 2:") + "{n}" + N(v.me) + "{n}{n}" + L(L"ЖДЁМ НАЧАЛА ИГРЫ");
      break;
    case State::Starting:
      s = L(L"ИГРА ПЕРЕЗАПУСКАЕТСЯ...") + "{n}{n}" + L(L"ЧЕРЕЗ НЕСКОЛЬКО СЕКУНД ОНА ОТКРОЕТСЯ В СЕТЕВОЙ ИГРЕ");
      break;
    default:
      break;
  }
  if (!v.error.empty()) s += "{n}{n}{c=FFFFFF00}" + gameui::letters(v.error) + "{~:c}";
  if (!g_note.empty()) s += "{n}{n}{c=FFFFFF00}" + L(g_note) + "{~:c}";
  return s;
}

// What the page says now goes to the game's objects (only what changed).
std::string g_said_rows[gameui::kRows], g_said_info;
bool g_said = false;

void refresh() {
  if (!gameui::page_up()) return;
  std::vector<Row> r = rows();
  const netlobby::View& v = netlobby::view();
  if (v.state == State::Hosting && v.other_in && !g_had_guest) gameui::focus_row(0);  // the second player is in: the next key is the start
  g_had_guest = v.other_in;
  int focus = gameui::focused_row();
  if (!r.empty() && (focus < 0 || focus >= static_cast<int>(r.size()))) gameui::focus_row(0);  // (its row is about to go)
  for (int i = 0; i < gameui::kRows; ++i) {
    std::string s = i < static_cast<int>(r.size()) ? r[static_cast<size_t>(i)].text : std::string();
    if (g_said && s == g_said_rows[i]) continue;
    g_said_rows[i] = s;
    gameui::set_row(i, s);
  }
  std::string words = info();
  if (!g_said || words != g_said_info) {
    g_said_info = words;
    gameui::set_info(words);
  }
  g_said = true;
}

void shown() {
  g_said = false;
  g_note.clear();
  g_had_guest = false;
  netlobby::open();
  refresh();
}

void left() { netlobby::close(); }

void chosen(int index) {
  std::vector<Row> r = rows();
  if (index < 0 || index >= static_cast<int>(r.size())) return;
  const Row& row = r[static_cast<size_t>(index)];
  g_note.clear();
  switch (row.act) {
    case Act::Create:
      netlobby::host();
      break;
    case Act::Join:
      netlobby::join_game(row.id);
      break;
    case Act::Address:
      if (g_address.empty()) {
        g_note = L"НАБЕРИТЕ АДРЕС ХОСТА";
      } else {
        WritePrivateProfileStringW(L"net", L"connect", std::wstring(g_address.begin(), g_address.end()).c_str(),
                                   (module_dir() + L"in2net.ini").c_str());
        netlobby::join_address(g_address);
      }
      break;
    case Act::Start:
      if (netlobby::view().other_in)
        netlobby::start();
      else
        g_note = L"ВТОРОГО ИГРОКА ЕЩЁ НЕТ";
      break;
    case Act::Invite:
      netlobby::invite(row.id);
      break;
    case Act::Leave:
      netlobby::leave();
      break;
    default:
      break;
  }
  refresh();
}

Act focused_act() {
  std::vector<Row> r = rows();
  int f = gameui::focused_row();
  return f >= 0 && f < static_cast<int>(r.size()) ? r[static_cast<size_t>(f)].act : Act::None;
}

}  // namespace

void install(const Config& cfg) {
  g_cfg = cfg;
  if (cfg.net_connect != "127.0.0.1") g_address = cfg.net_connect;
  gameui::Events ev;
  ev.shown = shown;
  ev.left = left;
  ev.chosen = chosen;
  gameui::on_events(ev);
}

bool active() { return gameui::page_up() && netlobby::view().state == State::Starting; }

bool on_key(unsigned vk) {
  if (!gameui::page_up()) return false;
  // The page is walked and chosen from by the game itself; only what the game's menus have no
  // use for is the mod's: left and right pick the friend to invite, Backspace rubs the address out.
  if ((vk == VK_LEFT || vk == VK_RIGHT) && focused_act() == Act::Invite) {
    size_t n = netlobby::view().friends.size();
    if (n) g_friend = static_cast<int>((static_cast<size_t>(g_friend) + (vk == VK_LEFT ? n - 1 : 1)) % n);
    refresh();
  } else if (vk == VK_BACK && focused_act() == Act::Address && !g_address.empty()) {
    g_address.pop_back();
    refresh();
  }
  return false;
}

bool on_char(wchar_t ch) {
  if (!gameui::page_up() || focused_act() != Act::Address) return false;
  if (g_address.size() < 40 &&
      ((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'.' || ch == L':' || ch == L'-')) {
    g_address += static_cast<char>(ch);
    refresh();
    return true;
  }
  return false;
}

void tick() {
  // [debug] script: "<frame> key.<name>" / "<frame> type.<text>" lines are the keys the window would get
  unsigned vk = 0;
  while (input::script_key(vk)) {
    if (vk >= 0x100)
      on_char(static_cast<wchar_t>(vk - 0x100));
    else
      on_key(vk);
  }
  gameui::poll();
  if (!gameui::page_up()) return;
  netlobby::tick();
  refresh();
}

}  // namespace netpage
