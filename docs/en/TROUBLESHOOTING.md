# If something is wrong

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
[Controls](CONTROLS.md) · [Settings](SETTINGS.md) · **Troubleshooting**

**English** · [Русский](../TROUBLESHOOTING.md)

## The launcher

**Windows says "Windows protected your PC".**
The launcher is not signed with a certificate. Press **More info → Run anyway**. The file's SHA-256 is in
`in2net-latest.json` of the [release](https://github.com/int3re/INCREDIBLES-RISE-OF-THE-UNDERMINER-COOP/releases/latest).

**The launcher does not find the game.**
Press **ВЫБРАТЬ ПАПКУ ИГРЫ** (or **Изменить** at the bottom of the window) and choose the folder that holds
`incrotu.exe`.

**«incrotu.exe другого издания» when installing.**
Your `incrotu.exe` is not the one the mod is made for. The mod will change nothing in such a game. The
supported file is the Russian release's, SHA-256 `640aa731…6c70f320` ([Install](INSTALL.md)).

**GitHub does not answer / the update does not download.**
Settings → **ЛАУНЧЕР** → **Прокси для GitHub**: try **Авто**, or **Свой** with the address of your proxy.
Without GitHub the launcher still installs the mod it carries and starts the game.

**The game is running and the launcher will not install.**
Close the game, or let the launcher ask it to close (**ЗАКРЫТЬ ИГРУ И ОБНОВИТЬ**).

## The game

**The game is slow, the sound is torn.**
Look at `in2net.log` in the game's folder. Every 1800 frames it says how fast the game went:

```
video: 3600 frames presented, 60.0 a second lately
sim: 1715 of the last 1800 frames came late (this PC shows fewer than 60 pictures a second now) ...
```

- Fewer than 60 a second means the computer cannot draw that many: often because another program is using
  the video card (a render, a second game, a browser with video). Close it, or lower
  **ГРАФИКА → Сглаживание краёв** in the launcher (8x → 4x → off).
- Alone, the game keeps its real speed down to 15 frames a second (since 0.5.2); it is only less smooth.
  **In a network game a slow computer slows the game for both players**: the two games make every step
  together.

**The game's own settings screen (ОПЦИИ) does not open.**
In a network game it is not opened on purpose: it would stop the game on one computer only. Set the keys
and the volume in the launcher.

**The mouse's buttons do nothing.**
- They act in a level, not in the main menu.
- A change made in the launcher takes effect at the next start of the game.
- The side buttons work only with the mod installed.
- The game's window has to be the one in front.

**The pointer of the mouse does not move in a menu.**
In a network game the pointer is off (the players' mice are not the same); walk the menus with the keys.

**The picture is 4:3 with black bars.**
ГРАФИКА → **Формат кадра**: **Широкий**. With **Окно игры: Полный экран игры** the mod does not touch the
picture at all.

**Alt+Tab freezes the game / the game stops when I switch away.**
By default the game waits while its window is not in front, as the original does. ГРАФИКА →
**Игра в фоне: Идёт** lets it go on. A network game always goes on.

## A network game

**The guest is not let in.**
The host refuses a guest whose `incrotu.exe`, `data\in2.ini` or network protocol differs; the reason is
written on the guest's page. Both players need the same game and versions of the mod with the same
protocol (the launcher shows it under each version).

**The friend's game is not in the list (Steam).**
Steam must be running at both players, and they must be friends in Steam. A game made with
**Игру в Steam видят: Друзья** is seen by friends only. Steam shows the status "Spacewar": that is as it
should be.

**Cannot join by address.**
The guest needs a way to the host over UDP, port `47820`: check the address the host sees in the frame on
the right, the host's firewall, the port forwarding at the router — or use a virtual network (Radmin VPN,
Hamachi, ZeroTier).

**Both games closed after НАЧАТЬ ИГРУ.**
That is how a network game begins: both games close and open again a few seconds later, already in the
network game.

**The other player's hero stands still.**
The other player has left or the connection was lost for more than 15 seconds; a message at the top of
the frame says which. You go on alone. There is no coming back into a game yet.

**My game stands and waits.**
The other player's game is busy (a long load) or slow. See "The game is slow" above.

## Reporting a problem

Open an [issue](https://github.com/int3re/INCREDIBLES-RISE-OF-THE-UNDERMINER-COOP/issues) and attach, from
the game's folder of **both** computers of a network game:

- `in2net.log` — the log of the last run;
- `in2net-menu.log` — the log of the run the network game was started from.

The launcher opens both: the settings → **ЛАУНЧЕР** → **Открыть журнал**, **Журнал встречи**. Say what
version the launcher shows, what you did and what happened.

> The logs hold your Steam name and Steam ID and the folder the game is in. Look them through before
> posting them in public, or remove those lines.

## Removing the mod

Launcher → the settings → **ЛАУНЧЕР** → **Удалить мод**. Or delete `winmm.dll` and `in2net.asi` from the
game's folder by hand: the game is then as it was.
