# Settings

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
[Controls](CONTROLS.md) · **Settings** · [Troubleshooting](TROUBLESHOOTING.md)

**English** · [Русский](../SETTINGS.md)

There are three places where settings live:

| Where | What | Who changes it |
|---|---|---|
| `in2net.ini` next to `incrotu.exe` | the mod's own: the window, the picture, the network game | the launcher's tabs ГРАФИКА and СЕТЬ, or a text editor |
| the game's registry key | the game's own: keys, mouse buttons, sound levels, its display options | the launcher's tabs УПРАВЛЕНИЕ and ЗВУК, or the game's ОПЦИИ screen |
| `%APPDATA%\in2net\launcher.ini` | the launcher's own | the launcher's tab ЛАУНЧЕР |

Everything is read by the game **at its start**: a change made while the game runs takes effect the next
time.

## The launcher's rows and what they write

### ГРАФИКА

| Row | Values | `in2net.ini`, `[video]` |
|---|---|---|
| **Окно игры** (the game's window) | На весь монитор · В окне · Полный экран игры | `mode = borderless \| window \| game` |
| **Разрешение** (resolution) | Как монитор · Как в ОПЦИЯХ игры · the monitor's modes | `resolution = auto \| game \| 1920x1080` |
| **Формат кадра** (shape of the frame) | Широкий · 4:3 | `aspect = wide \| 4:3` |
| **Верт. синхронизация** (vertical sync) | Сама · Выключена · Включена | `vsync = -1 \| 0 \| 1` |
| **Камера в сети** (camera in a network game) | Своя · Общая | `own_camera = 1 \| 0` |
| **Игра в фоне** (the game in the background) | Ждёт · Идёт | `background = 0 \| 1` |
| **Сглаживание краёв** (smoothed edges) | Выключено · 2x · 4x · 8x | `antialiasing = 0 \| 2 \| 4 \| 8` |
| **Чёткость текстур** (sharp textures at an angle) | Как в игре · 4x · 8x · 16x | `anisotropy = 0 \| 4 \| 8 \| 16` |
| **Резкость кадра** (sharpness of the frame) | Выключена · Лёгкая · Средняя · Сильная · Очень сильная | `sharpen = 0 \| 20 \| 35 \| 55 \| 80` |
| **Цвета** (colours) | Как в игре · Чуть ярче · Ярче · Сочные | `colors = 0 \| 20 \| 40 \| 65` |

Rows that have no effect now are shown pale: with «Полный экран игры» the mod leaves the picture alone, so
the resolution, the shape of the frame and the smoothing are the game's own.

### ЗВУК

| Row | Values | The game's registry key |
|---|---|---|
| **Звуки** (sounds) | 0 – 100 % by fives | `slider_soundvolume` (the game's default: 100) |
| **Музыка** (music) | 0 – 100 % by fives | `slider_musicvolume` (the game's default: 80) |

### УПРАВЛЕНИЕ

The keys and the mouse's buttons of both players: see [Controls](CONTROLS.md).

### СЕТЬ

| Row | Values | `in2net.ini`, `[net]` |
|---|---|---|
| **Соединение** (connection) | Steam и адрес · Только Steam · Только адрес | `transport = auto \| steam \| enet` |
| **Игру в Steam видят** (who sees the game in Steam) | Друзья · Все | `lobby = friends \| public` |
| **Задержка ввода** (input delay) | 1 – 12 кадров | `delay = 1 … 12` |

## `in2net.ini`

The file comes with a note at every line (in Russian). A line that starts with `;` is a note. A key that
is not in the file has its default; a file that is not there means all the defaults.

### `[video]`

| Key | Default | Meaning |
|---|---|---|
| `mode` | `borderless` | `borderless` — a window without a frame over the whole monitor: the desktop's resolution is not changed and Alt+Tab works at once. `window` — an ordinary window. `game` — as in the original: an exclusive full screen; the mod then does not touch the picture |
| `aspect` | `wide` | `wide` — the frame has the shape of the monitor, more is seen at the sides (as tall as in 4:3); the interface and the menus keep their proportions. `4:3` — the original picture with black bars at the sides |
| `resolution` | `auto` | the size of the frame the game draws: `auto` — the monitor's (without a frame) or three quarters of the desktop's height (in a window); `1280x720` — a size of your own; `game` — the one chosen on the game's ОПЦИИ screen |
| `own_camera` | `1` | in a network game: `1` — the camera follows your hero; `0` — the game's one camera for both. Only the picture changes: the game is computed the same on both computers |
| `window_width`, `window_height` | `0` | the size of the window for `mode = window`; `0` — the size of the frame |
| `vsync` | `-1` | `-1` — by itself (off in a window: the mod holds 60 frames a second), `0` — off, `1` — on |
| `background` | `0` | `1` — the game goes on while its window is not in front; `0` — it waits, as in the original. A network game always goes on: the other player would wait otherwise |
| `antialiasing` | `4` | smoothed edges (MSAA): `0`, `2`, `4`, `8`. Works in `borderless` and `window` when the frame fills the window; a video card that cannot do that much gets less |
| `anisotropy` | `16` | anisotropic filtering: `0` — as in the game, `2`, `4`, `8`, `16` |
| `sharpen` | `35` | sharpness of the finished frame, `0` … `100` |
| `colors` | `0` | saturation of the colours, `0` — as in the game … `100` |

### `[net]`

| Key | Default | Meaning |
|---|---|---|
| `mode` | `off` | `off` — as usual: play alone, two at one computer, or make a network game from the menu. `host` — make a game at the start: the game waits for the second player. `join` — join the game at `connect` |
| `transport` | `auto` | how to connect from the СЕТЕВАЯ ИГРА page: `auto` — over Steam if it runs, and by address; `steam` — Steam only; `enet` — by address only |
| `lobby` | `friends` | who sees a game made over Steam: `friends`, or `public` — everybody who has the mod |
| `connect` | `127.0.0.1` | the host's address: an IP or a name, with a port if needed (`192.168.0.10:47820`) |
| `port` | `47820` | the UDP port the host listens on |
| `name` | `player` | your name in a network game, in Latin letters; while it is `player`, the name is taken from Steam |
| `delay` | `2` | input delay in frames, `1` … `12` |
| `wait` | `120` | how many seconds a game started with `mode = host` waits for the second player |

### `[log]`

| Key | Default | Meaning |
|---|---|---|
| `level` | `info` | how much goes into `in2net.log`: `error`, `warn`, `info`, `debug` |

There is also a `[debug]` section for the test lab (windows off the screen, scripted presses, a slowed
video card and so on). It is described in the source, `src/mod/config.h`, and is never needed to play.

## The game's own settings

The game keeps its settings in the registry, under
`HKEY_CURRENT_USER\Software\THQ\the incredibles rise of the underminer`:

| Value | What |
|---|---|
| `slider_soundvolume`, `slider_musicvolume` | the sound levels, 0 – 100 |
| `p0\<ACTION>_0`, `p1\<ACTION>_0` | the key of an action of player 1 / player 2 |
| `p0\<ACTION>_1`, `p1\<ACTION>_1` | a second binding of the action; the launcher puts a button of the mouse here |
| `listbox_displayres`, `cbox_hdr`, `cbox_shadows`, `cbox_fsaa`, `cbox_distanceclipping`, `slider_clippingdistance` | the display options of the ОПЦИИ screen |

A binding is a number, `(device << 16) | code`: device 1 is the keyboard (the code is the key's
DirectInput number, `0xFF` — none), device 2 the mouse (9 — left, 10 — right, 11 — middle, 12 and 13 — the
side buttons), devices 3 and up the gamepads.

Before the launcher changes the keys or `in2net.ini` for the first time in a run, it puts a copy of what
was there into `%LOCALAPPDATA%\in2net\cache` (`keys.before-launcher.txt`, `in2net.ini.before-launcher`).

In a network game the display option "distance clipping" is off on both computers whatever the registry
says: it changes what the game computes, not only what is drawn.
