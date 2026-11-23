# Contributing

[Русский](CONTRIBUTING.md) · **English**

Thank you for wanting to help. What the mod needs most now is real games and the logs after them.

## Reporting a problem

Open an [issue](https://github.com/int3re/INCREDIBLES-RISE-OF-THE-UNDERMINER-COOP/issues) and say:

- the versions of the mod and the launcher (the launcher shows both);
- what you did and what happened; for a network game, who was the host and how you connected (Steam or an
  address);
- attach, from the game's folder, `in2net.log` and — if the network game was started from the СЕТЕВАЯ ИГРА
  page — `in2net-menu.log`, **from both PCs**. Without the other side's log a network error can rarely be
  found.

Look the logs through before posting: they hold your Steam name and ID and the game's folder. Those lines
can be removed.

What is known and what has not been checked: [Status](README.en.md#status) and
[If something is wrong](docs/en/TROUBLESHOOTING.md).

## Proposing a change

1. Build the mod: [docs/en/BUILDING.md](docs/en/BUILDING.md).
2. Read how it works: [docs/en/ARCHITECTURE.md](docs/en/ARCHITECTURE.md). The one rule of the network game
   is that both copies must compute the same. Whatever changes how the game plays has to be the same on
   both PCs or travel over the network as input; whatever changes the picture only must touch neither the
   game's state nor its random number generator.
3. Check the change in the lab (`tools/lab`): a single run, a pair in a network game (`run_net2.py`), a
   meeting through the page (`run_page.py`). The two games' logs have to match frame for frame.
4. If the change changes how the game plays, raise the network protocol (`src/common/protocol.h`).

A few habits of this code:

- before a function of the game is hooked, its first bytes are compared with what they should be; if they
  differ, the feature is left to the game and the log says so — the game must not break on a file the mod
  does not know;
- the game's addresses and structures live in one file, `src/mod/game.h`, each with a note;
- the mod's menus and messages are made by the game's own means: its menu objects and its font. The mod
  has no windows of its own over the game;
- a new item of a menu or of the launcher goes where a player would look for it, not to the end of a list;
- comments in the code are in English; texts for the player are in Russian.

## What is not in the repository and will not be

Files of the game, the Steamworks SDK, the notes and tools of taking the game's executable apart. Please
do not send them in issues or pull requests.
