# History of versions

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
[Controls](CONTROLS.md) · [Settings](SETTINGS.md) · [Troubleshooting](TROUBLESHOOTING.md)

**English** · [Русский](../CHANGELOG.md)

The newest version is at the top. **Protocol** is the network protocol: players meet in a network game
only on versions with the same one. Versions 0.1.0 – 0.3.1 were not published; the first public release
is 0.4.0.

| Version | Launcher | Protocol | Date | In a line |
|---|---|---|---|---|
| [0.5.2](#052) | 1.2.0 | 4 | 22 Nov 2026 | single player keeps its speed on a busy computer |
| [0.5.1](#051) | 1.2.0 | 4 | 22 Nov 2026 | the game's settings screen held to 60 frames a second |
| [0.5.0](#050) | 1.2.0 | 4 | 22 Nov 2026 | mouse buttons, the main menu's order, sound levels |
| [0.4.0](#040) | 1.1.0 | 4 | 22 Nov 2026 | a better picture, keys in the launcher, the launcher's new look |
| [0.3.1](#031) | 1.0.0 | 4 | 22 Nov 2026 | messages on the screen in the game's font; the first launcher |
| [0.3.0](#030) | — | 4 | 22 Nov 2026 | Steam, meeting inside the game, a native menu |
| [0.2.1](#021) | — | 3 | 22 Nov 2026 | what the checks of 0.2.0 found |
| [0.2.0](#020) | — | 2 | 22 Nov 2026 | wide screen, a camera of one's own |
| [0.1.0](#010) | — | 1 | 22 Nov 2026 | the first network game |

## 0.5.2

- **Single player keeps its speed when the computer cannot draw 60 frames a second.** The mod counts the
  game's time by frames, 1/60 s each. With the video card busy elsewhere the game ran in slow motion and
  its sound was torn. Outside a network game a frame that comes one to three sixtieths late now takes
  them with it: the game makes a longer step, as the original does, and stays at its real speed down to
  15 frames a second.
- In a network game nothing changes: both computers must make the same steps, so a slow one slows both.
- `in2net.log` says how many frames a second were shown (every 1800 frames), and how many came late.

## 0.5.1

- **The game's own settings screen (ОПЦИИ) is held to 60 frames a second.** It is a loop of its own that
  relied on the engine's frame limiter — which the mod takes out to run the game at 60 instead of 31 — and
  so drew 550 frames a second and more, loading the video card for nothing.

## 0.5.0

Launcher 1.2.0.

- **Buttons of the mouse.** An action can have a button of the mouse besides its key: left, right, middle
  and the two side ones. They are the game's own bindings; the mod tells the game the buttons' state
  itself, which makes the side buttons work (the game knows three) and works in a network game. In the
  main menu the mouse stays what it was.
- **The main menu.** СЕТЕВАЯ ИГРА is the third button, right under ДВА ИГРОКА (it was the seventh, under
  БОНУС). The seven buttons together are exactly as tall as the picture on their left.
- **Launcher:** a button of the mouse is set in the action's own cell of УПРАВЛЕНИЕ, like a key; a new tab
  ЗВУК with the game's two volume levels.

## 0.4.0

Launcher 1.1.0. The first public release.

- **A better picture**, none of it a part of the game's logic: smoothed edges (MSAA up to 8x), anisotropic
  filtering up to 16x, a sharpening pass over the finished frame, fuller colours on request. Two players
  of a network game may have them different.
- **Launcher:** a new look — the heroes' suit in red, black, yellow and orange, four "suits" to choose
  from; a tab УПРАВЛЕНИЕ with the keys of both players, which are the game's own; rows of the picture's
  quality on ГРАФИКА.

## 0.3.1

The first launcher, 1.0.0: one file with the mod inside; finds the game, installs and removes the mod,
starts the game, edits `in2net.ini`.

- **Messages on the screen written by the game itself**, in its own font: who you are in the network
  game, that the other player has left, that the connection is lost.

## 0.3.0

Protocol 4.

- **Steam.** Friends see each other's game; the host can invite a friend from inside the game. Steam's
  networking carries the game, so no port has to be opened.
- **Meeting inside the game.** A new item of the main menu, СЕТЕВАЯ ИГРА, and a network page — both made of
  the game's own menu objects: its frames, its font, its highlight. The players meet on the page over
  Steam or by a typed address; the host's start closes both games and opens them again in the network game.
- The game no longer pulls the mouse pointer into the middle of its window while the window is not in front.

## 0.2.1

Protocol 3. Found by checking 0.2.0 in the lab.

- The game's own settings screen is not opened in a network game: it stopped one computer only.
- "Distance clipping" of the display options is off for both players in a network game: it changes what
  the game computes.
- The host refuses a guest whose `data\in2.ini` differs.
- Closing the game tells the other player at once.
- A level that loads for long, or a window being dragged, no longer breaks the connection.

## 0.2.0

Protocol 2.

- **Wide screen.** The frame has the shape of the monitor and more is seen at the sides; the interface and
  the menus are not stretched. The camera is a part of the game's logic, so every computer computes the
  game with one and the same view and draws its own.
- **A camera of one's own** in a network game: it follows the hero of this computer.

## 0.1.0

Protocol 1.

- **The first network game:** the game's two-player mode on two computers, by a typed address. Both
  copies play the whole game and only the controls go over the network; the game's clock is the same on
  both — 1/60 s a frame, 60 frames a second instead of the original's 31.
- A window or a window without a frame instead of the exclusive full screen; any size of the frame.
- The connection lives through lost packets and breaks of a few seconds.
