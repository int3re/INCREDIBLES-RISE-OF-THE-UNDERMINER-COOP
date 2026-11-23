# Controls

[Documentation](README.md) · [Install](INSTALL.md) · [Launcher](LAUNCHER.md) · [Network play](NETPLAY.md) ·
**Controls** · [Settings](SETTINGS.md) · [Troubleshooting](TROUBLESHOOTING.md)

**English** · [Русский](../CONTROLS.md)

The keys are the game's own. The mod does not replace them: it adds the mouse's side buttons, makes the
mouse's buttons work in a network game, and lets the [launcher](LAUNCHER.md) set everything before the
game starts.

## The keys as the game has them

| Action | As the launcher calls it | Player 1 | Player 2 |
|---|---|---|---|
| Move forward / back / left / right | Вперёд / Назад / Влево / Вправо | W / S / A / D | arrows |
| Jump | Прыжок | Space | Num 0 |
| Pause | Пауза | Esc (fixed by the game) | Right Ctrl |
| Screenshot | Снимок экрана | F4 | — |
| Punch | Толчок | O | Num 2 |
| Grab (Mr. Incredible) / freeze (Frozone) | Взять / мороз | I | Num 1 |
| Action | Действие | P | Num 3 |
| Super move, left / right | Суперудар левый / правый | K / L | Num 7 / Num 9 |
| Upgrade | Улучшить | Left Shift | Backspace |
| Dodge forward / back / left / right | Уклон вперёд / назад / влево / вправо | 8 / 9 / 7 / 0 | Num 8 / Num 5 / Num 4 / Num 6 |
| Partner (AI): swap / guard / attack / follow | Смена ИИ / Охрана ИИ / Агрессия ИИ / ИИ следовать | 1 / 2 / 3 / 4 | Home / End / Delete / Page Down |

In the game's menus Enter chooses and Esc goes back, as the hints at the bottom of each menu say.

**In a network game each player uses their own player-1 keys** on their own computer — whichever of the
two heroes they play. Player 2's keys are for two players at one computer.

## Changing the keys

<p align="center">
  <img src="../images/launcher-controls.png" alt="The launcher: keys and mouse buttons" width="820">
</p>

In the launcher: the settings → **УПРАВЛЕНИЕ** → **ИГРОК 1** or **ИГРОК 2**.

1. Press the cell of the action. It lights up and says «клавиша / мышь» (a key or the mouse).
2. Press the new key. Esc cancels.

- A key that another action has — of either player: the keyboard is one — changes places with the key
  this action had.
- Esc cannot be given to an action: it is the game's pause and "back".
- Player 1's pause is fixed to Esc by the game itself; its cell is locked.
- **По умолчанию** (Defaults) returns the shown player's keys to the game's own.

The same keys can be changed on the game's own ОПЦИИ screen (ИГРОК 1 / ИГРОК 2): the launcher writes them
where the game keeps them. Changes take effect at the next start of the game.

## The mouse's buttons

An action can have a button of the mouse **besides** its key: the left one, the right one, the middle one
(the wheel pressed) and the two side ones.

<p align="center">
  <img src="../images/launcher-controls-wait.png" alt="A cell waits for a key or a button of the mouse" width="820">
</p>

1. Press the cell of the action.
2. Press the button you want **on that same cell**.

The cell then shows both, for example `O / ЛКМ`:

| On the cell | The button |
|---|---|
| ЛКМ | left |
| ПКМ | right |
| СКМ | middle (the wheel) |
| Бок.1 | side button 1 (usually "back") |
| Бок.2 | side button 2 (usually "forward") |

- Pressing the same button on the waiting cell again takes it off the action.
- A button belongs to one action: given to another, it leaves the first. The mouse is one for both players.
- A press of the left button anywhere else is an ordinary click; other buttons pressed elsewhere do nothing.
- **По умолчанию** takes the shown player's mouse buttons off.

How they work in the game:

- **In a level** a button does what its action does, together with the key. This holds in a network game
  too: the button goes into your controls before they are sent.
- **In the main menu** the mouse is what it always was: it moves the game's pointer and chooses items.
  Buttons given to actions do nothing there.
- **The side buttons need the mod.** The game itself knows the left, the right and the middle button only;
  the launcher can write a side button for it, but without the mod it will do nothing.
- Which side button is the first depends on the mouse. If they turn out the other way round, swap them in
  the launcher.

A way to play with the mouse: Толчок on the left button, Взять / мороз on the right one, the two super
moves on the side buttons.

## Gamepads

The game binds gamepads itself, on its ОПЦИИ screen. The mod does not change that and adds nothing to it.
Gamepads have not been tried with the mod; in the launcher an action bound to a pad shows «Геймпад».

## What has not been checked

The mouse's buttons were checked in the lab with presses fed to the mod, in a single game and in a
network one. With a real mouse in the game's real window nobody has pressed them yet.
