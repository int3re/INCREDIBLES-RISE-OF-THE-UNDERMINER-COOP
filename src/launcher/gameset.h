// The mod's settings on the launcher's ГРАФИКА and СЕТЬ pages: what in2net keeps in in2net.ini next to
// incrotu.exe - the picture ([video]) and the network game ([net]). A change is written at once and is
// read by the game at its next start. (The keys are the game's own: keys.h. Its sound levels and its
// display options are on the game's own ОПЦИИ screen.)
#pragma once
#include <string>

namespace lz {
namespace gset {

enum Row {
  kMode, kResolution, kAspect, kVsync,         // ГРАФИКА: the screen
  kOwnCamera, kBackground,                     //          the game
  kAntialias, kAniso, kSharpen, kColors,       //          the picture
  kTransport, kLobby, kDelay,                  // СЕТЬ
  kRows
};
constexpr int kFirstGame = kOwnCamera, kFirstPicture = kAntialias, kFirstNet = kTransport;

enum class Kind { Choice, Pair };  // values with arrows; two buttons

// Reads in2net.ini where the game sees it (again: the player may have changed it).
void load(const std::wstring& game_dir);
// Values for a picture: no file is read or written.
void load_canned();

bool has_mod();  // in2net.ini is there

Kind kind(Row r);
const wchar_t* label(Row r);
const char* key(Row r);            // its name for the lab: mode, aspect, ...
bool usable(Row r);
int count(Row r);                  // how many values it has now
int value(Row r);                  // the one set
std::wstring name(Row r, int v);   // a value's words

// Sets a value and writes the file. False: not written, error() says why.
bool set(Row r, int v);
// The mod's defaults for the rows of a page (1 ГРАФИКА, 2 СЕТЬ).
bool reset(int page);
const std::wstring& error();
std::wstring ini_file();  // where the settings are read and written (a copy Windows keeps for the game
                          // of a protected folder's file stands in for the folder's own)

}  // namespace gset
}  // namespace lz
