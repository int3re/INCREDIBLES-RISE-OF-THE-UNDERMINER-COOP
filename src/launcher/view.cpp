// The launcher's picture: every screen, drawn from the model (a 960 x 600 window: header 48, body 508,
// footer 44), in the look of the heroes' suit. The body is the suit's field - red, or the colour the
// player picked; what is black on the suit is black here: the bar above, the bar below, the leaning
// panel of the news, the boxes of the settings, with an orange seam where black meets the field. What
// shines is the sign's yellow running down to orange, in letters with a black outline. The shapes are
// round - ovals, discs, pills - and whatever leans, leans one way.
#include <algorithm>
#include <cmath>

#include "launcher/gameset.h"
#include "launcher/keys.h"
#include "launcher/launcher.h"

namespace lz {
namespace view {
namespace {

using gfx::Canvas;
using gfx::Font;
using gfx::Matrix;
using gfx::TextOpt;

// CREAM: words on black and on the field; GOLD, FLAME: the sign's yellow and orange (GOLD_HI, FLAME_HI:
// under the pointer); INK: words on them; COAL: a box on the field; LINE, EDGE: a box's edge, quiet and plain.
const uint32_t BLACK = 0x000000, COAL = 0x0F0607, COAL2 = 0x241214, LINE = 0x432527, EDGE = 0x8C6C64, CREAM = 0xFFF4E4,
               TEXT2 = 0xEFDCCD, MUTED = 0xB9A096, DIM = 0x7A6560, GOLD = 0xFFD63C, FLAME = 0xF78F1E, EMBER = 0xC9480C,
               GOLD_HI = 0xFFEA7A, FLAME_HI = 0xFFA840, INK = 0x1B0B03, WAIT = 0xFFD63C, RED = 0xFF6B5A,
               RED_TEXT = 0xFF9C8E, RED_EDGE = 0xD9503F, FIELD = 0x080304, TRACK = 0x2E191B, GOOD = 0x8FE06C;
// The suits a player picks from: the field's colours, from where the light falls to the far corner.
struct Suit {
  uint32_t hi, mid, lo, deep;
};
const Suit kSuits[4] = {{0xEC2C1E, 0xC8171A, 0x8F0C12, 0x57060B},   // red
                        {0x2F88DA, 0x1C62B6, 0x113E80, 0x092450},   // blue
                        {0x51474C, 0x322A2E, 0x1D1719, 0x0F0B0C},   // dark
                        {0x45BCDD, 0x1C8DBA, 0x11648D, 0x093E5C}};  // ice
constexpr float kLean = -10;  // degrees: how far the letters, the bars and the news panel's edge lean
// The news panel's edge: where it is at the body's top and at its bottom.
constexpr float kEdgeTop = 676, kEdgeBottom = 586;

Font D(float size, float spacing = 0) { return {gfx::Display, size, 400, spacing}; }
Font T(float size, int weight = 400, float spacing = 0) { return {gfx::Text, size, weight, spacing}; }
Font Mo(float size, int weight = 500) { return {gfx::Mono, size, weight, 0}; }

// icons, in a 24 x 24 box
const char kSliders[] = "M4 6h8M18 6h2M4 12h2M12 12h8M4 18h10";
const char kMin[] = "M6 17h12";
const char kClose[] = "M6 6l12 12M18 6L6 18";
const char kArrow[] = "M3 12h17M14 6l6 6-6 6";
const char kEqual[] = "M5 9h14M5 15h14";
const char kCheck[] = "M5 12.5l4.5 4.5L19 7.5";
const char kWarn[] = "M12 4l9 16H3ZM12 10v5M12 17.6v0.4";
const char kSpin[] = "M12 4a8 8 0 1 0 8 8";
const char kDownload[] = "M12 4v11M7 11l5 5 5-5M5 20h14";
const char kPlay[] = "M8 5l11 7-11 7Z";
const char kFolder[] = "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z";
const char kExternal[] = "M13 5h6v6M19 5l-9 9M10 7H5v12h12v-5";
const char kCopy[] = "M9 9h11v11H9ZM5 15H4V4h11v1";
const char kBack[] = "M20 12H5M11 6l-6 6 6 6";
const char kPrev[] = "M14 6l-6 6 6 6";
const char kNext[] = "M10 6l6 6-6 6";
const char kNote[] = "M12 11v6M12 7.4v0.4";  // in a ring of 9 around 12, 12
const char kLock[] = "M7 11V8a5 5 0 0 1 10 0v3M6 11h12v9H6Z";
struct Ctx {
  Canvas& c;
  const Model& m;
  std::vector<Hit>* hits;
  const Suit& suit;
};

void hit(Ctx& x, int id, float l, float t, float w, float h) {
  if (!x.hits) return;
  Matrix inv = x.c.current();
  if (!inv.Invert()) return;
  x.hits->push_back({id, D2D1::RectF(l, t, l + w, t + h), inv});
}

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

std::wstring megabytes(uint64_t bytes) {
  wchar_t buf[32];
  swprintf(buf, 32, L"%.1f", bytes / 1000000.0);
  for (wchar_t* p = buf; *p; ++p)
    if (*p == L'.') *p = L',';
  return buf;
}

// --- what a screen says --------------------------------------------------------------------------

struct Vals {
  bool offer = false, arrow = false, equal = false, has_new = false;
  std::wstring installed, installed_note, latest, latest_note, latest_label;
  uint32_t latest_border = LINE;
  int compat = 0;  // 1 warning (accent), 2 fine (green), 3 waiting, 4 bad (red), 5 plain
  std::wstring compat_text;
  bool notice = false, notice_bad = false;
  std::wstring notice_title, notice_text;
  std::wstring primary, secondary, hint;
  int icon = 0;  // 1 download, 2 play, 3 folder
  bool primary_on = true;
  std::wstring tab, meta, title, link, link_url;
  std::vector<std::wstring> items;
  bool copy = false;
  std::wstring net;
  int net_state = 0;
  std::wstring path;
};

std::wstring proto_note(int protocol) { return protocol ? L"протокол " + std::to_wstring(protocol) : L""; }

void news(const Release& r, Vals& v, const wchar_t* tab) {
  v.tab = tab;
  v.meta = r.version + (r.date.empty() ? L"" : L" · " + r.date);
  v.title = r.title;
  v.items = r.notes;
  if (v.items.empty()) {  // (a release without its description: an old one)
    v.title = L"Версия " + r.version;
    v.items = {L"Что в ней изменилось, эта сборка лаунчера не знает."};
  }
  v.link = L"Все изменения на GitHub";
  v.link_url = std::wstring(kRepoUrl) + L"/releases";
}

void how_it_works(Vals& v) {
  v.tab = L"КАК ЭТО РАБОТАЕТ";
  v.meta = L"in2net";
  v.title = L"Лаунчер мода";
  v.items = {L"Мод лежит внутри лаунчера: ставится в папку игры одной кнопкой.",
             L"Новая версия мода приходит вместе с новым лаунчером: она внутри него.",
             L"Игра по сети — пункт СЕТЕВАЯ ИГРА в главном меню игры."};
  v.link = L"Страница мода на GitHub";
  v.link_url = kRepoUrl;
}

// The notes of the version that is installed: GitHub's or this launcher's if they are of that version,
// else the ones kept from the day it was installed.
void installed_news(const Model& m, Vals& v) {
  if (m.inst.present && m.rel.known && m.rel.version == m.inst.version && !m.rel.notes.empty()) {
    news(m.rel, v, L"В ЭТОЙ ВЕРСИИ");
  } else if (m.inst.present && m.pack.known && m.pack.version == m.inst.version && !m.pack.notes.empty()) {
    news(m.pack, v, L"В ЭТОЙ ВЕРСИИ");
  } else if (m.inst.present && !m.set.notes.empty() && m.set.notes_version == m.inst.version) {
    v.tab = L"В ЭТОЙ ВЕРСИИ";
    v.meta = m.inst.version + L" · установлена";
    v.title = m.set.notes_title;
    v.items = m.set.notes;
    v.link = L"Все изменения на GitHub";
    v.link_url = std::wstring(kRepoUrl) + L"/releases";
  } else {
    how_it_works(v);
  }
}

// What there is to install: GitHub's version when it is newer than the one inside this launcher and its
// file can be had (it is downloaded then), else the one inside.
bool from_github(const Model& m) {
  return m.rel.known && !m.assets_blocked && (!m.pack.known || core::compare_versions(m.rel.version, m.pack.version) > 0);
}
const Release& offer(const Model& m) { return from_github(m) ? m.rel : m.pack; }

Vals vals(const Model& m) {
  Vals v;
  Screen s = m.screen == Screen::Settings ? m.back : m.screen;
  const uint32_t accent = GOLD;
  const Release& t = offer(m);
  const std::wstring inside_line = L"Мод " + (m.pack.known ? m.pack.version : std::wstring(L"?")) + L" — внутри лаунчера";
  const bool download = from_github(m);
  std::wstring have = m.inst.version.empty() ? L"" : m.inst.version;
  std::wstring play_on = have.empty() ? L"ИГРАТЬ" : L"ИГРАТЬ НА " + have;
  v.installed = !m.inst.present ? L"нет" : have.empty() ? L"?" : have;
  v.installed_note = !m.inst.present ? L"мод не установлен" : have.empty() ? L"версия неизвестна" : proto_note(m.inst.protocol);
  v.latest_label = L"НА GITHUB";
  v.latest = m.rel.known ? m.rel.version : L"—";
  v.latest_note = m.rel.known ? proto_note(m.rel.protocol) : L"нет ответа";
  v.path = m.has_game ? m.set.game_dir : L"(папка не выбрана)";
  std::wstring how = download ? (t.pkg_size ? megabytes(t.pkg_size) + L" МБ с GitHub" : std::wstring(L"Файл с GitHub")) +
                                    (t.pkg_sha256.empty() ? L" · в релизе нет контрольной суммы" : L" · после загрузки сверяется SHA-256")
                              : L"Версия " + t.version + L" уже внутри лаунчера: скачивать ничего не нужно.";
  std::wstring checked = m.checked_at.empty() ? L"GitHub: проверено только что"
                                             : L"GitHub: проверено в " + m.checked_at + (m.via.empty() ? L"" : L", " + m.via);
  // The footer's line where an answer of GitHub is not what the screen is about.
  auto github = [&]() {
    if (!m.repo) {
      v.net = inside_line;
    } else if (m.rechecking) {
      v.net = L"GitHub: проверяю";
      v.net_state = 1;
    } else if (m.rel.known && !m.assets_blocked) {
      v.net = checked;
    } else {
      v.net = m.assets_blocked ? L"GitHub: файлы не скачиваются" : L"GitHub: нет ответа";
      v.net_state = 2;
    }
  };
  // The right box, when the version to install is the one inside this launcher.
  auto inside = [&]() {
    if (download || (m.rel.known && m.rel.version == t.version)) return;
    v.latest_label = L"В ЭТОМ ЛАУНЧЕРЕ";
    v.latest = t.version;
    v.latest_note = proto_note(t.protocol);
  };
  switch (s) {
    case Screen::Checking:
      v.offer = v.arrow = true;
      v.latest = L"· · ·";
      v.latest_note = L"жду ответа";
      v.latest_border = EDGE;
      v.compat = 3;
      v.compat_text = L"Спрашиваю у GitHub, какая версия последняя.";
      v.primary = play_on;
      v.icon = 2;
      v.primary_on = m.inst.present;
      v.hint = L"Играть можно сразу: проверка идёт в фоне и запуск не задерживает.";
      installed_news(m, v);
      v.net = L"GitHub: проверяю";
      v.net_state = 1;
      break;
    case Screen::Update:
      v.offer = v.arrow = v.has_new = true;
      v.latest_border = accent;
      inside();
      if (m.inst.protocol && t.protocol && m.inst.protocol != t.protocol) {
        v.compat = 1;
        v.compat_text = L"Протокол новый: с " + have + L" по сети не соединяется. Обновитесь всей командой.";
      } else if (m.inst.protocol && m.inst.protocol == t.protocol) {
        v.compat = 2;
        v.compat_text = L"Протокол тот же: с друзьями на " + have + L" играть можно.";
      } else {
        v.compat = 5;
        v.compat_text = L"Есть версия мода новее установленной.";
      }
      v.primary = L"ОБНОВИТЬ ДО " + t.version;
      v.icon = 1;
      v.secondary = play_on;
      v.hint = how;
      news(t, v, L"ЧТО НОВОГО");
      v.copy = true;
      github();
      break;
    case Screen::Install:
      v.offer = v.arrow = v.has_new = true;
      v.latest_border = accent;
      inside();
      v.compat = 2;
      v.compat_text = L"Файлы игры не меняются: мод ставится рядом и убирается одной кнопкой.";
      v.primary = L"УСТАНОВИТЬ МОД " + t.version;
      v.icon = 1;
      v.hint = how;
      news(t, v, L"ЧТО НОВОГО");
      github();
      break;
    case Screen::Downloading:
      if (m.pending == Pending::Uninstall) installed_news(m, v);
      else news(m.pending == Pending::Launcher ? m.rel : t, v, L"ЧТО НОВОГО");
      v.copy = m.rel.known;
      v.net = !m.total ? L"Мод: из этого лаунчера" : m.stage == 0 ? L"GitHub: идёт загрузка" : L"GitHub: файл получен";
      if (m.pending == Pending::Uninstall) v.net = L"Мод убирается из папки игры";
      break;
    case Screen::Ready:
      v.offer = true;
      if (!m.repo) {  // (nothing to ask: what is installed is what this launcher carries)
        v.equal = true;
        v.latest_label = L"В ЭТОМ ЛАУНЧЕРЕ";
        v.latest = m.pack.known ? m.pack.version : L"—";
        v.latest_note = proto_note(m.pack.protocol);
        v.compat = 2;
        v.compat_text = L"Мод установлен. Новая версия придёт с новым лаунчером.";
        v.net = inside_line;
      } else if (m.unchecked) {
        v.arrow = true;
        v.latest_note = L"не проверялось";
        v.latest_border = EDGE;
        v.compat = 5;
        v.compat_text = L"Проверка при запуске выключена в настройках.";
        v.net = L"GitHub: не проверялось";
        v.net_state = 1;
      } else {
        v.equal = true;
        v.compat = 2;
        v.compat_text = L"У вас последняя версия. Новее на GitHub нет.";
        v.net = checked;
      }
      v.primary = m.launching ? L"ИГРА ЗАПУСКАЕТСЯ" : L"ИГРАТЬ";
      v.icon = 2;
      if (m.repo) v.secondary = m.rechecking ? L"ПРОВЕРЯЮ GITHUB" : L"ПРОВЕРИТЬ ОБНОВЛЕНИЯ";
      v.hint = L"Игра по сети — пункт СЕТЕВАЯ ИГРА в главном меню игры.";
      installed_news(m, v);
      v.copy = m.rel.known;
      if (m.rechecking) {
        v.net = L"GitHub: проверяю";
        v.net_state = 1;
      }
      break;
    case Screen::Offline:
      v.offer = v.arrow = true;
      v.latest_border = EDGE;
      v.compat = 4;
      if (m.assets_blocked) {
        v.latest_note = L"файл недоступен";
        v.compat_text = L"GitHub отвечает, но файлы релиза не отдаёт. Включите прокси в настройках.";
      } else {
        v.compat_text = L"GitHub не отвечает. Какая версия последняя — неизвестно.";
      }
      v.primary = play_on;
      v.icon = 2;
      v.secondary = L"ПОВТОРИТЬ ПРОВЕРКУ";
      v.hint = L"Проверка обновлений игре не нужна: мод работает как раньше.";
      v.tab = L"НЕТ СВЯЗИ";
      v.meta = m.assets_blocked ? L"файлы GitHub не скачиваются" : L"github.com не отвечает";
      v.title = L"Что можно сделать";
      v.items = {L"Повторить позже: лаунчер проверяет обновления при каждом запуске.",
                 L"Включить прокси в настройках — автоматически или свой.",
                 L"Скачать новый лаунчер в браузере: мод уже внутри него."};
      v.link = L"Страница релиза в браузере";
      v.link_url = std::wstring(kRepoUrl) + L"/releases/latest";
      v.net = m.assets_blocked ? L"GitHub: файлы не скачиваются" : L"GitHub: нет ответа";
      v.net_state = 2;
      break;
    case Screen::NoGame:
      v.notice = true;
      v.notice_title = L"Игра не найдена";
      v.notice_text = L"Лаунчер не нашёл игру на этом компьютере. Укажите папку, где лежит incrotu.exe.";
      v.primary = L"ВЫБРАТЬ ПАПКУ ИГРЫ";
      v.icon = 3;
      v.hint = L"Нужна своя копия игры: файлы игры в мод не входят.";
      v.tab = L"ТРИ ШАГА";
      v.meta = L"первый запуск";
      v.title = L"Как это работает";
      v.items = {L"Укажите папку игры.", L"Лаунчер поставит мод: он уже внутри.",
                 L"Запустите игру: в главном меню появится СЕТЕВАЯ ИГРА."};
      v.link = L"Инструкция на GitHub";
      v.link_url = kRepoUrl;
      github();
      if (m.rel.known && !m.rechecking) v.net = L"GitHub: последняя версия " + m.rel.version;
      break;
    case Screen::Running:
      v.notice = true;
      v.notice_title = L"Игра сейчас открыта";
      if (m.pending == Pending::Uninstall) {
        v.notice_text = L"Пока игра открыта, мод из её папки не убрать. Закройте игру — удаление начнётся само.";
        v.primary = L"ЗАКРЫТЬ ИГРУ И УДАЛИТЬ МОД";
        v.net = L"Мод ждёт, пока игра закроется";
        v.net_state = 1;
      } else {
        v.notice_text = L"Версия " + t.version + (m.total ? L" скачана и проверена" : L" готова к установке") +
                        L". Закройте игру — установка начнётся сама.";
        v.primary = L"ЗАКРЫТЬ ИГРУ И ОБНОВИТЬ";
        v.icon = 1;
        v.net = m.total ? L"GitHub: " + t.version + L" скачана и проверена" : L"Мод " + t.version + L" ждёт, пока игра закроется";
      }
      v.secondary = L"ПОЗЖЕ";
      v.hint = L"В сетевой игре остальные останутся без вас: сначала предупредите команду.";
      v.tab = L"ПОЧЕМУ ТАК";
      v.meta = L"incrotu.exe открыт";
      v.title = L"Мод занят игрой";
      v.items = {L"Пока игра открыта, файлы мода в её папке нельзя заменить.",
                 L"Скачивать заново не придётся: осталась только установка.", L"Настройки, клавиши и сохранения останутся."};
      v.link = L"Все изменения на GitHub";
      v.link_url = std::wstring(kRepoUrl) + L"/releases";
      break;
    case Screen::Problem:
    default:
      v.notice = v.notice_bad = true;
      v.notice_title = m.problem_title;
      v.notice_text = m.problem_text;
      v.primary = m.problem_primary;
      v.secondary = m.problem_secondary;
      v.hint = L"Игра и её файлы не тронуты: мод либо стоит прежний, либо не стоит вовсе.";
      if (t.known) news(t, v, L"ЧТО НОВОГО");
      else how_it_works(v);
      v.net = L"Не получилось";
      v.net_state = 2;
      break;
  }
  if (!m.net.empty()) {
    v.net = m.net;
    v.net_state = m.net_state;
  }
  if (m.has_game && !m.exe_ok && (s == Screen::Update || s == Screen::Install || s == Screen::Ready || s == Screen::Checking))
    v.hint = L"incrotu.exe в этой папке не тот, для которого сделан мод (нужно русское издание «Суперсемейка: Подземная "
             L"битва»): с этим мод не включится.";
  if (!m.repo) {  // (no page to send anybody to)
    v.link.clear();
    v.link_url.clear();
    v.copy = false;
  }
  return v;
}

// --- parts ---------------------------------------------------------------------------------------

Matrix lean(float cx, float cy) { return Matrix::Skew(kLean, 0, D2D1::Point2F(cx, cy)); }

// One of the sign's two figures: a head and a body, 2 * h tall around cy.
void figure(Canvas& c, float cx, float cy, float h, uint32_t top, uint32_t bottom) {
  c.ellipse(cx, cy - h * 0.66f, h * 0.30f, h * 0.30f, top);
  c.round_gradient(cx - h * 0.27f, cy - h * 0.24f, h * 0.54f, h * 1.2f, h * 0.14f, top, bottom);
}

// The launcher's sign: an oval that leans, a ring of yellow and orange around black, and in it two
// figures side by side - the two players. s: the oval's half height.
void emblem(Canvas& c, float cx, float cy, float s) {
  const float rx = s * 1.38f;
  c.push(Matrix::Rotation(-12, D2D1::Point2F(cx, cy)));
  c.ellipse(cx, cy, rx + s * 0.09f, s * 1.09f, BLACK);
  c.ellipse_gradient(cx, cy, rx, s, GOLD, EMBER);
  c.ellipse(cx, cy, rx * 0.81f, s * 0.75f, BLACK);
  c.pop();
  c.push(lean(cx, cy));
  figure(c, cx - s * 0.34f, cy, s * 0.55f, GOLD_HI, FLAME);
  figure(c, cx + s * 0.34f, cy, s * 0.55f, CREAM, 0xDFC4AE);
  c.pop();
}

// The suit's field: lit from the upper left, two sweeps of the sign's oval across it, far larger than
// the window.
void field(Canvas& c, const Suit& s) {
  const uint32_t rgb[4] = {s.hi, s.mid, s.lo, s.deep};
  const float pos[4] = {0, 0.45f, 0.82f, 1};
  c.gradient_radial(0, 0, kW, kH, 300, 220, 800, 640, rgb, pos, 4);
  c.push(Matrix::Rotation(-12, D2D1::Point2F(230, 660)));
  c.oval_ring(230, 660, 540, 340, 60, s.lo, 0.36f);
  c.oval_ring(230, 660, 640, 420, 5, s.hi, 0.34f);
  c.pop();
}

// The first screen's ground: the field, and at the right the black panel of the news - its edge leans,
// an orange seam and a thin yellow line run along it - with the sign at its top.
void ground_home(Canvas& c, const Suit& s) {
  field(c, s);
  const float t = 48, b = kH - 44.0f, w = static_cast<float>(kW);
  const D2D1_POINT_2F black[4] = {{kEdgeTop, t}, {w, t}, {w, b}, {kEdgeBottom, b}};
  c.poly(black, 4, BLACK);
  const D2D1_POINT_2F seam[4] = {{kEdgeTop - 9, t}, {kEdgeTop, t}, {kEdgeBottom, b}, {kEdgeBottom - 9, b}};
  c.poly(seam, 4, FLAME);
  const D2D1_POINT_2F thin[4] = {{kEdgeTop - 16, t}, {kEdgeTop - 14, t}, {kEdgeBottom - 14, b}, {kEdgeBottom - 16, b}};
  c.poly(thin, 4, GOLD);
  emblem(c, 812, 110, 42);
}

void header_button(Ctx& x, int id, float bx, const char* icon, uint32_t rgb, bool danger = false) {
  Canvas& c = x.c;
  const bool hover = x.m.hover == id, down = x.m.pressed == id;
  if (hover || down)
    c.ellipse(bx + 22, 22, 17, 17, danger ? RED_EDGE : CREAM, danger ? (down ? 1.0f : 0.9f) : (down ? 0.22f : 0.13f));
  c.icon(icon, bx + 12, 12, 20, danger && hover ? CREAM : rgb, 2.2f);
  if (id == H_Settings) {
    const float dots[3][2] = {{15, 6}, {9, 12}, {17, 18}};
    c.push(Matrix::Scale(20.0f / 24, 20.0f / 24) * Matrix::Translation(bx + 12, 12));
    for (auto& d : dots) c.ring(d[0], d[1], 2.4f, 2.2f, rgb);
    c.pop();
  }
  hit(x, id, bx, 0, 44, 45);
}

void header(Ctx& x) {
  Canvas& c = x.c;
  c.fill(0, 0, kW, 48, BLACK);
  c.fill(0, 45, kW, 3, FLAME);
  emblem(c, 34, 22, 12);
  float tx = 62;
  Font name = D(15, 0.04f);
  c.text_mid(L"IN2NET", name, tx, 22, CREAM);
  tx += c.measure(L"IN2NET", name).width + 10;
  c.text_mid(L"ЛАУНЧЕР", T(11, 600, 0.16f), tx, 22, MUTED);
  header_button(x, H_Settings, 824, kSliders, x.m.screen == Screen::Settings ? GOLD : TEXT2);
  header_button(x, H_Min, 868, kMin, TEXT2);
  header_button(x, H_Close, 912, kClose, TEXT2, true);
}

// The repository's address as it is written for a reader: without "https://".
std::wstring repo_text() {
  std::wstring r = core::repo();
  size_t at = r.find(L"://");
  return at == std::wstring::npos ? r : r.substr(at + 3);
}

void footer(Ctx& x, const Vals& v) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float top = kH - 44.0f, cy = top + 3 + 20.5f;
  c.fill(0, top, kW, 44, BLACK);
  c.fill(0, top, kW, 3, FLAME);
  bool flash = !m.flash.empty();
  const uint32_t dot[3] = {GOOD, WAIT, RED};
  c.ellipse(22, cy, 4, 4, dot[v.net_state % 3]);
  Font f = T(12);
  const std::wstring& line = flash ? m.flash : v.net;
  // the right end: a line of small print
  float right = kW - 22.0f;
  if (m.swapped) {
    const wchar_t* s = L"Новый лаунчер откроется при следующем запуске";
    right -= c.measure(s, f).width;
    c.text_mid(s, f, right, cy, MUTED);
  } else if (m.rel.full && !from_github(m) && !m.rel.launcher_version.empty() && m.screen != Screen::Downloading &&
             core::compare_versions(m.rel.launcher_version, kLauncherVersion) > 0) {
    // (a newer launcher with the same mod; with a newer mod it comes along with the update)
    std::wstring s = L"Вышел лаунчер " + m.rel.launcher_version + L" — обновить";
    TextOpt u;
    u.underline = true;
    float w = c.measure(s, f).width;
    right -= w;
    c.text_mid(s, f, right, cy, m.hover == H_NewLauncher ? CREAM : GOLD, u);
    hit(x, H_NewLauncher, right, top + 6, w, 34);
  } else {
    const wchar_t* s = L"Неофициальный фанатский мод";
    right -= c.measure(s, f).width;
    c.text_mid(s, f, right, cy, MUTED);
  }
  TextOpt one;
  one.ellipsis = true;
  one.max_w = right - 14 - 36;
  c.text_mid(line, f, 36, cy, flash ? CREAM : TEXT2, one);
  float px = 36 + std::min(c.measure(line, f).width, one.max_w) + 14;
  // the repository's address after the line, where a long passing note leaves room for it
  Font mono = Mo(12, 500);
  const std::wstring repo = repo_text();
  float rw = c.measure(repo, mono).width;
  if (m.repo && !repo.empty() && px + 1 + 14 + rw + 14 <= right) {
    c.fill(px, cy - 7, 1, 14, LINE);
    px += 1 + 14;
    c.text_mid(repo, mono, px, cy, m.hover == H_Repo ? CREAM : MUTED);
    hit(x, H_Repo, px, top + 6, rw, 34);
  }
}

// The big button: a pill of the sign's yellow and orange, at its left a black disc with what it does.
// Returns its width.
float primary_button(Ctx& x, const Vals& v, float bx, float by) {
  Canvas& c = x.c;
  Font f = D(17, 0.02f);
  const float tw = c.measure(v.primary, f).width, h = 60;
  const float w = (v.icon ? 8 + 44 + 14 : 30) + tw + 30;
  const bool on = v.primary_on, hover = on && x.m.hover == H_Primary, down = on && x.m.pressed == H_Primary;
  const float oy = by + (down ? 3.0f : 0.0f);
  if (on && !down) c.round_fill(bx - 3, by + 3, w + 6, h + 6, 33, BLACK, 0.40f);  // (its shadow on the field)
  c.round_fill(bx - 3, oy - 3, w + 6, h + 6, 33, BLACK);
  if (on) {
    c.round_gradient(bx, oy, w, h, 30, hover ? GOLD_HI : GOLD, hover ? FLAME_HI : FLAME);
    c.round_fill(bx + 24, oy + 4, w - 48, 3, 1.5f, 0xFFFFFF, 0.38f);  // (the sheen along its upper edge)
  } else {
    c.round_fill(bx, oy, w, h, 30, 0x3B2A2B);
  }
  const float cy = oy + h / 2;
  float ix = bx + 30;
  if (v.icon) {
    const float dx = bx + 8 + 22;
    c.ellipse(dx, cy, 22, 22, on ? BLACK : 0x241819);
    const uint32_t ic = on ? GOLD : MUTED;
    if (v.icon == 1) c.icon(kDownload, dx - 11, cy - 11, 22, ic, 2.6f);
    if (v.icon == 2) c.icon(kPlay, dx - 11, cy - 11, 22, ic, 2, 24, true);
    if (v.icon == 3) c.icon(kFolder, dx - 11, cy - 11, 22, ic, 2.4f);
    ix = bx + 8 + 44 + 14;
  }
  c.text_mid(v.primary, f, ix, cy, on ? INK : MUTED);
  if (on) hit(x, H_Primary, bx - 3, by - 3, w + 6, h + 9);
  return w;
}

// A pill with words on it. style: 0 plain, 1 the chosen one of a row, 2 a dangerous one, 3 a dangerous
// one asked a second time. Returns its width.
float pill_button(Ctx& x, int id, const std::wstring& label, float bx, float by, float h, float pad, float fixed_w = 0,
                  int style = 0, bool enabled = true, float spacing = 0) {
  Canvas& c = x.c;
  Font f = T(13, 700, spacing);
  const float tw = c.measure(label, f).width, w = fixed_w > 0 ? fixed_w : pad + tw + pad, r = h / 2;
  const bool hover = enabled && x.m.hover == id, down = enabled && x.m.pressed == id;
  uint32_t text = CREAM;
  if (style == 1 && enabled) {
    c.round_fill(bx, by, w, h, r, BLACK);
    c.round_gradient(bx + 2, by + 2, w - 4, h - 4, r - 2, hover ? GOLD_HI : GOLD, hover ? FLAME_HI : FLAME);
    text = INK;
  } else if (style == 1) {
    c.round_box(bx, by, w, h, r, LINE, 2, BLACK);
    text = TEXT2;
  } else {
    const uint32_t edge = !enabled ? 0x2B1819 : style >= 2 ? RED_EDGE : hover ? GOLD : LINE;
    c.round_box(bx, by, w, h, r, style == 3 ? 0x5E1810 : down ? COAL2 : hover ? 0x1B0D0E : BLACK, 2, edge);
    text = !enabled ? DIM : style == 2 ? RED_TEXT : CREAM;
  }
  c.text_mid(label, f, bx + (w - tw) / 2, by + h / 2, text);
  if (enabled) hit(x, id, bx, by, w, h);
  return w;
}

void version_box(Ctx& x, float bx, float by, float w, float h, const wchar_t* label, const std::wstring& number,
                 const std::wstring& note, uint32_t edge) {
  Canvas& c = x.c;
  c.round_box(bx, by, w, h, 16, COAL, 2, edge == LINE ? BLACK : edge);
  float tx = bx + 2 + 16, ty = by + 2 + 10;
  Font lab = T(11, 700, 0.12f);
  ty += c.text(label, lab, tx, ty, FLAME) + 4;
  TextOpt num;
  num.line_h = 26 * 1.1f;
  ty += c.text(number, Mo(26, 700), tx, ty, CREAM, num) + 4;
  c.text(note, T(12), tx, ty, MUTED);
}

void notice_box(Ctx& x, const Vals& v, float bx, float by, float w, float* out_h) {
  Canvas& c = x.c;
  const float tx = bx + 2 + 16 + 24 + 12, tw = w - (tx - bx) - 16 - 2;
  Font title = T(16, 700), body = T(13);
  TextOpt wrap;
  wrap.wrap = true;
  wrap.max_w = tw;
  wrap.line_h = 13 * 1.45f;
  float th = c.line_h(title) + 4 + c.measure(v.notice_text, body, wrap).height;
  float h = 2 + 14 + (th < 24 ? 24 : th) + 14 + 2;
  c.round_box(bx, by, w, h, 16, COAL, 2, v.notice_bad ? RED_EDGE : GOLD);
  c.icon(kWarn, bx + 2 + 16, by + 2 + 14, 24, v.notice_bad ? RED_TEXT : GOLD, 2.2f);
  float ty = by + 2 + 14;
  ty += c.text(v.notice_title, title, tx, ty, CREAM) + 4;
  c.text(v.notice_text, body, tx, ty, TEXT2, wrap);
  *out_h = h;
}

void downloading(Ctx& x, float top) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float left = 40, right = 548;
  // the three steps
  const wchar_t* labels[3] = {L"Загрузка", L"Проверка", m.pending == Pending::Uninstall ? L"Удаление"
                                                        : m.pending == Pending::Launcher ? L"Перезапуск" : L"Установка"};
  float sx = left;
  Font lab = T(12, 700);
  for (int i = 0; i < 3; ++i) {
    bool done = i < m.stage, active = i == m.stage;
    float tw = c.measure(labels[i], lab).width;
    float w = 6 + 22 + 8 + tw + 14;
    if (active) {
      c.round_fill(sx, top, w, 34, 17, BLACK);
      c.round_gradient(sx + 2, top + 2, w - 4, 30, 15, GOLD, FLAME);
    } else {
      c.round_fill(sx, top, w, 34, 17, BLACK, done ? 1.0f : 0.55f);
    }
    c.ellipse(sx + 6 + 11, top + 17, 11, 11, active ? BLACK : done ? GOOD : LINE);
    if (done) {
      c.icon(kCheck, sx + 6 + 4, top + 10, 14, INK, 3.2f);
    } else {
      wchar_t n[2] = {static_cast<wchar_t>(L'1' + i), 0};
      Font nf = Mo(11, 700);
      c.text_mid(n, nf, sx + 6 + (22 - c.measure(n, nf).width) / 2, top + 17, active ? GOLD : MUTED);
    }
    c.text_mid(labels[i], lab, sx + 6 + 22 + 8, top + 17, active ? INK : done ? TEXT2 : MUTED);
    sx += w + 8;
  }
  // how far
  float y = top + 34 + 14;
  float share = m.stage > 0 ? 1.0f : m.total ? clamp01(static_cast<float>(static_cast<double>(m.got) / m.total)) : 0.0f;
  int pct = static_cast<int>(share * 100 + 0.5f);
  TextOpt big;
  big.line_h = 44;
  c.push(lean(left, y + 44));
  c.text_sign(std::to_wstring(pct) + L"%", D(38), left + 7, y + 4, BLACK, BLACK, 3, BLACK, big);
  c.text_sign(std::to_wstring(pct) + L"%", D(38), left + 4, y, GOLD_HI, FLAME, 3, BLACK, big);
  c.pop();
  std::wstring stage;
  if (m.stage == 0)
    stage = m.total ? megabytes(m.got) + L" из " + megabytes(m.total) + L" МБ" : std::wstring(L"мод уже в лаунчере");
  else if (m.stage == 1)
    stage = m.total ? L"сверяю SHA-256" : L"проверяю, что внутри";
  else
    stage = m.pending == Pending::Uninstall ? L"убираю файлы мода из папки игры"
            : m.pending == Pending::Launcher ? L"меняю файл лаунчера" : L"ставлю файлы в папку игры";
  Font sf = Mo(13, 500);
  c.text(stage, sf, right - c.measure(stage, sf).width, y + 44 - c.line_h(sf) - 4, CREAM);
  // the gauge: a black pill, filling with the sign's colours; lines of speed run along what is filled
  y += 44 + 12;
  const float tw = right - left, th = 36, r = th / 2;
  c.round_fill(left - 3, y - 3, tw + 6, th + 6, r + 3, BLACK);
  c.round_fill(left, y, tw, th, r, 0x1D0D0E);
  const float lw = std::max(th, tw * share);
  if (share > 0.004f) {
    c.round_gradient(left, y, lw, th, r, GOLD, FLAME);
    const float drift = static_cast<float>(std::fmod(m.time * 46.0, 34.0));
    for (float t = drift; t < lw - r - 12; t += 34) {
      if (t < r - 4) continue;
      c.push(lean(left + t + 6, y + th / 2));
      c.fill(left + t, y + 5, 11, th - 10, 0xFFFFFF, 0.20f);
      c.pop();
    }
    c.round_fill(left + 14, y + 4, lw - 28, 3, 1.5f, 0xFFFFFF, 0.36f);
  }
  // where from, and the way out
  y += th + 3 + 16;
  Font cf = T(13, 700, 0.06f);
  const float cw = 20 + c.measure(L"ОТМЕНА", cf).width + 20;
  TextOpt url;
  url.ellipsis = true;
  url.max_w = right - left - 16 - cw;
  // (the repository's whole address is in the footer: here it would not fit beside the button)
  c.text_mid(m.total ? L"in2net-launcher.exe: с GitHub, мод внутри" : L"in2net-launcher.exe: мод внутри", Mo(12, 500), left, y + 22,
             TEXT2, url);
  pill_button(x, H_Cancel, L"ОТМЕНА", right - cw, y, 44, 20, 0, 0, true, 0.06f);
  y += 44 + 14;
  TextOpt note;
  note.wrap = true;
  note.max_w = right - left;
  note.line_h = 12 * 1.45f;
  c.text(m.pending == Pending::Uninstall ? std::wstring(L"Файлы игры, её сохранения и настройки остаются на месте.")
         : m.pending == Pending::Launcher ? L"На GitHub вышла версия лаунчера " + m.rel.launcher_version +
                                                L": лаунчер обновляет себя и перезапустится сам. Мод в папке игры при этом не меняется."
                                          : std::wstring(L"Перед установкой лаунчер попросит закрыть игру. Настройки и сохранения остаются."),
         T(12), left, y, CREAM, note, 0.85f);
}

// The news, on the black panel under the sign: its heading on a pill of the sign's colours, the notes
// each marked with a small leaning bar. As much of the list is shown as there is room for.
void news_panel(Ctx& x, const Vals& v) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float L = 674, R = kW - 30.0f, W = R - L, top = 172, limit = kH - 44 - 16.0f;
  Font tab_f = D(12, 0.06f), meta_f = Mo(12, 700), title_f = D(16), item_f = T(13), link_f = T(13, 700);
  TextOpt head;
  head.wrap = true;
  head.max_w = W;
  head.line_h = 16 * 1.32f;
  const float mark_w = 12, mark_gap = 9;
  TextOpt item;
  item.wrap = true;
  item.max_w = W - mark_w - mark_gap;
  item.line_h = 13 * 1.45f;
  float y = top;
  // the heading
  const float pw = 14 + c.measure(v.tab, tab_f).width + 14;
  c.round_gradient(L, y, pw, 26, 13, GOLD, FLAME);
  c.text_mid(v.tab, tab_f, L + 14, y + 13, INK);
  y += 26 + 10;
  y += c.text(v.meta, meta_f, L, y, MUTED) + 8;
  y += c.text(v.title, title_f, L, y, CREAM, head) + 12;
  // the notes
  const bool links = !v.link.empty();
  const float below = links ? 12 + 1 + 8 + 28 + (v.copy ? 26.0f : 0.0f) : 0.0f;  // the line, the links
  for (size_t i = 0; i < v.items.size(); ++i) {
    const float ih = c.measure(v.items[i], item_f, item).height + (i ? 9.0f : 0.0f);
    if (y + ih + below > limit) break;
    if (i) y += 9;
    c.push(lean(L + mark_w / 2, y + 9));
    c.round_fill(L, y + 6, mark_w, 7, 2, i & 1 ? FLAME : GOLD);
    c.pop();
    y += c.text(v.items[i], item_f, L + mark_w + mark_gap, y, TEXT2, item);
  }
  if (!links) return;
  y += 12;
  c.fill(L, y, W, 1, LINE);
  y += 1 + 8;
  TextOpt under;
  under.underline = true;
  bool hover = m.hover == H_PaperLink;
  float lw = c.measure(v.link, link_f).width;
  c.text_mid(v.link, link_f, L, y + 14, hover ? CREAM : GOLD, under);
  c.icon(kExternal, L + lw + 6, y + 7, 14, hover ? CREAM : GOLD, 2.6f);
  hit(x, H_PaperLink, L, y, lw + 6 + 14, 28);
  if (v.copy) {
    y += 26;
    const bool copied = m.copied_until != 0;
    const wchar_t* label = copied ? L"Ссылка скопирована" : L"Скопировать ссылку для друзей";
    const float cw = c.measure(label, link_f).width;
    hover = m.hover == H_Copy;
    c.text_mid(label, link_f, L, y + 14, hover ? CREAM : GOLD, under);
    c.icon(copied ? kCheck : kCopy, L + cw + 6, y + 7, 14, hover ? CREAM : GOLD, 2.6f);
    hit(x, H_Copy, L, y, cw + 6 + 14, 28);
  }
}

void hero(Ctx& x, const Vals& v) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float left = 40, right = 548, width = right - left;
  float y = 48 + 22;
  // above the name: what the mod is, on a black pill
  Font eyebrow = T(11, 700, 0.14f);
  const wchar_t* what = L"СЕТЕВОЙ КООП · 2 ИГРОКА";
  c.round_fill(left, y, 14 + c.measure(what, eyebrow).width + 14, 24, 12, BLACK);
  c.text_mid(what, eyebrow, left + 14, y + 12, GOLD);
  y += 24 + 10;
  // the name: a sign of yellow running down to orange, outlined in black, its shadow behind it
  float size = 46;
  while (size > 30 && c.measure(L"СУПЕРСЕМЕЙКА", D(size)).width > width - 24) size -= 2;
  TextOpt l1;
  l1.line_h = 56;
  c.push(lean(left, y + 56));
  c.text_sign(L"СУПЕРСЕМЕЙКА", D(size), left + 8, y + 6, BLACK, BLACK, 3.5f, BLACK, l1);
  c.text_sign(L"СУПЕРСЕМЕЙКА", D(size), left + 4, y, GOLD_HI, FLAME, 3.5f, BLACK, l1);
  c.pop();
  y += 56 + 8;
  // under it the game's second name, on a black bar that leans (two chips fly off its end)
  Font sub = D(19, 0.05f);
  const float bar = 16 + c.measure(L"ПОДЗЕМНАЯ БИТВА", sub).width + 16;
  c.push(lean(left, y + 30));
  c.fill(left, y, bar, 30, BLACK);
  c.fill(left + bar + 5, y, 7, 30, BLACK);
  c.fill(left + bar + 17, y, 3, 30, BLACK);
  c.text_mid(L"ПОДЗЕМНАЯ БИТВА", sub, left + 16, y + 15, CREAM);
  c.pop();
  y += 30 + 22;

  if (m.screen == Screen::Downloading) {
    downloading(x, y);
  } else {
    if (v.offer) {
      const float bw = (width - 28 - 24) / 2;
      float bh = 2 + 10 + c.line_h(T(11, 700)) + 4 + 26 * 1.1f + 4 + c.line_h(T(12)) + 12 + 2;
      version_box(x, left, y, bw, bh, L"УСТАНОВЛЕНО", v.installed, v.installed_note, LINE);
      if (v.arrow) c.icon(kArrow, left + bw + 12, y + bh / 2 - 14, 28, GOLD, 2.8f);
      if (v.equal) c.icon(kEqual, left + bw + 12, y + bh / 2 - 14, 28, CREAM, 2.8f);
      float rx = left + bw + 12 + 28 + 12;
      version_box(x, rx, y, bw, bh, v.latest_label.c_str(), v.latest, v.latest_note, v.latest_border);
      if (v.has_new) {
        Font bf = T(10, 700, 0.12f);
        const float tw = c.measure(L"НОВАЯ", bf).width, ph = 20, pw = 10 + tw + 10;
        const float bx = rx + bw - 14 - pw;
        c.round_fill(bx - 2, y - 11, pw + 4, ph + 4, 12, BLACK);
        c.round_gradient(bx, y - 9, pw, ph, 10, GOLD, FLAME);
        c.text_mid(L"НОВАЯ", bf, bx + 10, y - 9 + ph / 2, INK);
      }
      y += bh + 12;
      if (v.compat) {
        const float cy = y + 11;
        const uint32_t ic = v.compat == 1 ? GOLD : v.compat == 2 ? GOOD : v.compat == 3 ? WAIT : v.compat == 4 ? RED_TEXT : CREAM;
        c.ellipse(left + 11, cy, 11, 11, BLACK);
        if (v.compat == 3) {
          c.push(Matrix::Rotation(static_cast<float>(std::fmod(m.time * 300.0, 360.0)), D2D1::Point2F(left + 11, cy)));
          c.icon(kSpin, left + 4, cy - 7, 14, ic, 3);
          c.pop();
        } else {
          c.icon(v.compat == 2 ? kCheck : v.compat == 5 ? kArrow : kWarn, left + 4, cy - 7, 14, ic, 3);
        }
        TextOpt one;
        one.ellipsis = true;
        one.max_w = width - 32;
        c.text_mid(v.compat_text, T(13, 500), left + 22 + 10, cy, CREAM, one);
      }
      y += 22 + 20;
    } else if (v.notice) {
      float nh = 0;
      notice_box(x, v, left, y, width, &nh);
      y += nh + 20;
    }
    // the buttons
    float pw = primary_button(x, v, left + 3, y);
    if (!v.secondary.empty()) pill_button(x, H_Secondary, v.secondary, left + 3 + pw + 16, y + 6, 48, 22, 0, 0, true, 0.06f);
    y += 60 + 18;
    TextOpt hint;
    hint.wrap = true;
    hint.max_w = width;
    hint.line_h = 12 * 1.45f;
    const bool wrong = m.has_game && !m.exe_ok && v.offer;
    c.text(v.hint, T(12, wrong ? 600 : 400), left, y, wrong ? WAIT : CREAM, hint, wrong ? 1.0f : 0.85f);
  }

  // the game folder, at the bottom
  const float cy = 48 + 508 - 26;
  c.icon(kFolder, left, cy - 9, 18, CREAM, 2.2f);
  Font lab = T(11, 700, 0.12f);
  float px = left + 18 + 10;
  c.text_mid(L"ПАПКА ИГРЫ", lab, px, cy, CREAM, {}, 0.8f);
  px += c.measure(L"ПАПКА ИГРЫ", lab).width + 10;
  Font cf = T(13, 700);
  float cw = c.measure(L"Изменить", cf).width + 16;
  TextOpt path;
  path.ellipsis = true;
  path.max_w = right - px - 10 - cw;
  Font pf = Mo(12, 500);
  float pwid = c.measure(v.path, pf).width;
  if (pwid > path.max_w) pwid = path.max_w;
  c.text_mid(v.path, pf, px, cy, CREAM, path);
  px += pwid + 10;
  TextOpt under;
  under.underline = true;
  c.text_mid(L"Изменить", cf, px + 8, cy, m.hover == H_Folder ? CREAM : GOLD, under);
  hit(x, H_Folder, px, cy - 22, cw, 44);

  news_panel(x, v);
}

// --- settings --------------------------------------------------------------------------------

struct Section {
  float x, y, w, cx, cw, cy;  // the box; where its content starts
};

Section section_begin(Ctx& x, float sx, float sy, float w, float h, const wchar_t* title) {
  Canvas& c = x.c;
  c.round_box(sx, sy, w, h, 16, COAL, 2, BLACK);
  Section s{sx, sy, w, sx + 2 + 16, w - 4 - 32, sy + 2 + 14};
  s.cy += c.text(title, D(13, 0.06f), s.cx, s.cy, GOLD);
  return s;
}

void toggle_row(Ctx& x, int id, const wchar_t* label, bool on, float lx, float ty, float w) {
  Canvas& c = x.c;
  const float cy = ty + 22, bx = lx + w - 52, by = cy - 13;
  c.text_mid(label, T(14), lx, cy, CREAM);
  const bool hover = x.m.hover == id;
  if (on) {
    c.round_fill(bx, by, 52, 26, 13, BLACK);
    c.round_gradient(bx + 2, by + 2, 48, 22, 11, hover ? GOLD_HI : GOLD, hover ? FLAME_HI : FLAME);
  } else {
    c.round_box(bx, by, 52, 26, 13, TRACK, 2, hover ? CREAM : EDGE);
  }
  c.ellipse(bx + (on ? 39 : 13), cy, 8, 8, on ? BLACK : MUTED);
  hit(x, id, bx - 4, ty, 60, 44);
}

// One of the suits to pick: a disc of its colour with the sign's oval on it.
void swatch(Ctx& x, int index, float cx, float cy) {
  Canvas& c = x.c;
  const int id = H_Accent0 + index;
  const bool on = (x.m.set.accent & 3) == index, hover = x.m.hover == id;
  const Suit& s = kSuits[index];
  if (on) c.ellipse(cx, cy, 27, 27, GOLD);
  else if (hover) c.ellipse(cx, cy, 27, 27, CREAM, 0.55f);
  c.ellipse(cx, cy, 24, 24, BLACK);
  c.ellipse_gradient(cx, cy, 21, 21, s.hi, s.lo);
  c.push(Matrix::Rotation(-12, D2D1::Point2F(cx, cy)));
  c.ellipse(cx, cy, 12, 8.6f, BLACK);
  c.ellipse_gradient(cx, cy, 10.6f, 7.4f, GOLD, EMBER);
  c.ellipse(cx, cy, 7.6f, 4.8f, BLACK);
  c.pop();
  hit(x, id, cx - 26, cy - 26, 52, 52);
}

// Back, the title, and the pages' tabs at the right.
void settings_head(Ctx& x, float left, float top) {
  Canvas& c = x.c;
  const Model& m = x.m;
  Font bf = T(13, 700);
  const float bw = 14 + 18 + 8 + c.measure(L"Назад", bf).width + 18;
  const bool hover = m.hover == H_Back;
  c.round_box(left, top, bw, 44, 22, hover ? 0x1B0D0E : BLACK, 2, hover ? GOLD : LINE);
  c.icon(kBack, left + 14, top + 13, 18, CREAM, 2.4f);
  c.text_mid(L"Назад", bf, left + 14 + 18 + 8, top + 22, CREAM);
  hit(x, H_Back, left, top, bw, 44);
  TextOpt tl;
  tl.line_h = 32;
  Font tf = D(27);
  const float tx = left + bw + 22;
  c.push(lean(tx, top + 38));
  c.text_sign(L"НАСТРОЙКИ", tf, tx + 3, top + 6 + 4, BLACK, BLACK, 2.5f, BLACK, tl);
  c.text_sign(L"НАСТРОЙКИ", tf, tx, top + 6, GOLD_HI, FLAME, 2.5f, BLACK, tl);
  c.pop();

  const int tabs = 4;
  const wchar_t* names[tabs] = {L"ЛАУНЧЕР", L"ГРАФИКА", L"УПРАВЛЕНИЕ", L"СЕТЬ"};
  Font nf = D(12, 0.06f);
  float widths[tabs], total = (tabs - 1) * 8;
  for (int i = 0; i < tabs; ++i) {
    widths[i] = 17 + c.measure(names[i], nf).width + 17;
    total += widths[i];
  }
  float px = kW - left - total;
  const float ty = top + 3, th = 38;
  for (int i = 0; i < tabs; ++i) {
    const float w = widths[i];
    const bool on = static_cast<int>(m.tab) == i, over = m.hover == H_Tab0 + i;
    if (on) {
      c.round_fill(px, ty, w, th, th / 2, BLACK);
      c.round_gradient(px + 2, ty + 2, w - 4, th - 4, th / 2 - 2, GOLD, FLAME);
    } else {
      c.round_box(px, ty, w, th, th / 2, over ? 0x1B0D0E : BLACK, 2, over ? GOLD : LINE);
    }
    c.text_mid(names[i], nf, px + 17, ty + th / 2, on ? INK : CREAM);
    hit(x, H_Tab0 + i, px, top, w, 44);
    px += w + 8;
  }
}

// ЛАУНЧЕР: updates, the network, the game and the mod, the launcher's own look.
void page_launcher(Ctx& x, float left, float top) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float col_w = (kW - 80 - 24) / 2.0f, x1 = left, x2 = left + col_w + 24;
  const float cw = col_w - 4 - 32;
  Font tiny = T(12);
  float h3 = c.line_h(D(13, 0.06f)), sm = c.line_h(tiny);
  float h_updates = 2 + 14 + h3 + 6 + 44 + 2 + 44 + 2 + 44 + 10 + 2;
  float h_net = 2 + 14 + h3 + 8 + sm + 8 + 40 + 8 + sm + 8 + 40 + 14 + 2;
  float row1 = h_updates > h_net ? h_updates : h_net;
  float h_game = 2 + 14 + h3 + 6 + 44 + 6 + 40 + 8 + 40 + 14 + 2;
  float h_look = 2 + 14 + h3 + 8 + sm + 8 + 56 + 4 + 44 + 10 + 2;
  float row2 = h_game > h_look ? h_game : h_look;
  // (updates and the way to GitHub: only once the mod has a repository to ask)
  float y1 = top + 44 + 16, y2 = m.repo ? y1 + row1 + 12 : y1;
  Section s{};
  float y = 0;
  if (m.repo) {
    // updates
    s = section_begin(x, x1, y1, col_w, row1, L"ОБНОВЛЕНИЯ");
    y = s.cy + 6;
    toggle_row(x, H_Toggle1, L"Проверять при запуске", m.set.auto_check, s.cx, y, cw);
    y += 44 + 2;
    toggle_row(x, H_Toggle4, L"Обновлять лаунчер при запуске", m.set.auto_self, s.cx, y, cw);
    y += 44 + 2;
    toggle_row(x, H_Toggle2, L"Ставить обновления мода без вопроса", m.set.auto_install, s.cx, y, cw);

    // the network
    s = section_begin(x, x2, y1, col_w, row1, L"СЕТЬ");
    y = s.cy + 8;
    c.text(L"Прокси для GitHub — если он не открывается напрямую", tiny, s.cx, y, MUTED);
    y += sm + 8;
    const wchar_t* modes[3] = {L"Авто", L"Нет", L"Свой"};
    const float seg = (cw - 16) / 3;
    for (int i = 0; i < 3; ++i)
      pill_button(x, H_ProxyAuto + i, modes[i], s.cx + i * (seg + 8), y, 40, 0, seg, m.set.proxy == i ? 1 : 0);
    y += 40 + 8;
    c.text(m.set.proxy == 0 ? L"Авто: как в Windows, а если не выходит — VPN-клиент на этом ПК"
                            : m.set.proxy == 1 ? L"Нет: только напрямую" : L"Адрес своего прокси (HTTP)",
           tiny, s.cx, y, MUTED);
    y += sm + 8;
    bool own = m.set.proxy == 2;
    c.round_box(s.cx, y, cw, 40, 12, FIELD, 2, m.proxy_focus ? GOLD : m.hover == H_ProxyField ? TEXT2 : LINE);
    Font pf = Mo(13, 500);
    if (m.set.proxy_addr.empty() && !m.proxy_focus) {
      c.text_mid(L"адрес:порт", pf, s.cx + 2 + 14, y + 20, MUTED, {}, 0.7f);
    } else {
      TextOpt clip;
      clip.max_w = cw - 32;
      clip.ellipsis = true;
      c.text_mid(m.set.proxy_addr, pf, s.cx + 2 + 14, y + 20, own ? CREAM : MUTED, clip);
      if (m.proxy_focus && std::fmod(m.time, 1.0) < 0.55) {
        float caret = s.cx + 2 + 14 + c.measure(m.set.proxy_addr, pf).width + 1;
        if (caret < s.cx + cw - 14) c.fill(caret, y + 11, 2, 18, GOLD);
      }
    }
    hit(x, H_ProxyField, s.cx, y, cw, 40);
  }

  // the game and the mod
  s = section_begin(x, x1, y2, col_w, row2, L"ИГРА И МОД");
  y = s.cy + 6;
  c.icon(kFolder, s.cx, y + 13, 18, MUTED, 2.2f);
  Font cf = T(13, 700);
  float chw = c.measure(L"Изменить", cf).width + 8;
  TextOpt path;
  path.ellipsis = true;
  path.max_w = cw - 28 - 10 - chw;
  c.text_mid(m.has_game ? m.set.game_dir : L"(папка не выбрана)", Mo(12, 500), s.cx + 28, y + 22, CREAM, path);
  TextOpt under;
  under.underline = true;
  c.text_mid(L"Изменить", cf, s.cx + cw - chw + 4, y + 22, m.hover == H_FolderSettings ? CREAM : GOLD, under);
  hit(x, H_FolderSettings, s.cx + cw - chw, y, chw, 44);
  y += 44 + 6;
  const float half = (cw - 8) / 2;
  pill_button(x, H_OpenLog, L"Открыть журнал", s.cx, y, 40, 0, half);
  pill_button(x, H_MenuLog, L"Журнал встречи", s.cx + half + 8, y, 40, 0, half);
  y += 40 + 8;
  pill_button(x, H_Reinstall, L"Переустановить мод", s.cx, y, 40, 0, half);
  if (m.confirm_uninstall) pill_button(x, H_Uninstall, L"Точно удалить?", s.cx + half + 8, y, 40, 0, half, 3);
  else pill_button(x, H_Uninstall, L"Удалить мод", s.cx + half + 8, y, 40, 0, half, 2);

  // the launcher itself
  s = section_begin(x, x2, y2, col_w, row2, L"ЛАУНЧЕР");
  y = s.cy + 8;
  c.text(L"Костюм лаунчера", tiny, s.cx, y, MUTED);
  y += sm + 8;
  for (int i = 0; i < 4; ++i) swatch(x, i, s.cx + 28 + i * (48 + 18), y + 28);
  y += 56 + 4;
  toggle_row(x, H_Toggle3, L"Закрывать лаунчер, когда игра запущена", m.set.close_on_play, s.cx, y, cw);
}

// --- the mod's settings: ГРАФИКА, СЕТЬ -----------------------------------------------------------------

// A value with an arrow at each side.
void stepper(Ctx& x, int id_prev, int id_next, const std::wstring& value, float sx, float sy, float w, float h, bool on) {
  Canvas& c = x.c;
  c.round_box(sx, sy, w, h, h / 2, FIELD, 2, on ? LINE : 0x2B1819);
  const int ids[2] = {id_prev, id_next};
  for (int side = 0; side < 2; ++side) {
    const float cx = side == 0 ? sx + h / 2 + 2 : sx + w - h / 2 - 2;
    const bool over = on && x.m.hover == ids[side], down = on && x.m.pressed == ids[side];
    if (over || down) c.ellipse(cx, sy + h / 2, h / 2 - 5, h / 2 - 5, CREAM, down ? 0.22f : 0.12f);
    c.icon(side == 0 ? kPrev : kNext, cx - 9, sy + (h - 18) / 2, 18, !on ? DIM : over ? GOLD : CREAM, 2.8f);
    if (on) hit(x, ids[side], side == 0 ? sx : sx + w - h - 4, sy, h + 4, h);
  }
  Font f = T(13, 700);
  TextOpt one;
  one.ellipsis = true;
  one.max_w = w - 2 * h - 12;
  float tw = c.measure(value, f).width;
  if (tw > one.max_w) tw = one.max_w;
  c.text_mid(value, f, sx + (w - tw) / 2, sy + h / 2, on ? CREAM : DIM, one);
}

// A row of the mod's settings: its name, and at the right what sets it.
void game_row(Ctx& x, gset::Row r, float lx, float ty, float w, float h) {
  Canvas& c = x.c;
  const bool on = gset::usable(r);
  const int base = H_Row + r * kRowParts;
  c.text_mid(gset::label(r), T(14), lx, ty + h / 2, on ? CREAM : DIM);
  switch (gset::kind(r)) {
    case gset::Kind::Choice:
      stepper(x, base + P_Prev, base + P_Next, gset::name(r, gset::value(r)), lx + w - 216, ty, 216, h, on);
      break;
    case gset::Kind::Pair: {
      const float half = (216 - 6) / 2.0f;
      for (int i = 0; i < 2; ++i)
        pill_button(x, base + P_Item + i, gset::name(r, i), lx + w - 216 + i * (half + 6), ty, h, 0, half, gset::value(r) == i ? 1 : 0, on);
      break;
    }
  }
}

// The line under a page: a note at the left, "По умолчанию" at the right. It stands on the field under
// the boxes, whatever their height.
void page_foot(Ctx& x, float left, const std::wstring& note, uint32_t rgb, bool reset_on) {
  Canvas& c = x.c;
  const float y = kH - 44 - 5 - 44;
  Font bf = T(13, 700);
  const float bw = 18 + c.measure(L"По умолчанию", bf).width + 18, right = kW - left;
  TextOpt wrap;
  wrap.wrap = true;
  wrap.max_w = right - bw - 20 - (left + 18 + 10);
  wrap.line_h = 12 * 1.45f;
  const float nh = c.measure(note, T(12), wrap).height, ny = y + (44 - nh) / 2;
  c.push(Matrix::Scale(18.0f / 24, 18.0f / 24) * Matrix::Translation(left, ny));
  c.ring(12, 12, 9, 2.2f, rgb);
  c.stroke_path(kNote, 2.2f, rgb);
  c.pop();
  c.text(note, T(12), left + 18 + 10, ny, rgb, wrap);
  pill_button(x, H_Reset, L"По умолчанию", right - bw, y, 44, 18, 0, 0, reset_on);
}

const wchar_t kNoMod[] = L"Мод не установлен. Эти настройки появятся вместе с ним: они хранятся в in2net.ini в папке игры.";

// ГРАФИКА and СЕТЬ: two boxes - rows of settings with notes under them, or (the second box of СЕТЬ) how
// a network game is begun.
void page_game(Ctx& x, float left, float top, bool net) {
  Canvas& c = x.c;
  struct Group {
    const wchar_t* name;
    int from, to;
    const wchar_t* note;
  };
  const Group groups[2][2] = {
      {{L"ЭКРАН И ИГРА", gset::kMode, gset::kFirstPicture, L""},
       {L"КАРТИНКА", gset::kFirstPicture, gset::kFirstNet,
        L"Сглаживание и чёткость текстур делает видеокарта, резкость и цвета — последний штрих мода поверх кадра. "
        L"На ход игры они не влияют: у игроков по сети могут быть разными.\n\n"
        L"«Полный экран игры» — как в оригинале: мод тогда картинку не трогает, широкого кадра и сглаживания нет."}},
      {{L"СЕТЕВАЯ ИГРА", gset::kFirstNet, gset::kRows,
        L"Задержка ввода: больше — спокойнее при плохой связи, меньше — отзывчивее. Действует та, что у хоста."},
       {L"КАК СЫГРАТЬ", 0, 0, L""}}};
  const wchar_t* steps[4] = {L"Оба запускают игру и выбирают в главном меню «СЕТЕВАЯ ИГРА».",
                             L"Хост — «СОЗДАТЬ ИГРУ». Друг в Steam видит её в списке; без Steam набирает адрес хоста.",
                             L"Хост — «НАЧАТЬ ИГРУ»: обе игры откроются заново, уже в сетевой игре.",
                             L"В главном меню игрок 1 выбирает «ДВА ИГРОКА»."};
  const Group* page = groups[net ? 1 : 0];
  const float col_w = (kW - 2 * left - 24) / 2.0f, cw = col_w - 4 - 32;
  const float row_h = net ? 44.0f : 40.0f, row_gap = net ? 6.0f : 5.0f;
  const float h3 = c.line_h(D(13, 0.06f));
  TextOpt wrap;
  wrap.wrap = true;
  wrap.max_w = cw;
  wrap.line_h = 12 * 1.45f;
  TextOpt step_wrap;
  step_wrap.wrap = true;
  step_wrap.max_w = cw - 22 - 10;
  step_wrap.line_h = 13 * 1.45f;
  float box_h = 0;
  for (int i = 0; i < 2; ++i) {
    const int n = page[i].to - page[i].from;
    float h = 2 + 14 + h3 + 2 + 6 + 14 + 2;
    if (n > 0) {
      h += n * row_h + (n - 1) * row_gap;
      if (page[i].note[0]) h += row_gap + 6 + c.measure(page[i].note, T(12), wrap).height;
    } else {
      for (int k = 0; k < 4; ++k) h += c.measure(steps[k], T(13), step_wrap).height + (k ? 10.0f : 0.0f);
    }
    box_h = std::max(box_h, h);
  }
  const float y1 = top + 44 + 16;
  for (int i = 0; i < 2; ++i) {
    Section s = section_begin(x, left + i * (col_w + 24), y1, col_w, box_h, page[i].name);
    float y = s.cy + 2 + 6;
    if (page[i].to > page[i].from) {
      for (int r = page[i].from; r < page[i].to; ++r) {
        game_row(x, static_cast<gset::Row>(r), s.cx, y, cw, row_h);
        y += row_h + row_gap;
      }
      if (page[i].note[0]) c.text(page[i].note, T(12), s.cx, y + 6, MUTED, wrap);
    } else {
      for (int k = 0; k < 4; ++k) {  // the steps, each with its number on a disc of the sign's colours
        if (k) y += 10;
        c.ellipse_gradient(s.cx + 10, y + 10, 10, 10, GOLD, FLAME);
        wchar_t num[2] = {static_cast<wchar_t>(L'1' + k), 0};
        Font nf = Mo(11, 700);
        c.text_mid(num, nf, s.cx + (20 - c.measure(num, nf).width) / 2, y + 10, INK);
        y += c.text(steps[k], T(13), s.cx + 22 + 10, y, TEXT2, step_wrap);
      }
    }
  }
  const bool mod = gset::has_mod();
  const wchar_t* note = !mod ? kNoMod
                        : net ? L"Настройки сетевой игры из in2net.ini. Действуют со следующего запуска игры."
                              : L"Настройки мода из in2net.ini. Громкость и графику самой игры меняет её экран ОПЦИИ. "
                                L"Действуют со следующего запуска игры.";
  page_foot(x, left, note, mod ? CREAM : WAIT, mod);
}

// --- the game's keys: УПРАВЛЕНИЕ -----------------------------------------------------------------------

// A key's cell: the key's name; while it waits for a key - an edge that glows and "жду клавишу".
void key_cell(Ctx& x, int action, float bx, float by, float w, float h) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const int id = H_Key + action, player = m.keys_player;
  const bool fixed = keys::fixed(player, action), waits = m.capture == action;
  const bool hover = !fixed && m.hover == id;
  Font f = T(12, 700);
  TextOpt one;
  one.ellipsis = true;
  one.max_w = w - 16;
  if (waits) {
    const float glow = 0.55f + 0.45f * static_cast<float>(std::sin(m.time * 6.0));
    c.round_fill(bx - 2, by - 2, w + 4, h + 4, h / 2 + 2, GOLD, 0.25f + 0.35f * glow);
    c.round_box(bx, by, w, h, h / 2, 0x2A1A06, 2, GOLD);
    const wchar_t* ask = L"жду клавишу";
    const float tw = std::min(c.measure(ask, f).width, one.max_w);
    c.text_mid(ask, f, bx + (w - tw) / 2, by + h / 2, GOLD, one);
  } else {
    c.round_box(bx, by, w, h, h / 2, fixed ? COAL : hover ? 0x1B0D0E : FIELD, 2, fixed ? 0x2B1819 : hover ? GOLD : LINE);
    const std::wstring name = keys::shown(player, action);
    float tw = std::min(c.measure(name, f).width, one.max_w);
    if (fixed) {  // (the game's own: a small lock before the name)
      c.icon(kLock, bx + (w - tw - 16) / 2, by + (h - 12) / 2 - 1, 12, DIM, 2.4f);
      c.text_mid(name, f, bx + (w - tw - 16) / 2 + 16, by + h / 2, DIM, one);
    } else {
      c.text_mid(name, f, bx + (w - tw) / 2, by + h / 2, keys::code(player, action) == 0 ? DIM : CREAM, one);
    }
  }
  if (!fixed) hit(x, id, bx, by, w, h);
}

// УПРАВЛЕНИЕ: whose keys, then three boxes of actions, each with the key that does it.
void page_keys(Ctx& x, float left, float top) {
  Canvas& c = x.c;
  const Model& m = x.m;
  const float y0 = top + 44 + 14;
  // whose keys
  const float pw = 118;
  pill_button(x, H_KeysOne, L"ИГРОК 1", left, y0, 36, 0, pw, m.keys_player == 0 ? 1 : 0, true, 0.06f);
  pill_button(x, H_KeysTwo, L"ИГРОК 2", left + pw + 8, y0, 36, 0, pw, m.keys_player == 1 ? 1 : 0, true, 0.06f);
  TextOpt how;
  how.ellipsis = true;
  how.max_w = kW - left - (left + 2 * pw + 8 + 18);
  c.text_mid(m.capture >= 0 ? L"Нажмите новую клавишу для действия. Esc — отмена."
                            : L"Нажмите на клавишу действия, потом — на новую клавишу.",
             T(13, 500), left + 2 * pw + 8 + 18, y0 + 18, CREAM, how, 0.9f);
  // the three boxes
  const float y1 = y0 + 36 + 12, box_h = (kH - 44 - 5 - 44 - 10) - y1;
  const float col_w = (kW - 2 * left - 2 * 12) / 3.0f, cw = col_w - 4 - 28, cell_w = 118, row_h = 30, pitch = 33;
  for (int g = 0; g < keys::kGroups; ++g) {
    const float sx = left + g * (col_w + 12);
    c.round_box(sx, y1, col_w, box_h, 16, COAL, 2, BLACK);
    const float cx = sx + 2 + 14;
    float y = y1 + 2 + 13;
    y += c.text(keys::group_name(g), D(13, 0.06f), cx, y, GOLD) + 9;
    for (int a = 0; a < keys::kActions; ++a) {
      if (keys::group(a) != g || !keys::has(m.keys_player, a)) continue;
      TextOpt one;
      one.ellipsis = true;
      one.max_w = cw - cell_w - 6;
      c.text_mid(keys::label(a), T(13), cx, y + row_h / 2, CREAM, one);
      key_cell(x, a, cx + cw - cell_w, y, cell_w, row_h);
      y += pitch;
    }
  }
  page_foot(x, left,
            L"Клавиши — самой игры, те же, что на её экране ОПЦИИ. В сетевой игре каждый играет на своём ПК клавишами "
            L"игрока 1; клавиши игрока 2 — для игры вдвоём за одним ПК. Действуют со следующего запуска игры.",
            CREAM, true);
}

void settings(Ctx& x) {
  const float left = 40, top = 48 + 20;
  settings_head(x, left, top);
  switch (x.m.tab) {
    case Tab::Graphics: page_game(x, left, top, false); break;
    case Tab::Controls: page_keys(x, left, top); break;
    case Tab::Net: page_game(x, left, top, true); break;
    default: page_launcher(x, left, top); break;
  }
}

}  // namespace

bool primary_enabled(const Model& m) { return vals(m).primary_on; }
std::wstring link_url(const Model& m) { return vals(m).link_url; }

void draw(gfx::Canvas& c, const Model& m, std::vector<Hit>* hits) {
  if (hits) hits->clear();
  const int suit = m.set.accent & 3;
  Ctx x{c, m, hits, kSuits[suit]};
  const bool pages = m.screen == Screen::Settings;
  c.cached((pages ? 20 : 10) + suit, kW, kH, [&](Canvas& ground) {
    if (pages) field(ground, kSuits[suit]);
    else ground_home(ground, kSuits[suit]);
  });
  Vals v = vals(m);
  header(x);
  if (pages) settings(x);
  else hero(x, v);
  footer(x, v);
}

// The screens as pictures (launcher --shot <screen>): a model of each, with the mod inside this launcher
// as the one on offer. "github-*" screens are drawn as they will be once the mod has a repository.
bool demo(const std::wstring& screen_name, Model& m) {
  std::wstring screen = screen_name;
  m = Model{};
  m.repo = core::has_repo();
  if (screen.rfind(L"github-", 0) == 0) {
    screen = screen.substr(7);
    m.repo = true;
  }
  m.has_game = true;
  m.set.game_dir = L"D:\\Games\\Суперсемейка 2";
  m.inst.present = true;
  m.inst.version = L"0.3.1";
  m.inst.protocol = 4;
  m.pack = core::bundled();
  if (!m.pack.known) {
    m.pack.known = true;
    m.pack.version = L"0.4.0";
    m.pack.protocol = 4;
    m.pack.title = L"Картинка лучше, клавиши в лаунчере";
    m.pack.notes = {L"Сглаживание краёв, чёткие текстуры и лёгкая резкость кадра.", L"Клавиши обоих игроков назначаются в лаунчере."};
  }
  if (m.repo) {
    m.rel = m.pack;
    m.rel.full = true;
    m.rel.pkg_name = L"in2net-launcher.exe";
    m.rel.pkg_size = 3900000;
    m.rel.pkg_sha256 = "x";
  }
  m.total = m.repo ? 3900000 : 0;
  m.got = static_cast<uint64_t>(m.total * 0.62);
  if (screen == L"update") {
    m.screen = Screen::Update;
  } else if (screen == L"downloading") {
    m.screen = Screen::Downloading;
    m.pending = Pending::Install;
    m.stage = m.repo ? 0 : 2;
  } else if (screen == L"ready") {
    m.screen = Screen::Ready;
    m.inst.version = m.pack.version;
    m.inst.protocol = m.pack.protocol;
    m.unchecked = !m.repo;
  } else if (screen == L"checking") {
    m.screen = Screen::Checking;
    m.rel = Release{};
  } else if (screen == L"install") {
    m.screen = Screen::Install;
    m.inst = Installed{};
  } else if (screen == L"nogame") {
    m.screen = Screen::NoGame;
    m.has_game = false;
    m.inst = Installed{};
  } else if (screen == L"offline") {
    m.screen = Screen::Offline;
    m.rel = Release{};
  } else if (screen == L"running") {
    m.screen = Screen::Running;
    m.pending = Pending::Install;
  } else if (screen == L"problem") {
    m.screen = Screen::Problem;
    m.problem_title = L"Мод не установился";
    m.problem_text = L"Папка игры закрыта для записи. Запустите лаунчер от имени администратора или выберите другую папку.";
    m.problem_primary = L"ПОВТОРИТЬ";
    m.problem_secondary = L"НАЗАД";
  } else if (screen == L"settings") {
    m.screen = Screen::Settings;
    m.back = Screen::Ready;
    m.inst.version = m.pack.version;
  } else if (screen == L"graphics" || screen == L"net" || screen == L"controls" || screen == L"controls2" ||
             screen == L"controls-wait") {
    m.screen = Screen::Settings;
    m.back = Screen::Ready;
    m.inst.version = m.pack.version;
    m.tab = screen == L"graphics" ? Tab::Graphics : screen == L"net" ? Tab::Net : Tab::Controls;
    m.keys_player = screen == L"controls2" ? 1 : 0;
    m.capture = screen == L"controls-wait" ? 4 : -1;
    gset::load_canned();
    keys::load_canned();
  } else {
    return false;
  }
  return true;
}

}  // namespace view
}  // namespace lz
