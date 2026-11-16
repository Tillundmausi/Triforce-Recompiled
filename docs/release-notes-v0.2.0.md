**0.2.0 - character switching with a button, plus the tooling for the two-players-plus-doppel experiment.**

> Unofficial fan project, not affiliated with Nintendo. **No game files are included** -
> every player needs their own copy of the game, decrypted on their own 3DS.

## What's new in 0.2.0

### Switch characters with a button (single player)

The game hands control to the Doppel whose panel you tap on the touch screen. Pressing
**Tab** - or clicking the **right stick** on a controller - does that tap for you and cycles
through the three panels, so the mouse is not needed for switching any more.

Key, controller button and the three panel positions are under
*Settings -> Controls -> "Doppel switch (single player)"*. If a tap lands somewhere else,
tap the panel once with the mouse in the game, press Esc, then **"Use last tap"** - that
also covers other screen layouts and swapped screens.

In multiplayer the same panels show the other players, so turn the shortcut off there.

### Research panel: hero bodies

*Settings -> Controls -> "Research: hero bodies"* shows the three bodies the game keeps for the
players - position and whether the slot holds a body at all. In single player all three slots are
used (Link plus the two Doppels), in a network lobby only the first one is. **"Copy session
state"** puts it on the clipboard, **"Dump memory"** writes the game's memory to
`user\memdump\` (only useful for debugging, ~100 MB per dump), and the address field lets you
read or write single bytes.

These are developer tools for the "two players + one doppel" question - see
[docs/2PLAYER_DOPPEL.md](https://github.com/Tillundmausi/Triforce-Recompiled/blob/main/docs/2PLAYER_DOPPEL.md).

## The experiment this build is for

Goal: **two human players + one doppel** instead of three humans. The game keeps three hero
bodies in single player (Link plus two doppels) but only one per console online, and a session
currently has to start with three consoles - so the missing piece is game-side, not something a
launcher setting can flip. The research panel in this build is the measuring tool for that work.

If you want to help: play as usual (three players online), open *Settings -> Controls ->
Research* and press **"Dump memory"** once in the lobby and once inside a level, then send the
`user\memdump\mem_NNN.txt` files. Every dump pins down a bit more of the game's state.

**Only the European version without update (CTR-P-EA3P) is mapped.** On any other version the
numbers in the panel are meaningless (harmless, but useless). Tell us which version you use.

## Download & start

1. Download **`TriforceRecompiled-v0.2.0-windows-x64.zip`** below and unzip it anywhere.
   Over an existing 0.1.x folder is fine - `user\` (saves, settings) stays.
2. Start **`TriforceRecompiled.exe`** and follow the **Setup** tab.
3. Press **PLAY**.

Windows may show a SmartScreen warning because the exe is not signed:
*More info -> Run anyway*.

## Requirements

- Windows 10/11, 64-bit, graphics card with **Vulkan**
- Your own **Tri Force Heroes**, decrypted (`.cia` / `.3ds`), **European version without update**
- Online co-op: **Steam** running and signed in - every player needs their own copy

## Known issues

- Steam shows you as playing *Spacewar* (Valve's free test app 480 is used for lobbies).
- Only the European version without update has been tested.
- Keep the launcher open while playing online - it carries the connection.
- The research panel reads the game's own memory; it is a measuring tool, not a feature. Do not
  expect doppels in an online session from this build.

## Files

- `TriforceRecompiled-v0.2.0-windows-x64.zip` - the game frontend (this is what you want)
- `TriforceRecompiled-v0.2.0-symbols.zip` - debug symbols, only needed to analyse crash reports
- `SHA256SUMS.txt` - checksums

## Source & license

Built from this repository (tag `v0.2.0`) with Azahar commit
`11eceda5e0e682a1f44e396bf954a1b9137097ca` plus the patches in `patches/`; `setup_game.bat`
reproduces the build. Project code: GPL-2.0-or-later. The release binary contains Azahar and is
distributed under the GNU GPL v3 - license texts and third-party notices are in the zip.
