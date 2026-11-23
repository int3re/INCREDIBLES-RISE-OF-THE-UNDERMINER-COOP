# Building from source

[Documentation](README.md) · [How it works](ARCHITECTURE.md) · [History](CHANGELOG.md)

**English** · [Русский](../BUILDING.md)

## What you need

- **Windows** and **Visual Studio 2022 or newer** with the C++ tools (MSVC, x86).
- **CMake 3.20** or newer (the one that comes with Visual Studio will do).
- **Python 3** — only for the tools (the release's manifest, the lab).

Everything is built for **Win32**: `incrotu.exe` is a 32-bit process, and so is whatever goes into it.
CMake stops with an error if it is configured for another platform.

## What is taken from outside

| What | Where the build looks for it | Without it |
|---|---|---|
| **ENet** (UDP) | `../tmnt2003-netplay/third_party/enet` — the tree of [TMNT-2003-COOP](https://github.com/int3re/TMNT-2003-COOP), cloned with `--recursive` next to this one. Another place: `-DTMNT1_DIR=<path>` | the build does not start |
| **Ultimate ASI Loader**, 32-bit | `../tmnt2003-netplay/third_party/ultimate-asi-loader/bin/Win32/Release/dinput8.dll` (built there by `tools/build_asi_loader.ps1`). Any 32-bit build of the [loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader): `-DUAL_DLL=<file>` | there is no `winmm.dll` in the result and the launcher is not built |
| **Steamworks SDK** | `../steamworks_sdk_165/sdk`. Another place: `-DSTEAMWORKS_SDK_DIR=<path to sdk>` | the mod is built for games by address only |

MinHook is in the repository (`third_party/minhook`). The Steamworks SDK is Valve's and is never a part
of the repository.

## Build

```
git clone https://github.com/int3re/INCREDIBLES-RISE-OF-THE-UNDERMINER-COOP
git clone --recursive https://github.com/int3re/TMNT-2003-COOP tmnt2003-netplay
cd INCREDIBLES-RISE-OF-THE-UNDERMINER-COOP
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The result is in `build/dist/Release`:

| File | What |
|---|---|
| `in2net-launcher.exe` | the launcher with the mod inside: the one file of a release |
| `in2net.asi`, `in2net.ini`, `in2net-README.md` | the mod, its settings, its short description |
| `winmm.dll` | Ultimate ASI Loader |
| `steam_api.dll` | Steam's library, if the SDK was found |

Only the launcher: `cmake --build build --config Release --target in2net_launcher`. After a change of a
version (below) configure again, so that the launcher's package knows it.

The C runtime is linked statically: a player needs no Visual C++ redistributable.

## The tree

| Folder | What is in it |
|---|---|
| `src/mod` | the mod, `in2net.asi`: see [How it works](ARCHITECTURE.md) |
| `src/common` | what the mod's parts share: the log, the network transports (ENet, Steam, the lab's stand-in for Steam), the protocol, SHA-256 |
| `src/launcher` | `in2net-launcher.exe`; its fonts and icon in `assets` |
| `dist` | what goes to a player as it is: `in2net.ini`, the short description, `release.json` |
| `docs` | this documentation and its pictures |
| `tools` | the release's manifest, the launcher's icon and fonts, the picture's shader, the lab |
| `third_party/minhook` | MinHook |

## Versions

| What | Where |
|---|---|
| the mod's version | `src/common/version.h`, `IN2NET_VERSION` |
| the network protocol | `src/common/protocol.h`, `kVersion` — changed whenever two versions would not play the same game |
| the launcher's version | `src/launcher/launcher.h`, `kLauncherVersion` — changed when the launcher itself changes |
| what a release says of itself | `dist/release.json`: version, protocol, date, a title (40 characters at most), one to four notes (110 characters each at most), the protocols of the earlier versions |

## A release

A release is two files under the tag `v<version>`: `in2net-launcher.exe` and `in2net-latest.json`.

1. Fill `dist/release.json` and the versions **before** the build; configure and build.
2. `python tools/release_manifest.py build/dist/Release/in2net-launcher.exe build/dist/Release/in2net-latest.json`
   — the manifest: what is in `release.json` plus the launcher file's size and SHA-256. It refuses a
   launcher that was not built from this source.
3. Publish both files as the release's assets. A launcher asks for
   `<repository>/releases/latest/download/in2net-latest.json` and takes the launcher file from
   `<repository>/releases/download/v<version>/`.

The repository's address is in `src/launcher/launcher.h` (`kRepoUrl`): a fork that wants its own updates
changes it there.

## The lab

`tools/lab` runs copies of the game with the mod without ever showing them: the window is off the screen
and never takes the focus, the sound is muted, the run ends by itself. It needs your own copies of the
game in `../test-in2` and `../test-in2b` (the game is not a part of the repository) and Python with Pillow.

| Script | What it does |
|---|---|
| `run_one.py <seconds> "<script>" [debug lines]` | one copy; presses by frame, pictures of chosen frames |
| `run_twice.py` | the same run twice: are the two logs the same? |
| `run_net2.py <seconds> "<script>"` | two copies in one network game by address; the two games' logs are compared frame by frame |
| `run_page.py <seconds> "<script>"` | two copies meet on the СЕТЕВАЯ ИГРА page (through a stand-in for Steam, or by address) and start the network game from it |
| `soak.py <seconds> <level> …` | a pair in a network game with random play, level after level |

A script is lines of `<frame>[-<frame>] [p2.]<control>`: `420 start`, `600 down`, `700 a`,
`3000-3030 p1.mouse.left`. The lab's switches are the `[debug]` section of `in2net.ini`; each is described
in `src/mod/config.h` — a lossy network, a stalled game, a slowed video card (`slow`), bindings told to
the game without touching the registry (`bind`), and more.

What the lab cannot check is the game's real window in front of a player: the keyboard, a real mouse,
Alt+Tab. Those are checked by playing.

## The launcher without its window

| Command | What it does |
|---|---|
| `in2net-launcher.exe --shot <screen> <file.png>` | a screen of the launcher as a picture: `ready`, `update`, `downloading`, `install`, `nogame`, `offline`, `settings`, `graphics`, `sound`, `controls`, `net` |
| `in2net-launcher.exe --drive "<steps>" --pick <game folder> --settings <ini> --cache <folder> --out <log> --keys <registry subkey>` | the launcher run by a script, never shown: `until:<screen>:<ms>`, `click:<name>`, `mouse:x,y`, `keydown:<vk>:<scan>:<ext>`, `state`, `shot:<png>`, `quit`. With `--keys` the game's bindings and sound levels are read and written under another key, not the game's own |
| `in2net-launcher.exe --check --out <log>` | asks GitHub for the newest release and writes what came |

## What is not in the repository

- Files of the game, and the Steamworks SDK.
- The notes and tools of taking the game's executable apart. The addresses and structures the mod uses
  are in the source, `src/mod/game.h`, each with a note on what it is.
