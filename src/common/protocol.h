// in2net wire protocol. Little endian, packed. Bump kVersion on ANY change to a
// struct layout, channel use or field meaning: peers with different versions
// refuse each other.
#pragma once
#include <cstdint>
#include <cstring>

namespace in2p {

constexpr uint32_t kMagic = 0x4E324E49;  // bytes 'I','N','2','N'
// 2 (in2net 0.2.0): the game's logic works with one view on every PC (16:9); the Input packet carries an ack.
// 3 (in2net 0.2.1): Hello carries a fingerprint of the game's data; no mouse, no settings screen and no
//   distance clipping in a session.
// 4 (in2net 0.3.0): the network page: players meet in their running games (Knock / Lobby / Go) and the games
//   start again into the session; Steam besides the address.
constexpr uint16_t kVersion = 4;
constexpr uint16_t kDefaultPort = 47820;

constexpr uint8_t kChannelControl = 0;  // reliable, ordered
constexpr uint8_t kChannelInput = 1;    // unreliable: inputs, hashes
constexpr uint8_t kChannelCount = 2;

constexpr int kPlayers = 2;        // Mr. Incredible and Frozone
constexpr int kInputMax = 120;  // pads in one Input packet at most (everything the other side has not confirmed)

enum class Type : uint8_t {
  Hello = 1,     // client -> host: who I am
  Welcome = 2,   // host -> client: the session's settings
  SaveFile = 3,  // host -> client: one file of the host's co-op saves
  Start = 4,     // host -> client: everything is sent; play from this frame
  Input = 5,     // both ways: the sender's pad for the last frames
  Hash = 6,      // both ways: a fingerprint of the game's state at a frame
  Bye = 7,       // both ways: leaving
  Reject = 8,    // host -> client: not let in, and why
  Alive = 9,     // both ways: "this game is busy (loading a level) but here"; a Header alone
  // The network page (the games are not in a session yet):
  Knock = 10,    // guest -> host: let me into your game (a Hello by layout)
  Lobby = 11,    // host -> guest: you are in; wait for the start
  Go = 12,       // host -> guest: start your game again into the session
  GoAck = 13,    // guest -> host: starting again (a Header alone)
};

#pragma pack(push, 1)

struct Header {
  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint8_t type = 0;
  uint8_t reserved = 0;
};

struct Hello {
  Header h;
  uint8_t exe_hash[8];   // first bytes of the SHA-256 of incrotu.exe
  uint8_t data_hash[8];  // first bytes of the SHA-256 of data\in2.ini (the game's rules: levels, damage, rewards)
  char mod_version[16];
  char name[32];
};

struct Welcome {
  Header h;
  uint8_t player;        // the client's player number (1)
  uint8_t input_delay;   // frames between reading a pad and the frame that uses it
  uint16_t save_files;   // SaveFile packets that follow
  uint32_t first_frame;  // the game frame the session starts at
  char host_name[32];
};

struct SaveFile {
  Header h;
  uint32_t size;
  char name[64];
  // `size` bytes follow
};

struct Start {
  Header h;
};

// A player's pad for one frame (input::Pad on the wire).
struct PadWire {
  uint8_t buttons;
  uint8_t analog[8];
  int16_t lx, ly, rx, ry;
};

struct Input {
  Header h;
  uint8_t player;
  uint8_t count;    // pads that follow, for frames newest - count + 1 .. newest
  uint32_t newest;
  uint32_t ack;     // the newest frame of the receiver's pads the sender has: nothing older need be sent again
  // PadWire pads[count]
};

struct Hash {
  Header h;
  uint32_t frame;
  uint32_t hash;
};

// The host's answer to a Knock.
struct Lobby {
  Header h;
  char host_name[32];
};

// The host's start: how the guest's new game finds the host's.
struct Go {
  Header h;
  uint8_t via;       // 1 = by address (ENet), 2 = Steam
  uint8_t reserved;
  uint16_t port;     // by address: the host's port
  uint64_t steam_id; // Steam: the host's SteamID64
};

struct Reject {
  Header h;
  char reason[96];
};

#pragma pack(pop)

template <typename T>
T make(Type t) {
  T p{};
  p.h.magic = kMagic;
  p.h.version = kVersion;
  p.h.type = static_cast<uint8_t>(t);
  return p;
}

// The packet's type if it is one of ours (right magic and version), else 0.
inline uint8_t type_of(const void* data, size_t size) {
  if (size < sizeof(Header)) return 0;
  Header h;
  std::memcpy(&h, data, sizeof(h));
  if (h.magic != kMagic || h.version != kVersion) return 0;
  return h.type;
}

}  // namespace in2p
