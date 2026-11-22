// Where two players meet before a network game: in their running games, on the network page.
// One opens a game (a Steam lobby for friends and a UDP port for an address), the other finds it
// among friends' games or dials the address; the host's start then closes both games and starts
// them again into the session (netplay: a session begins with the process, which is what makes
// both games the same from their first frame).
//
// Game thread only. The page (netpage) shows view() and calls the rest.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mod/config.h"

namespace netlobby {

enum class State {
  Closed,    // the page is not up
  Menu,      // looking: friends' games, an address
  Hosting,   // this player's game is open
  Joining,   // knocking at a host's game
  Joined,    // in a host's game, waiting for the start
  Starting,  // the start is given: this game is about to start again
};

struct Game {
  uint64_t lobby = 0;
  std::string host;   // the host's Steam name
  bool ok = false;    // can be joined
  std::string why;    // why not (Russian)
};

struct Friend {
  uint64_t id = 0;
  std::string name;
  bool here = false;  // runs the mod right now
};

struct View {
  State state = State::Closed;
  bool steam = false;         // Steam (or the lab's stand-in) is up
  std::string steam_note;     // why it is not (Russian)
  std::string me;             // this player's name
  std::string other;          // the other player's name ("" = nobody yet)
  bool other_in = false;
  std::vector<Game> games;
  std::vector<Friend> friends;
  std::string addresses;      // hosting: where an address player dials
  std::string error;          // the last thing that went wrong (Russian)
  std::string invited;        // hosting: the last friend invited
};

void install(const Config& cfg);

void open();
void close();
void tick();  // once a game frame while the page is up

void host();
void join_game(uint64_t lobby);
void join_address(const std::string& address);
void leave();   // back to looking
void start();   // host: the start
void invite(uint64_t friend_id);

const View& view();

}  // namespace netlobby
