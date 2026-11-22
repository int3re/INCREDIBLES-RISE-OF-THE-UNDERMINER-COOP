#include "mod/netplay.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/log.h"
#include "common/net.h"
#include "common/protocol.h"
#include "common/sha256.h"
#include "common/steam_boot.h"
#include "common/transport.h"
#include "common/transport_enet.h"
#include "common/transport_steam.h"
#include "common/version.h"
#include "mod/game.h"
#include "mod/notice.h"
#include "mod/patch.h"

namespace netplay {
namespace {

using transport::Channel;
using transport::Event;
using transport::EventType;
using transport::PeerId;

enum class Phase { Off, Meeting, Ready, Playing, Ended };

Config g_cfg;
Phase g_phase = Phase::Off;
std::unique_ptr<transport::ITransport> g_t;
PeerId g_peer;
int g_me = 0;
int g_delay = 2;
uint32_t g_first = 1;      // the first frame of the session
uint32_t g_frame = 0;      // the frame being played
uint8_t g_exe_hash[8] = {};
uint8_t g_data_hash[8] = {};
std::string g_peer_name;

// Pads by frame: g_pads[player][frame - g_first]; g_newest[player] = the last frame known.
std::vector<input::Pad> g_pads[in2p::kPlayers];
uint32_t g_newest[in2p::kPlayers] = {};
uint32_t g_last_rx_ms = 0;
uint32_t g_acked = 0;  // the newest frame of this PC's pads the other side has confirmed
uint32_t g_started_ms = 0;

std::map<uint32_t, uint32_t> g_hash_mine, g_hash_theirs;
bool g_desync = false;
uint32_t g_waited_ms = 0, g_waits = 0;

// The link's keeper. The game's thread answers the network once a frame; a frame that loads a
// level takes seconds (longer than a link may stay silent on a slow disk), and so does a window
// being dragged. A second thread then answers in its place: it runs the transport (pings are
// answered, packets are taken into its queue) and says "here" twice a second. Everything that
// touches the transport holds g_net_lock.
CRITICAL_SECTION g_net_lock;
std::atomic<uint32_t> g_pumped_ms{0};  // when the game's thread last ran the transport
std::atomic<bool> g_keeper_on{false};
std::atomic<uint32_t> g_kept{0};  // how many times the keeper stood in

struct NetLock {
  NetLock() { EnterCriticalSection(&g_net_lock); }
  ~NetLock() { LeaveCriticalSection(&g_net_lock); }
};

// --- small helpers --------------------------------------------------------------------
void set_pad(int player, uint32_t frame, const input::Pad& pad) {
  if (frame < g_first) return;
  size_t i = frame - g_first;
  if (g_pads[player].size() <= i) g_pads[player].resize(i + 1);
  g_pads[player][i] = pad;
  if (frame > g_newest[player]) g_newest[player] = frame;
}

input::Pad get_pad(int player, uint32_t frame) {
  if (frame < g_first) return {};
  size_t i = frame - g_first;
  return i < g_pads[player].size() ? g_pads[player][i] : input::Pad{};
}

in2p::PadWire to_wire(const input::Pad& p) {
  in2p::PadWire w{};
  w.buttons = p.buttons;
  memcpy(w.analog, p.analog, 8);
  w.lx = p.lx, w.ly = p.ly, w.rx = p.rx, w.ry = p.ry;
  return w;
}

input::Pad from_wire(const in2p::PadWire& w) {
  input::Pad p;
  p.buttons = w.buttons;
  memcpy(p.analog, w.analog, 8);
  p.lx = w.lx, p.ly = w.ly, p.rx = w.rx, p.ry = w.ry;
  return p;
}

template <typename T>
bool send(const T& packet, Channel ch) {
  NetLock lock;
  return g_t && g_peer.valid() && g_t->Send(g_peer, &packet, sizeof(packet), ch);
}

void copy_name(char* to, size_t n, const std::string& s) {
  memset(to, 0, n);
  memcpy(to, s.data(), s.size() < n - 1 ? s.size() : n - 1);
}

// The window's messages while the game's own loop is held.
bool pump_messages() {
  MSG msg;
  while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      PostQuitMessage(static_cast<int>(msg.wParam));
      return false;
    }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  return *reinterpret_cast<const uint8_t*>(game::kAppQuit) == 0;
}

void end_session(const char* why) {
  if (g_phase == Phase::Off || g_phase == Phase::Ended) return;
  LOGW("net: the session is over: %s (frame %u; waited %u ms in %u waits; the keeper stood in %u times)", why, g_frame,
       g_waited_ms, g_waits, g_kept.load());
  g_keeper_on = false;
  if (strcmp(why, "this game is closing") != 0 && g_phase == Phase::Playing)
    notice::say(strcmp(why, "the other player left") == 0 ? L"ВТОРОЙ ИГРОК ВЫШЕЛ ИЗ ИГРЫ\nДАЛЬШЕ ВЫ ИГРАЕТЕ ОДИН"
                                                          : L"СВЯЗЬ СО ВТОРЫМ ИГРОКОМ ПОТЕРЯНА\nДАЛЬШЕ ВЫ ИГРАЕТЕ ОДИН",
                10);
  if (g_t && g_peer.valid()) {
    auto bye = in2p::make<in2p::Hash>(in2p::Type::Bye);
    send(bye, Channel::Control);
    NetLock lock;
    g_t->Poll(20);
  }
  g_phase = Phase::Ended;
}

// --- the co-op saves: host -> guest ------------------------------------------------------
std::string save_dir() { return reinterpret_cast<const char*>(game::kSaveDir); }

struct FileData {
  std::string name;
  std::vector<uint8_t> data;
};

std::vector<FileData> read_saves() {
  std::vector<FileData> files;
  WIN32_FIND_DATAA fd;
  std::string dir = save_dir();
  HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return files;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if (fd.nFileSizeHigh || fd.nFileSizeLow > 4 * 1024 * 1024 || strlen(fd.cFileName) >= sizeof(in2p::SaveFile::name)) continue;
    FileData f;
    f.name = fd.cFileName;
    FILE* fp = fopen((dir + "\\" + f.name).c_str(), "rb");
    if (!fp) continue;
    f.data.resize(fd.nFileSizeLow);
    size_t got = f.data.empty() ? 0 : fread(f.data.data(), 1, f.data.size(), fp);
    fclose(fp);
    if (got == f.data.size()) files.push_back(std::move(f));
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return files;
}

void clear_saves() {
  WIN32_FIND_DATAA fd;
  std::string dir = save_dir();
  HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileA((dir + "\\" + fd.cFileName).c_str());
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

bool safe_name(const char* name) {
  if (!*name) return false;
  for (const char* c = name; *c; ++c)
    if (*c == '\\' || *c == '/' || *c == ':' || (c[0] == '.' && c[1] == '.')) return false;
  return true;
}

// [debug] net_loss / net_outage: a bad link for the lab. Unreliable packets of this game are
// dropped at random, or all of them for a stretch of time. (An own generator: the game's
// random numbers are part of the game.)
bool lab_drops() {
  if (g_cfg.net_outage_ms) {
    uint32_t t = net::now_ms() - g_started_ms;
    if (t >= g_cfg.net_outage_at_ms && t < g_cfg.net_outage_at_ms + g_cfg.net_outage_ms) return true;
  }
  if (!g_cfg.net_loss) return false;
  static uint32_t seed = GetTickCount() | 1;
  seed = seed * 1664525u + 1013904223u;
  return (seed >> 16) % 100 < static_cast<uint32_t>(g_cfg.net_loss);
}

// --- packets ------------------------------------------------------------------------------
void send_inputs() {
  if (g_phase != Phase::Playing) return;
  // Every pad the other side has not confirmed yet goes into every packet: a lost packet (or
  // a hundred) costs nothing but the wait for the next one.
  uint8_t buf[sizeof(in2p::Input) + in2p::kInputMax * sizeof(in2p::PadWire)];
  auto in = in2p::make<in2p::Input>(in2p::Type::Input);
  uint32_t newest = g_newest[g_me];
  if (newest < g_first) return;
  uint32_t from = g_acked >= g_first ? g_acked + 1 : g_first;
  if (from > newest) from = newest;  // (at least the newest pad: it carries the acknowledgement)
  uint32_t count = newest - from + 1;
  if (count > in2p::kInputMax) {
    // the oldest unconfirmed ones first: those are what the other game is waiting for
    newest = from + in2p::kInputMax - 1;
    count = in2p::kInputMax;
  }
  in.player = static_cast<uint8_t>(g_me);
  in.count = static_cast<uint8_t>(count);
  in.newest = newest;
  in.ack = g_newest[1 - g_me];
  memcpy(buf, &in, sizeof(in));
  for (uint32_t i = 0; i < count; ++i) {
    in2p::PadWire w = to_wire(get_pad(g_me, newest - count + 1 + i));
    memcpy(buf + sizeof(in) + i * sizeof(w), &w, sizeof(w));
  }
  if (!lab_drops()) {
    NetLock lock;
    g_t->Send(g_peer, buf, sizeof(in) + count * sizeof(in2p::PadWire), Channel::Input);
  }
}

void check_hash(uint32_t frame) {
  auto a = g_hash_mine.find(frame);
  auto b = g_hash_theirs.find(frame);
  if (a == g_hash_mine.end() || b == g_hash_theirs.end()) return;
  if (a->second != b->second && !g_desync) {
    g_desync = true;
    LOGE("net: DESYNC at frame %u: this game %08X, %s's %08X", frame, a->second, g_peer_name.c_str(), b->second);
    notice::say(L"ИГРЫ РАЗОШЛИСЬ\nНАЧНИТЕ СЕТЕВУЮ ИГРУ ЗАНОВО", 15);
  }
  g_hash_mine.erase(g_hash_mine.begin(), a);
  g_hash_theirs.erase(g_hash_theirs.begin(), b);
}

uint32_t state_hash() {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      h ^= (v >> (i * 8)) & 0xFF;
      h *= 16777619u;
    }
  };
  mix(patch::read<uint32_t>(game::kRngLeft));
  mix(patch::read<uint32_t>(game::kRngNext) - static_cast<uint32_t>(game::kRngState));
  for (int i = 0; i < 8; ++i) mix(patch::read<uint32_t>(game::kRngState + 4 * i));
  auto scene = patch::read<const uint32_t*>(game::kScenePtr);
  mix(scene ? *scene : 0);
  return h;
}

void on_data(const Event& ev) {
  const uint8_t* d = ev.data.data();
  size_t n = ev.data.size();
  uint8_t type = in2p::type_of(d, n);
  if (!type) return;
  g_last_rx_ms = net::now_ms();
  switch (static_cast<in2p::Type>(type)) {
    case in2p::Type::Input: {
      if (n < sizeof(in2p::Input)) return;
      in2p::Input in;
      memcpy(&in, d, sizeof(in));
      if (in.player >= in2p::kPlayers || in.player == g_me) return;
      if (n < sizeof(in) + in.count * sizeof(in2p::PadWire) || in.count > in.newest + 1) return;
      if (in.ack > g_acked && in.ack <= g_newest[g_me]) g_acked = in.ack;
      for (uint32_t i = 0; i < in.count; ++i) {
        uint32_t frame = in.newest - in.count + 1 + i;
        if (frame <= g_newest[in.player] && g_newest[in.player] >= g_first) continue;  // known already
        if (frame != g_newest[in.player] + 1 && !(g_newest[in.player] < g_first && frame == g_first)) break;  // a gap: wait
        in2p::PadWire w;
        memcpy(&w, d + sizeof(in) + i * sizeof(w), sizeof(w));
        set_pad(in.player, frame, from_wire(w));
      }
      break;
    }
    case in2p::Type::Hash: {
      if (n < sizeof(in2p::Hash)) return;
      in2p::Hash hp;
      memcpy(&hp, d, sizeof(hp));
      g_hash_theirs[hp.frame] = hp.hash;
      check_hash(hp.frame);
      break;
    }
    case in2p::Type::Bye:
      end_session("the other player left");
      break;
    default:
      break;
  }
}

void pump_net(uint32_t wait_ms) {
  if (!g_t) return;
  g_pumped_ms = net::now_ms();
  Event ev[16];
  bool first = true;
  for (;;) {
    int n;
    {
      NetLock lock;
      if (first) {
        if (steam::ready()) steam::pump();
        g_t->Poll(wait_ms);
      }
      first = false;
      n = g_t->Receive(ev, 16);
    }
    if (n <= 0) break;
    for (int i = 0; i < n; ++i) {
      if (ev[i].type == EventType::Data && ev[i].peer == g_peer)
        on_data(ev[i]);
      else if (ev[i].type == EventType::Disconnected && ev[i].peer == g_peer)
        end_session(("the link is gone: " + ev[i].reason).c_str());
    }
  }
}

DWORD WINAPI keeper(void*) {
  uint32_t said = 0;
  for (;;) {
    Sleep(100);
    if (!g_keeper_on) continue;
    uint32_t now = net::now_ms();
    if (now - g_pumped_ms.load() < 300) continue;  // the game's thread is at it
    NetLock lock;
    if (!g_keeper_on || !g_t || !g_peer.valid()) continue;
    if (steam::ready()) steam::pump();
    g_t->Poll(0);
    ++g_kept;
    if (now - said >= 500) {
      said = now;
      in2p::Header alive;
      alive.type = static_cast<uint8_t>(in2p::Type::Alive);
      g_t->Send(g_peer, &alive, sizeof(alive), Channel::Input);
    }
  }
}

// --- the meeting -----------------------------------------------------------------------------
// Over Steam (by SteamID; the lab's stand-in too) or by address: a game started again from the network
// page is told (IN2NET_VIA), a game set up in the ini goes by [net] transport.
bool over_steam() {
  if (!g_cfg.net_via.empty()) return g_cfg.net_via == "steam";
  return g_cfg.net_transport == "steam" || g_cfg.net_transport == "mock";
}

bool meet_host() {
  uint16_t port = static_cast<uint16_t>(g_cfg.net_port);
  bool via_steam = over_steam();
  if (via_steam) {
    if (!steam::boot(g_cfg)) {
      LOGE("net: Steam is not there: %s", steam::unavailable_reason().c_str());
      return false;
    }
    // Only the player this game was started for (the lab's stand-in: anybody).
    g_t = transport::make_steam_transport([](uint64_t id) { return g_cfg.net_peer ? id == g_cfg.net_peer : steam::is_mock(); });
    if (!g_t) return false;
    LOGI("net: hosting over Steam as %llu; waiting for the second player (up to %d s)", (unsigned long long)steam::my_id(),
         g_cfg.net_wait_s);
  } else {
    g_t = std::make_unique<transport::EnetTransport>(port, 1, 2000);
    if (!g_t->Init()) {
      LOGE("net: cannot listen on UDP port %u", port);
      return false;
    }
    LOGI("net: hosting on UDP port %u; waiting for the second player (up to %d s)", port, g_cfg.net_wait_s);
  }
  uint32_t t0 = net::now_ms();
  while (net::now_ms() - t0 < static_cast<uint32_t>(g_cfg.net_wait_s) * 1000) {
    if (via_steam) steam::pump();
    g_t->Poll(20);
    Event ev[8];
    int n = g_t->Receive(ev, 8);
    for (int i = 0; i < n; ++i) {
      if (ev[i].type != EventType::Data || in2p::type_of(ev[i].data.data(), ev[i].data.size()) != static_cast<uint8_t>(in2p::Type::Hello) ||
          ev[i].data.size() < sizeof(in2p::Hello))
        continue;
      in2p::Hello hello;
      memcpy(&hello, ev[i].data.data(), sizeof(hello));
      hello.name[sizeof(hello.name) - 1] = 0;
      hello.mod_version[sizeof(hello.mod_version) - 1] = 0;
      g_peer = ev[i].peer;
      const char* why = memcmp(hello.exe_hash, g_exe_hash, 8) != 0     ? "another incrotu.exe"
                        : memcmp(hello.data_hash, g_data_hash, 8) != 0 ? "another data\\in2.ini (the game's files differ)"
                                                                       : nullptr;
      if (why) {
        auto rej = in2p::make<in2p::Reject>(in2p::Type::Reject);
        copy_name(rej.reason, sizeof(rej.reason), why);
        send(rej, Channel::Control);
        g_t->Poll(20);
        LOGW("net: %s refused: %s", hello.name, why);
        g_peer = {};
        continue;
      }
      g_peer_name = hello.name;
      LOGI("net: %s joined (in2net %s, %s)", hello.name, hello.mod_version, g_t->Describe(g_peer).c_str());
      std::vector<FileData> files = read_saves();
      auto w = in2p::make<in2p::Welcome>(in2p::Type::Welcome);
      w.player = 1;
      w.input_delay = static_cast<uint8_t>(g_delay);
      w.save_files = static_cast<uint16_t>(files.size());
      w.first_frame = g_first;
      copy_name(w.host_name, sizeof(w.host_name), g_cfg.net_name);
      send(w, Channel::Control);
      for (const FileData& f : files) {
        std::vector<uint8_t> buf(sizeof(in2p::SaveFile) + f.data.size());
        auto sf = in2p::make<in2p::SaveFile>(in2p::Type::SaveFile);
        sf.size = static_cast<uint32_t>(f.data.size());
        copy_name(sf.name, sizeof(sf.name), f.name);
        memcpy(buf.data(), &sf, sizeof(sf));
        if (!f.data.empty()) memcpy(buf.data() + sizeof(sf), f.data.data(), f.data.size());
        g_t->Send(g_peer, buf.data(), buf.size(), Channel::Control);
      }
      send(in2p::make<in2p::Start>(in2p::Type::Start), Channel::Control);
      g_t->Poll(5);
      LOGI("net: %u save file(s) sent; the game starts", (unsigned)files.size());
      g_me = 0;
      return true;
    }
    if (!pump_messages()) return false;
  }
  LOGW("net: nobody joined in %d s; the game is played alone", g_cfg.net_wait_s);
  return false;
}

bool meet_guest() {
  net::Addr addr;
  std::string host = g_cfg.net_connect;
  uint16_t port = static_cast<uint16_t>(g_cfg.net_port);
  size_t colon = host.rfind(':');
  if (colon != std::string::npos) {
    port = static_cast<uint16_t>(atoi(host.c_str() + colon + 1));
    host.erase(colon);
  }
  bool via_steam = over_steam();
  if (via_steam) {
    if (!steam::boot(g_cfg)) {
      LOGE("net: Steam is not there: %s", steam::unavailable_reason().c_str());
      return false;
    }
    uint64_t host_id = g_cfg.net_peer ? g_cfg.net_peer : strtoull(g_cfg.net_connect.c_str(), nullptr, 10);
    if (!host_id) {
      LOGE("net: no SteamID of the host to join");
      return false;
    }
    g_t = transport::make_steam_transport([host_id](uint64_t id) { return id == host_id; });
    if (!g_t) return false;
    g_peer = PeerId::steam(host_id);
    LOGI("net: joining the host %llu over Steam", (unsigned long long)host_id);
  } else {
    if (!net::resolve(host, port, addr)) {
      LOGE("net: cannot resolve %s", g_cfg.net_connect.c_str());
      return false;
    }
    g_t = std::make_unique<transport::EnetTransport>(static_cast<uint16_t>(0), 1, 2000);
    if (!g_t->Init()) {
      LOGE("net: cannot open a UDP socket");
      return false;
    }
    g_peer = PeerId::enet(addr.sa.sin_addr.s_addr, port);
    LOGI("net: joining %s:%u", host.c_str(), port);
  }
  auto say_hello = [] {
    auto hello = in2p::make<in2p::Hello>(in2p::Type::Hello);
    memcpy(hello.exe_hash, g_exe_hash, 8);
    memcpy(hello.data_hash, g_data_hash, 8);
    copy_name(hello.mod_version, sizeof(hello.mod_version), IN2NET_VERSION);
    copy_name(hello.name, sizeof(hello.name), g_cfg.net_name);
    send(hello, Channel::Control);
  };
  uint32_t t0 = net::now_ms();
  bool connected = false, welcomed = false;
  uint32_t dialled = 0, hello_at = 0;
  unsigned files_left = 0;
  while (net::now_ms() - t0 < static_cast<uint32_t>(g_cfg.net_wait_s) * 1000) {
    if (!connected && (!dialled || net::now_ms() - dialled > (via_steam ? 1500u : 3000u))) {
      g_t->Close(g_peer);
      g_t->Connect(g_peer);  // (again: the host may not be up yet)
      dialled = net::now_ms();
    }
    // (Steam has no "connected": the session opens with the first message, and a host that is not up
    // yet never saw it)
    if (via_steam && connected && !welcomed && net::now_ms() - hello_at > 3000) {
      say_hello();
      hello_at = net::now_ms();
    }
    if (via_steam) steam::pump();
    g_t->Poll(20);
    Event ev[8];
    int n = g_t->Receive(ev, 8);
    for (int i = 0; i < n; ++i) {
      if (ev[i].peer != g_peer) continue;
      if (ev[i].type == EventType::Connected) {
        connected = true;
        say_hello();
        hello_at = net::now_ms();
      } else if (ev[i].type == EventType::Disconnected) {
        connected = false;
        if (welcomed) {
          LOGE("net: the host is gone during the start: %s", ev[i].reason.c_str());
          return false;
        }
      } else if (ev[i].type == EventType::Data) {
        const uint8_t* d = ev[i].data.data();
        size_t size = ev[i].data.size();
        switch (static_cast<in2p::Type>(in2p::type_of(d, size))) {
          case in2p::Type::Reject: {
            in2p::Reject rej{};
            memcpy(&rej, d, size < sizeof(rej) ? size : sizeof(rej));
            rej.reason[sizeof(rej.reason) - 1] = 0;
            LOGE("net: the host refused: %s", rej.reason);
            return false;
          }
          case in2p::Type::Welcome: {
            if (size < sizeof(in2p::Welcome)) break;
            in2p::Welcome w;
            memcpy(&w, d, sizeof(w));
            w.host_name[sizeof(w.host_name) - 1] = 0;
            g_peer_name = w.host_name;
            g_me = w.player;
            g_delay = w.input_delay;
            g_first = w.first_frame;
            files_left = w.save_files;
            welcomed = true;
            clear_saves();  // the co-op saves are the host's
            LOGI("net: in %s's game as player %d, input delay %d, %u save file(s) to come", w.host_name, g_me + 1, g_delay,
                 files_left);
            break;
          }
          case in2p::Type::SaveFile: {
            if (size < sizeof(in2p::SaveFile) || !welcomed) break;
            in2p::SaveFile sf;
            memcpy(&sf, d, sizeof(sf));
            sf.name[sizeof(sf.name) - 1] = 0;
            if (size < sizeof(sf) + sf.size || !safe_name(sf.name)) break;
            FILE* fp = fopen((save_dir() + "\\" + sf.name).c_str(), "wb");
            if (fp) {
              fwrite(d + sizeof(sf), 1, sf.size, fp);
              fclose(fp);
            }
            if (files_left) --files_left;
            break;
          }
          case in2p::Type::Start:
            if (!welcomed) break;
            if (files_left) LOGW("net: %u save file(s) did not arrive", files_left);
            return true;
          default:
            break;
        }
      }
    }
    if (!pump_messages()) return false;
  }
  LOGW("net: no host at %s in %d s; the game is played alone", g_cfg.net_connect.c_str(), g_cfg.net_wait_s);
  return false;
}

}  // namespace

bool active() { return g_phase == Phase::Playing; }
void leave(const char* why) { end_session(why); }
int me() { return g_phase == Phase::Playing || g_phase == Phase::Ready ? g_me : 0; }

void meet() {
  if (g_phase != Phase::Meeting) return;
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  uint8_t hash[32] = {};
  sha256_file(exe, hash);
  memcpy(g_exe_hash, hash, 8);
  uint8_t data[32] = {};
  if (!sha256_file(module_dir() + L"data\\in2.ini", data)) LOGW("net: data\\in2.ini cannot be read");
  memcpy(g_data_hash, data, 8);
  net::startup();
  bool ok = g_cfg.net_mode == Config::Net::Host ? meet_host() : meet_guest();
  if (!ok) {
    if (g_t) g_t->Shutdown();
    g_t.reset();
    g_phase = Phase::Ended;
    if (g_cfg.net_restarted) notice::say(L"ВТОРОЙ ИГРОК НЕ ПОДКЛЮЧИЛСЯ\nСЕТЕВАЯ ИГРА НЕ НАЧАЛАСЬ", 20);
    return;
  }
  g_last_rx_ms = net::now_ms();
  g_pumped_ms = net::now_ms();
  g_phase = Phase::Ready;
  g_keeper_on = true;
  if (HANDLE t = CreateThread(nullptr, 0, keeper, nullptr, 0, nullptr)) CloseHandle(t);
}

void begin_frame(uint32_t frame) {
  g_frame = frame;
  if (g_phase == Phase::Ready && frame >= g_first) {
    if (frame != g_first) {
      LOGE("net: the session should have started at frame %u, this game is at %u", g_first, frame);
      end_session("the two games did not start together");
      return;
    }
    g_phase = Phase::Playing;
    g_newest[0] = g_newest[1] = 0;
    g_acked = 0;
    g_started_ms = net::now_ms();
    LOGI("net: playing with %s from frame %u as player %d", g_peer_name.c_str(), frame, g_me + 1);
    notice::say(g_me == 0 ? L"СЕТЕВАЯ ИГРА: ВЫ - ИГРОК 1\nВЫБЕРИТЕ В МЕНЮ \"ДВА ИГРОКА\"" : L"СЕТЕВАЯ ИГРА: ВЫ - ИГРОК 2\nМЕНЮ ВЕДЕТ ИГРОК 1", 12);
  }
  if (g_phase != Phase::Playing) return;
  if (*reinterpret_cast<const uint8_t*>(game::kAppQuit)) {
    end_session("this game is closing");
    return;
  }

  // [debug] net_quit: this game leaves the session.
  if (g_cfg.net_quit_ms && net::now_ms() - g_started_ms >= g_cfg.net_quit_ms) {
    LOGI("net: [lab] leaving the session");
    end_session("this game is closing");
    return;
  }
  // [debug] net_stall: the lab's stand-in for a frame that takes long.
  static bool stalled = false;
  if (g_cfg.net_stall_ms && !stalled && net::now_ms() - g_started_ms >= g_cfg.net_stall_at_ms) {
    stalled = true;
    LOGI("net: [lab] this game stands still for %u ms (frame %u)", g_cfg.net_stall_ms, frame);
    Sleep(g_cfg.net_stall_ms);
    LOGI("net: [lab] going on");
  }

  // This PC's pad, read now, is the pad of frame + delay; the first `delay` frames have none.
  if (frame == g_first)
    for (int d = 0; d < g_delay; ++d) set_pad(g_me, g_first + d, input::Pad{});
  set_pad(g_me, frame + g_delay, input::read_local(g_me));
  send_inputs();

  if (frame % 30 == 0) {
    auto hp = in2p::make<in2p::Hash>(in2p::Type::Hash);
    hp.frame = frame;
    hp.hash = state_hash();
    g_hash_mine[frame] = hp.hash;
    if (!lab_drops()) send(hp, Channel::Input);
    check_hash(frame);
  }

  // The other player's pad for this frame.
  int other = 1 - g_me;
  uint32_t t0 = net::now_ms(), resent = t0;
  pump_net(0);
  bool waited = false;
  while (g_phase == Phase::Playing && (g_newest[other] < frame || g_newest[other] < g_first)) {
    waited = true;
    pump_net(1);
    uint32_t now = net::now_ms();
    if (now - resent >= 8) {
      send_inputs();
      resent = now;
    }
    if (now - g_last_rx_ms > 15000) {
      end_session("nothing heard from the other player for 15 s");
      break;
    }
    if (now - t0 > 100 && !pump_messages()) {
      end_session("this game is closing");
      break;
    }
  }
  if (waited) {
    g_waited_ms += net::now_ms() - t0;
    ++g_waits;
  }
  if (frame % 1800 == 0) {
    NetLock lock;
    LOGI("net: frame %u, ping %u ms, waited %u ms in %u waits so far, the keeper stood in %u times%s", frame,
         g_t ? g_t->RttMs(g_peer) : 0, g_waited_ms, g_waits, g_kept.load(), g_desync ? ", DESYNC" : "");
  }
}

bool pad(int player, input::Pad& out) {
  if (g_phase != Phase::Playing || player < 0 || player >= in2p::kPlayers) return false;
  out = get_pad(player, g_frame);
  return true;
}

bool prepare(Config& cfg) {
  wchar_t v[200];
  if (GetEnvironmentVariableW(L"IN2NET_AFTER", v, 200)) {
    // the game before this one closes: its window, its ports and its place in Steam are still its own
    if (HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, wcstoul(v, nullptr, 10))) {
      WaitForSingleObject(old, 15000);
      CloseHandle(old);
    }
    SetEnvironmentVariableW(L"IN2NET_AFTER", nullptr);
  }
  auto narrow = [](const wchar_t* w) {
    std::string s;
    for (; *w; ++w) s += static_cast<char>(*w < 128 ? *w : '?');
    return s;
  };
  if (!GetEnvironmentVariableW(L"IN2NET_NET", v, 200)) return false;
  std::wstring mode = v;
  SetEnvironmentVariableW(L"IN2NET_NET", nullptr);
  if (mode != L"host" && mode != L"join") return false;
  cfg.net_mode = mode == L"host" ? Config::Net::Host : Config::Net::Join;
  cfg.net_restarted = true;
  cfg.background = true;
  cfg.net_wait_s = 45;  // (the other game is starting again right now: it is there in seconds or not at all)
  if (GetEnvironmentVariableW(L"IN2NET_VIA", v, 200)) cfg.net_via = narrow(v);
  if (GetEnvironmentVariableW(L"IN2NET_PEER", v, 200)) cfg.net_peer = _wcstoui64(v, nullptr, 10);
  if (GetEnvironmentVariableW(L"IN2NET_JOIN", v, 200)) cfg.net_connect = narrow(v);
  for (const wchar_t* name : {L"IN2NET_VIA", L"IN2NET_PEER", L"IN2NET_JOIN"}) SetEnvironmentVariableW(name, nullptr);
  return true;
}

void install(const Config& cfg) {
  g_cfg = cfg;
  InitializeCriticalSection(&g_net_lock);
  g_delay = cfg.net_delay;
  if (cfg.net_mode != Config::Net::Off) {
    g_phase = Phase::Meeting;
    LOGI("net: %s", cfg.net_mode == Config::Net::Host ? "this game hosts" : "this game joins");
  }
}

}  // namespace netplay
