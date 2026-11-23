# Installing, updating and removing

[Documentation](README.md) · **Install** · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
[Controls](CONTROLS.md) · [Settings](SETTINGS.md) · [Troubleshooting](TROUBLESHOOTING.md)

**English** · [Русский](../INSTALL.md)

The mod and the launcher speak Russian, like the release of the game they are made for. Names of buttons
and menu items are given here as they are on the screen, with a translation in brackets.

## What you need

- **Windows 10 or 11.**
- **Your own copy of the game**: the Russian PC release «Суперсемейка: Подземная битва» (*The Incredibles:
  Rise of the Underminer*, 2005), with its `incrotu.exe`
  (SHA-256 `640aa7316d36218d4bd08173cba7c5bf79b374c8c7d96a069128be296c70f320`). The repository and the
  releases hold no files of the game.
- For a game over Steam: the Steam client running and any account. Nothing has to be bought.
- For a game by address over the internet: UDP port `47820` reachable at the host, or a virtual network
  (Radmin VPN, Hamachi, ZeroTier).

> **Another edition of the game.** The mod knows one `incrotu.exe`. With any other file it changes nothing
> and turns nothing on: the launcher says so when it installs («incrotu.exe другого издания»), and the game
> runs as if there were no mod.

## Install

1. Download [`in2net-launcher.exe`](https://github.com/int3re/INCREDIBLES-RISE-OF-THE-UNDERMINER-COOP/releases/latest/download/in2net-launcher.exe).
   It is one file of about 4.6 MB with the mod inside; it installs itself nowhere and can lie anywhere.
2. Start it. If it has not found the game, press **Изменить** (Change) and choose the folder that holds
   `incrotu.exe`.
3. Press **УСТАНОВИТЬ МОД** (Install the mod). No internet is needed: the files come out of the launcher.
4. Press **ИГРАТЬ** (Play).

<p align="center">
  <img src="../images/launcher-install.png" alt="The launcher: the mod is not installed yet" width="410">
  <img src="../images/launcher-home.png" alt="The launcher: the mod is installed" width="410">
</p>

> The launcher is not signed with a certificate, so Windows may show "Windows protected your PC". Press
> **More info → Run anyway**. The SHA-256 of the file is in `in2net-latest.json` of every
> [release](https://github.com/int3re/INCREDIBLES-RISE-OF-THE-UNDERMINER-COOP/releases).

## What is put where

Everything goes next to `incrotu.exe`. The game's own files are not touched, and `incrotu.exe` is not
changed on the disk: the mod works in memory.

| File | What it is |
|---|---|
| `winmm.dll` | [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader): loads the mod into the game. If one is there already, it is kept and used |
| `in2net.asi` | the mod |
| `in2net.ini` | the mod's settings, each with a note. A file that is there already is kept |
| `steam_api.dll` | Steam's library (needed only for a game over Steam) |
| `steam_appid.txt` | made by itself at the first use of Steam |
| `in2net-README.md` | a short description in Russian |
| `in2net-install.txt` | the list of files the launcher put in; by it the launcher removes exactly them |

What appears later, by itself:

| File or folder | What it is |
|---|---|
| `in2net.log` | the log of the last run |
| `in2net-menu.log` | the log of the run a network game was started from |
| `in2net-coop\` | saves of network games (the host's progress); your own saves are not touched |

The launcher's own files are outside the game's folder: its settings in `%APPDATA%\in2net\launcher.ini`,
downloads and copies of settings made before a change in `%LOCALAPPDATA%\in2net\cache`.

## Update

Start the launcher. It asks GitHub for the newest release at every start:

- a newer **launcher** is taken by itself: downloaded, checked by size and SHA-256, put in the old one's
  place and started again;
- a newer **mod** is offered with the button **ОБНОВИТЬ ДО …** (Update to …). Your `in2net.ini`, your keys
  and your saves stay.

Under the two versions the launcher says whether friends on the older version can still be played with
(the same network protocol) or everybody has to update.

<p align="center">
  <img src="../images/launcher-update.png" alt="The launcher: a newer version is out" width="410">
  <img src="../images/launcher-downloading.png" alt="The launcher: downloading the update" width="410">
</p>

## Remove

Launcher → the settings (the sliders icon at the top right) → tab **ЛАУНЧЕР** → **Удалить мод** (Remove
the mod), then **Точно удалить?** (Sure?). Exactly the files of `in2net-install.txt` are removed. By hand:
delete `winmm.dll` and `in2net.asi`.

## Without the launcher's window

```
in2net-launcher.exe /install "C:\Games\Incredibles 2" /quiet
```

```
in2net-launcher.exe /uninstall "C:\Games\Incredibles 2" /quiet
```

The exit code is 0 when it is done and 2 when it is not (no `incrotu.exe` in the folder, the game is
running). Without `/quiet` the result is shown in a message.

## By hand

Build the mod ([Building](BUILDING.md)) or take the files out of an installed copy, and put `winmm.dll`,
`in2net.asi`, `in2net.ini` and, for Steam, `steam_api.dll` next to `incrotu.exe`.
