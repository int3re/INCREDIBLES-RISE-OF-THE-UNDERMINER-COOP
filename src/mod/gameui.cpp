#include "mod/gameui.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/log.h"
#include "mod/game.h"
#include "mod/patch.h"

namespace gameui {
namespace {

Config g_cfg;
Events g_events;

constexpr uint32_t kDyna = 0x44594E41, kGrup = 0x47525550;  // asset types, as the engine names them

// The game's events used here (its numbers).
constexpr uint32_t kEvVisible = 3, kEvInvisible = 4, kEvSelect = 0x3F, kEvFocusOn = 0x4F, kEvFocus = 0x5E, kEvUnfocus = 0x5F,
                   kEvSetText = 0x14E, kEvSetVar = 0x218;

using Count_t = int(__cdecl*)(uint32_t type);
using FindByType_t = void*(__cdecl*)(uint32_t type, int index, uint32_t* size);
using Find_t = void*(__cdecl*)(uint32_t id, uint32_t* size);
using Event_t = void(__cdecl*)(void* from, uint32_t from_event, void* to, uint32_t to_event, const float* params, void* widget,
                               uint32_t widget_id, int force);
Count_t oCount = nullptr;
FindByType_t oFindByType = nullptr;
Find_t oFind = nullptr;

// The engine's name hash (asset ids are hashes of asset names).
uint32_t hash(const std::string& s) {
  uint32_t v = 0;
  for (char ch : s) {
    uint32_t c = static_cast<unsigned char>(ch);
    c -= c & (c >> 1) & 0x20;  // upper case
    v = v * 131 + c;
  }
  return v;
}

uint32_t rd32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
void wr32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
void wrf(uint8_t* p, float v) { memcpy(p, &v, 4); }

// --- the mod's assets ------------------------------------------------------------------------
struct Asset {
  uint32_t id = 0;
  std::vector<uint8_t> data;
};
std::vector<Asset> g_dyna;    // objects the engine is to make ('DYNA': found by type and by id)
std::vector<Asset> g_groups;  // groups the engine is to make ('GRUP')
std::vector<Asset> g_other;   // found by id only: the game's assets the mod has a changed copy of
void* g_built_for = nullptr;  // the menu pack these were made from (its "bonus" label's asset)
bool g_building = false;
bool g_hooked = false;        // every hook is in (a count without its finder would send the engine to nowhere)

// The words: text assets the engine finds by id, at fixed places (the labels keep pointers to them).
enum Slot { kLabel, kTitle, kRow0, kInfo = kRow0 + kRows, kSlots };
struct Words {
  uint32_t id = 0;
  uint8_t buf[4 + 1024] = {};  // u32 length, the characters, a zero
};
Words g_words[kSlots];

void put_words(Slot slot, const std::string& s) {
  Words& w = g_words[slot];
  uint32_t len = static_cast<uint32_t>(s.size() < 1000 ? s.size() : 1000);
  memset(w.buf, 0, sizeof(w.buf));
  wr32(w.buf, len);
  memcpy(w.buf + 4, s.data(), len);
}
uint32_t words_size(const Words& w) { return (4 + rd32(w.buf) + 1 + 3) & ~3u; }

// An asset's links: `count` records of 0x20 bytes at its end.
constexpr size_t kLink = 0x20;
int link_count(const Asset& a) { return a.data[5]; }
uint8_t* link_at(Asset& a, int i) { return a.data.data() + a.data.size() - static_cast<size_t>(link_count(a) - i) * kLink; }

// Links are dropped where `drop(source event, target event, target id)` says so.
template <typename F>
void drop_links(Asset& a, F drop) {
  int n = link_count(a);
  std::vector<uint8_t> kept;
  int left = 0;
  for (int i = 0; i < n; ++i) {
    const uint8_t* l = link_at(a, i);
    uint16_t src, dst;
    memcpy(&src, l, 2);
    memcpy(&dst, l + 2, 2);
    if (drop(src, dst, rd32(l + 4))) continue;
    kept.insert(kept.end(), l, l + kLink);
    ++left;
  }
  a.data.resize(a.data.size() - static_cast<size_t>(n) * kLink);
  a.data.insert(a.data.end(), kept.begin(), kept.end());
  a.data[5] = static_cast<uint8_t>(left);
}

using Names = std::map<uint32_t, uint32_t>;

// A copy of one of the game's assets; every id in `names` (old -> new) is changed wherever it stands.
bool clone(const std::string& from, const Names& names, Asset& out) {
  uint32_t size = 0;
  auto p = static_cast<const uint8_t*>(oFind(hash(from), &size));
  if (!p || size < 0x10) {
    LOGE("ui: the game's asset '%s' is not there", from.c_str());
    return false;
  }
  out.data.assign(p, p + size);
  for (size_t i = 0; i + 4 <= out.data.size(); i += 4) {
    auto it = names.find(rd32(out.data.data() + i));
    if (it != names.end()) wr32(out.data.data() + i, it->second);
  }
  out.id = rd32(out.data.data());
  return true;
}

std::string row_name(int row, const char* what) { return "UIS NETWORK ITEM " + std::to_string(row + 1) + " " + what; }

uint32_t g_screen = 0, g_row_text[kRows] = {}, g_row_box[kRows] = {}, g_info_text = 0, g_info_box = 0, g_main_text = 0;
constexpr uint32_t kTitleObject = 0x4D405624;  // the title frame's words, shared by the game's menu pages

// The menu scene's pack is loaded: the mod's assets are made from the game's own.
bool build() {
  g_dyna.clear();
  g_groups.clear();
  g_other.clear();
  auto add = [](Asset& a) {
    g_dyna.push_back(a);
    a = Asset{};
  };

  // --- names: the БОНУС page's objects -> the network page's ---
  static const char* kRowFrom[kRows] = {"1", "3", "4", "5", "6", "2"};  // (2 and 3 lie on one row in the game; 2 is unused)
  Names names;
  auto rename = [&names](const std::string& from, const std::string& to) { names[hash(from)] = hash(to); };
  rename("UIS PREVIEW SCREEN", "UIS NETWORK SCREEN");
  rename("UIS PREVIEW GROUP", "UIS NETWORK GROUP");
  rename("UIS PREVIEW CURITEM", "UIS NETWORK CURITEM");
  rename("UIS PREVIEW PIC BOX", "UIS NETWORK INFO BOX");
  rename("UIS PREVIEW SELECT TEXT", "UIS NETWORK SELECT TEXT");
  rename("UIS PREVIEW BACK TEXT", "UIS NETWORK BACK TEXT");
  rename("UIS TXT PREVIEW TITLE", "UIS TXT NETWORK TITLE");
  for (int r = 0; r < kRows; ++r) {
    rename(std::string("UIS PREVIEW ITEM ") + kRowFrom[r] + " BOX", row_name(r, "BOX"));
    rename(std::string("UIS PREVIEW ITEM ") + kRowFrom[r] + " TEXT", row_name(r, "TEXT"));
    rename(std::string("UIS TXT PREVIEW ") + kRowFrom[r], "UIS TXT NETWORK " + std::to_string(r + 1));
    g_row_text[r] = hash(row_name(r, "TEXT"));
    g_row_box[r] = hash(row_name(r, "BOX"));
    g_words[kRow0 + r].id = hash("UIS TXT NETWORK " + std::to_string(r + 1));
  }
  g_screen = hash("UIS NETWORK SCREEN");
  g_info_text = hash("UIS NETWORK INFO TEXT");
  g_info_box = hash("UIS NETWORK INFO BOX");
  g_main_text = hash("UIS MAINM NETWORK TEXT");
  g_words[kLabel].id = hash("UIS TXT MAINM NETWORK");
  g_words[kTitle].id = hash("UIS TXT NETWORK TITLE");
  g_words[kInfo].id = hash("UIS TXT NETWORK INFO");
  const uint32_t pic_image = hash("UIS PREVIEW PIC IMAGE");

  // --- the network page: the БОНУС page's screen, rows, frame and hints over again ---
  Asset a;
  if (!clone("UIS PREVIEW SCREEN", names, a)) return false;
  add(a);
  if (!clone("UIS PREVIEW CURITEM", names, a)) return false;
  add(a);
  for (int r = 0; r < kRows; ++r) {
    Names row = names;
    if (r == kRows - 1) {  // the sixth row lies lower than the game's sixth label: its own steam puffs
      row[hash("_UI_STEAM UII X130 Y175")] = hash("_UI_STEAM UII X130 Y365");
      row[hash("_UI_STEAM UII X170 Y175")] = hash("_UI_STEAM UII X170 Y365");
    }
    Asset box, text;
    if (!clone(std::string("UIS PREVIEW ITEM ") + kRowFrom[r] + " BOX", row, box) ||
        !clone(std::string("UIS PREVIEW ITEM ") + kRowFrom[r] + " TEXT", row, text))
      return false;
    wrf(box.data.data() + 0x14, 135.0f + 45.0f * r);
    wrf(text.data.data() + 0x14, 140.0f + 45.0f * r);
    wr32(text.data.data() + 0x38, g_row_text[(r + kRows - 1) % kRows]);  // above
    wr32(text.data.data() + 0x3C, g_row_text[(r + 1) % kRows]);          // below
    drop_links(text, [&](uint16_t src, uint16_t dst, uint32_t to) {
      // the preview's picture and film; choosing a row is the mod's business
      return to == pic_image || (src == kEvSelect && (dst == 0x126 || dst == kEvFocusOn));
    });
    for (int i = 0; i < link_count(text); ++i) {  // "the page's current item is me" (one of the game's names another row)
      uint8_t* l = link_at(text, i);
      uint16_t src, dst;
      memcpy(&src, l, 2);
      memcpy(&dst, l + 2, 2);
      if (src == kEvFocusOn && dst == kEvSetVar) wr32(l + 0x18, g_row_text[r]);
    }
    add(box);
    add(text);
  }
  if (!clone("UIS PREVIEW PIC BOX", names, a)) return false;
  wrf(a.data.data() + 0x20, 261.0f);  // as tall as the six rows
  add(a);
  {
    Names info = names;
    info[hash("UIS PREVIEW SELECT TEXT")] = g_info_text;
    if (!clone("UIS PREVIEW SELECT TEXT", info, a)) return false;
    wrf(a.data.data() + 0x10, 400.0f);
    wrf(a.data.data() + 0x14, 144.0f);
    wrf(a.data.data() + 0x1C, 155.0f);
    wrf(a.data.data() + 0x20, 243.0f);
    wr32(a.data.data() + 0x50, g_words[kInfo].id);
    a.data[0x5E] = 0;  // the words start at the frame's top
    if (g_cfg.ui_font >= 0 && g_cfg.ui_font < 9) a.data[0x54] = static_cast<uint8_t>(g_cfg.ui_font);
    add(a);
  }
  if (!clone("UIS PREVIEW SELECT TEXT", names, a)) return false;
  add(a);
  if (!clone("UIS PREVIEW BACK TEXT", names, a)) return false;
  add(a);

  // what is shown and hidden together as the page comes and goes
  {
    uint32_t size = 0;
    auto p = static_cast<const uint8_t*>(oFind(hash("UIS PREVIEW GROUP"), &size));
    if (!p) return false;
    std::vector<uint32_t> members;
    for (int r = 0; r < kRows; ++r) {
      members.push_back(g_row_box[r]);
      members.push_back(g_row_text[r]);
    }
    members.push_back(hash("UIS NETWORK INFO BOX"));
    members.push_back(g_info_text);
    members.push_back(hash("UIS NETWORK SELECT TEXT"));
    members.push_back(hash("UIS NETWORK BACK TEXT"));
    for (uint32_t shared : {0x066F186Cu, kTitleObject, 0x1AD6324Bu}) members.push_back(shared);  // the pages' common frame and title
    Asset g;
    g.data.assign(p, p + 12);
    g.id = hash("UIS NETWORK GROUP");
    wr32(g.data.data(), g.id);
    uint16_t n = static_cast<uint16_t>(members.size());
    memcpy(g.data.data() + 8, &n, 2);
    for (uint32_t m : members) {
      g.data.resize(g.data.size() + 4);
      wr32(g.data.data() + g.data.size() - 4, m);
    }
    g_groups.push_back(g);
  }

  // --- СЕТЕВАЯ ИГРА, the seventh button of the main menu: АВТОРЫ's frame and label, a row under БОНУС ---
  {
    const uint32_t bonus_text = hash("UIS MAINM BONUS TEXT"), first_text = hash("UIS MAINM NEW GAME 1PLYR TEXT");
    const uint32_t net_box = hash("UIS MAINM NETWORK BOX");
    Names main = {{hash("UIS MAINM CREDITS TEXT"), g_main_text},
                  {hash("UIS MAINM CREDITS BOX"), net_box},
                  {hash("UIS CREDITS SCREEN"), g_screen}};  // chosen: the network page comes up, as АВТОРЫ brings its own
    Asset box, text;
    if (!clone("UIS MAINM CREDITS BOX", main, box) || !clone("UIS MAINM CREDITS TEXT", main, text)) return false;
    wrf(box.data.data() + 0x14, 375.0f);
    wrf(text.data.data() + 0x14, 380.0f);
    wr32(text.data.data() + 0x38, bonus_text);  // above
    wr32(text.data.data() + 0x3C, first_text);  // below: round to the first
    wr32(text.data.data() + 0x50, g_words[kLabel].id);
    const uint32_t steam_a = hash("_UI_STEAM UII X380 Y280"), steam_b = hash("_UI_STEAM UII X460 Y280");
    drop_links(text, [&](uint16_t, uint16_t, uint32_t to) { return to == steam_a || to == steam_b; });  // АВТОРЫ's own row's puffs
    add(box);
    add(text);
    // БОНУС's "below" and the first button's "above" lead to the new one.
    if (auto p = static_cast<uint8_t*>(oFind(bonus_text, nullptr))) wr32(p + 0x3C, g_main_text);
    if (auto p = static_cast<uint8_t*>(oFind(first_text, nullptr))) wr32(p + 0x38, g_main_text);
    // The main menu's group with the two more.
    uint32_t size = 0;
    if (auto p = static_cast<const uint8_t*>(oFind(hash("UIS MAINM GROUP"), &size))) {
      Asset g;
      g.id = rd32(p);
      g.data.assign(p, p + size);
      uint16_t n;
      memcpy(&n, p + 8, 2);
      uint16_t more = static_cast<uint16_t>(n + 2);
      memcpy(g.data.data() + 8, &more, 2);
      g.data.resize(size + 8);
      wr32(g.data.data() + size, net_box);
      wr32(g.data.data() + size + 4, g_main_text);
      g_other.push_back(g);
    }
  }
  LOGI("ui: the menu scene's pack is loaded: %u objects and %u groups of the mod's go in with it", (unsigned)g_dyna.size(),
       (unsigned)g_groups.size());
  return true;
}

// True while the menu scene's pack is loaded (the mod's assets are then made for it).
bool ready() {
  if (g_building || !g_hooked) return false;
  static const uint32_t mark_id = hash("UIS MAINM BONUS TEXT");
  void* mark = oFind(mark_id, nullptr);
  if (!mark) {
    g_built_for = nullptr;
    return false;
  }
  if (mark != g_built_for) {
    g_building = true;
    bool ok = build();
    g_building = false;
    g_built_for = ok ? mark : nullptr;
    if (!ok) {
      g_dyna.clear();
      g_groups.clear();
      g_other.clear();
    }
  }
  return g_built_for != nullptr;
}

void* mine(uint32_t id, uint32_t* size) {
  for (std::vector<Asset>* list : {&g_dyna, &g_groups, &g_other})
    for (Asset& a : *list)
      if (a.id == id) {
        if (size) *size = static_cast<uint32_t>(a.data.size());
        return a.data.data();
      }
  for (Words& w : g_words)
    if (w.id == id) {
      if (size) *size = words_size(w);
      return w.buf;
    }
  return nullptr;
}

int __cdecl hkCount(uint32_t type) {
  int n = oCount(type);
  if (type == kDyna && ready()) n += static_cast<int>(g_dyna.size());
  if (type == kGrup && ready()) n += static_cast<int>(g_groups.size());
  return n;
}

void* __cdecl hkFindByType(uint32_t type, int index, uint32_t* size) {
  if ((type == kDyna || type == kGrup) && ready()) {
    std::vector<Asset>& list = type == kDyna ? g_dyna : g_groups;
    int real = oCount(type);
    if (index >= real && index - real < static_cast<int>(list.size())) {
      Asset& a = list[static_cast<size_t>(index - real)];
      if (size) *size = static_cast<uint32_t>(a.data.size());
      return a.data.data();
    }
  }
  void* p = oFindByType(type, index, size);
  if (p && type == kGrup && ready())  // a group the mod has a longer copy of
    if (void* own = mine(rd32(static_cast<const uint8_t*>(p)), size)) return own;
  return p;
}

void* __cdecl hkFind(uint32_t id, uint32_t* size) {
  if (!g_building && id && g_built_for)
    if (void* own = mine(id, size)) return own;
  return oFind(id, size);
}

// --- the scene's objects -----------------------------------------------------------------------
void* object(uint32_t id) {
  auto table = patch::read<const uint32_t*>(game::kObjectTable);
  uint32_t size = patch::read<uint32_t>(game::kObjectTableSize);
  if (!table || !size || !id) return nullptr;
  uint32_t i = (size - 1) & id;
  for (uint32_t n = 0; n < size; ++n) {
    uint32_t at = table[i * 2];
    if (at == id) return reinterpret_cast<void*>(table[i * 2 + 1]);
    if (!at) return nullptr;
    if (++i == size) i = 0;
  }
  return nullptr;
}

void send(void* to, uint32_t event, uint32_t widget_id = 0) {
  if (!to) return;
  reinterpret_cast<Event_t>(game::kEntEvent)(nullptr, 0, to, event, nullptr, nullptr, widget_id, 0);
}

// A label's words. The engine keeps the layout of a text it has drawn and finds it again by the text's
// content - and the layout points into the text. So words, once given to a label, must stay where
// they are and as they are for ever: every different string gets a place of its own, kept for good
// (a row that took another row's words from a buffer written over showed that row's new words).
const char* keep(const std::string& s) {
  static std::map<std::string, std::unique_ptr<char[]>> kept;
  auto it = kept.find(s);
  if (it != kept.end()) return it->second.get();
  auto p = std::make_unique<char[]>(s.size() + 1);
  memcpy(p.get(), s.c_str(), s.size() + 1);
  const char* out = p.get();
  kept.emplace(s, std::move(p));
  return out;
}

// (a "ui:text" object: +0x78 its words)
void give_words(void* label, const std::string& s) {
  if (label) wr32(static_cast<uint8_t*>(label) + 0x78, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(keep(s))));
}

bool g_page = false;
int g_focus = -1;
bool g_row_on[kRows] = {true, true, true, true, true, true};

void show_row(int row, bool on) {
  if (!g_page || g_row_on[row] == on) return;
  g_row_on[row] = on;
  send(object(g_row_box[row]), on ? kEvVisible : kEvInvisible);
  send(object(g_row_text[row]), on ? kEvVisible : kEvInvisible);
}

// --- the engine sends an object's links for an event ---------------------------------------
using SendLinks_t = void(__thiscall*)(void* self, void* from, uint32_t event);
SendLinks_t oSendLinks = nullptr;

void __fastcall hkSendLinks(void* self, void* /*edx*/, void* from, uint32_t event) {
  uint32_t id = g_built_for ? rd32(static_cast<const uint8_t*>(self) + 4) : 0;
  int row = -1;
  if (id)
    for (int r = 0; r < kRows; ++r)
      if (id == g_row_text[r]) row = r;
  if (g_cfg.ui_log && id && (id == g_screen || id == g_main_text || row >= 0) && event != 0x22B && event != 0x22C)
    LOGI("ui: %s%d: links for event %u (0x%X)", id == g_screen ? "the network page " : id == g_main_text ? "the main menu's button " : "row ",
         row, event, event);
  oSendLinks(self, from, event);
  if (row >= 0 && g_page) {
    if (event == kEvFocusOn) g_focus = row;
    if (event == kEvSelect && g_events.chosen) g_events.chosen(row);
  }
}

}  // namespace

void on_events(const Events& ev) { g_events = ev; }

void poll() {
  // (an object: +0x5C "is shown")
  auto box = g_built_for ? static_cast<const uint8_t*>(object(g_info_box)) : nullptr;
  bool up = box && box[0x5C] != 0;
  if (up == g_page) return;
  g_page = up;
  if (up) {
    if (g_focus < 0) g_focus = 0;
    for (bool& on : g_row_on) on = true;  // (the page's group has just shown all of them)
    LOGI("ui: the network page is up");
    if (g_events.shown) g_events.shown();
  } else {
    LOGI("ui: the network page is left");
    if (g_events.left) g_events.left();
  }
}
bool page_up() { return g_page && g_built_for; }
int focused_row() { return page_up() ? g_focus : -1; }

void set_title(const std::string& s) {
  put_words(kTitle, s);
  if (page_up()) send(object(kTitleObject), kEvSetText, g_words[kTitle].id);
}

void set_row(int row, const std::string& s) {
  if (row < 0 || row >= kRows || !page_up()) return;
  give_words(object(g_row_text[row]), s);
  show_row(row, !s.empty());
  if (g_cfg.ui_log) LOGI("ui: row %d := \"%s\"", row, s.c_str());
}

void set_info(const std::string& text) {
  std::string s = g_cfg.ui_font == 9   ? "A{f=1}abc f{~:f}B{n}C{font=1}xyz font{~:font}D{n}E{f:1}colon{~:f}F{n}{c=FFFFFF00}YELLOW{~:c} G{n}{font:1}fc{~:font}H"
                  : g_cfg.ui_font >= 0 ? "ABCDEFGHIJKLM NOPQRSTUVWXYZ abcdefghijklm nopqrstuvwxyz 0123456789 .,:;!?-+()_/"
                                       : text;
  if (page_up()) give_words(object(g_info_text), s);
}

void focus_row(int row) {
  if (!page_up() || row < 0 || row >= kRows || row == g_focus) return;
  if (g_focus >= 0) send(object(g_row_text[g_focus]), kEvUnfocus);
  send(object(g_row_text[row]), kEvFocus);
  g_focus = row;
}

std::string letters(const std::wstring& text) {
  // А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я, as the Russian release's font has them
  static const char kCyr[] = "A6BFDEG3NNKLMHOZPCTYVXQ4WWJSJ3UR";
  std::string out;
  for (wchar_t c : text) {
    if (c >= 0x430 && c <= 0x44F) c = static_cast<wchar_t>(c - 0x20);  // small Cyrillic: as capitals
    if (c >= 0x410 && c <= 0x42F)
      out += kCyr[c - 0x410];
    else if (c == 0x401 || c == 0x451)
      out += 'E';
    else if (c >= L'A' && c <= L'Z')
      out += static_cast<char>(c - L'A' + 'a');
    else if (c == L'{' || c == L'}')
      out += c == L'{' ? '(' : ')';  // (braces are the engine's text commands)
    else if (c == 0x2026)
      out += "...";
    else if (c == 0xAB || c == 0xBB || c == 0x201C || c == 0x201D)
      out += '"';
    else if (c == 0x2014 || c == 0x2013)
      out += '-';
    else if (c >= 0x20 && c < 0x7F)
      out += static_cast<char>(c);
    else
      out += '?';
  }
  return out;
}

std::string letters(const std::string& utf8) {
  if (utf8.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &w[0], n);
  return letters(w);
}

std::string name(const std::string& utf8) { return "{f=1}" + letters(utf8) + "{~:f}"; }

void install(const Config& cfg) {
  g_cfg = cfg;
  if (cfg.net_mode != Config::Net::Off) {
    LOGI("ui: this game is in a session: the main menu stays the game's own");
    return;
  }
  put_words(kLabel, letters(L"СЕТЕВАЯ ИГРА"));
  put_words(kTitle, letters(L"СЕТЕВАЯ ИГРА"));
  static const uint8_t count_head[] = {0x51, 0x53, 0x56, 0x33, 0xC0, 0x57};        // push ecx; push ebx; push esi; xor eax,eax; push edi
  static const uint8_t find_head[] = {0x51, 0x8B, 0x44, 0x24, 0x08, 0x85, 0xC0};   // push ecx; mov eax,[esp+8]; test eax,eax
  static const uint8_t type_head[] = {0x83, 0xEC, 0x08};                           // sub esp, 8
  static const uint8_t links_head[] = {0xA0, 0x65, 0x1B, 0x74, 0x00, 0x84, 0xC0};  // mov al,[0x741B65]; test al,al
  if (!patch::bytes_are(game::kAssetCountByType, count_head, sizeof(count_head)) ||
      !patch::bytes_are(game::kAssetFind, find_head, sizeof(find_head)) ||
      !patch::bytes_are(game::kAssetFindByType, type_head, sizeof(type_head)) ||
      !patch::bytes_are(game::kSendLinks, links_head, sizeof(links_head))) {
    LOGE("ui: the engine's asset functions are not where they should be; the mod's menus are off");
    return;
  }
  bool ok = patch::hook("assets of a type: how many", game::kAssetCountByType, (void*)&hkCount, (void**)&oCount);
  ok = patch::hook("an asset by its id", game::kAssetFind, (void*)&hkFind, (void**)&oFind) && ok;
  ok = patch::hook("assets of a type: one of them", game::kAssetFindByType, (void*)&hkFindByType, (void**)&oFindByType) && ok;
  ok = patch::hook("an object's links for an event", game::kSendLinks, (void*)&hkSendLinks, (void**)&oSendLinks) && ok;
  g_hooked = ok;
  if (!ok) LOGE("ui: a hook did not go in; the mod's menus are off");
}

}  // namespace gameui
