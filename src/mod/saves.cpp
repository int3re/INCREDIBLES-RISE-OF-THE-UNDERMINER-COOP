#include "mod/saves.h"

#include <windows.h>

#include <cstring>
#include <string>

#include "common/log.h"
#include "mod/game.h"

namespace saves {
namespace {

Config g_cfg;
bool g_done = false;

}  // namespace

void install(const Config& cfg) { g_cfg = cfg; }

void on_window() {
  if (g_done) return;
  g_done = true;
  char* buf = reinterpret_cast<char*>(game::kSaveDir);
  LOGI("saves: the game's folder is %s", buf);
  // A network game keeps its own saves (the host's co-op progress; the guest's folder is a copy
  // of it for the session), apart from the player's own.
  std::string name = g_cfg.save_dir;
  if (name.empty() && g_cfg.net_mode != Config::Net::Off) name = "in2net-coop";
  if (name.empty()) return;
  char exe[MAX_PATH];
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir = exe;
  size_t slash = dir.find_last_of("\\/");
  dir = dir.substr(0, slash + 1) + name;
  if (dir.size() + 1 > game::kSaveDirSize) {
    LOGE("saves: %s is too long a path; the game's own folder is used", dir.c_str());
    return;
  }
  CreateDirectoryA(dir.c_str(), nullptr);
  memset(buf, 0, game::kSaveDirSize);
  memcpy(buf, dir.c_str(), dir.size());
  LOGI("saves: redirected to %s", buf);
}

}  // namespace saves
