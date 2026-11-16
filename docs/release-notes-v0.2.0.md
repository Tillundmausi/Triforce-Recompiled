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

### Research panel: doppels in online sessions

*Settings -> Controls -> "Research: doppel in online sessions"* shows what the game itself
is doing while it runs: `DoppelsEnabled`, `ActiveLink`, level/stage, network state and the
positions of all three heroes. **"Copy session state"** puts all of it on the clipboard.

There is also an experimental **"Force DoppelsEnabled = 1"** switch. It is not saved on
purpose - it is off again after every start.

## The experiment this build is for

Goal: **two human players + one doppel** instead of three humans. The game's doppel mode is
a flag plus an index, and networked bodies can be doppels (a known glitch proves it), but a
session currently has to start with three consoles - so the missing piece is game-side.

This build measures the first half of that question.

1. Everyone installs **this same build** (0.2.0) - mixed versions are not useful here.
2. Play as usual: online session, three players, into a level.
3. On **one** client: Esc -> Settings -> Controls -> *Research* -> press
   **"Copy session state"** while you are still in the lobby or level (that is the "before").
4. Then tick **"Force DoppelsEnabled = 1"** on that one client.
5. Watch and note:
   - do the other two Links keep moving normally, or do they freeze / snap back?
   - does the camera stay on your Link?
   - can you switch between the three characters (Tab / tapping the panels)?
   - does the session survive, or does it kick someone out?
6. Press **"Copy session state"** again (the "after") and send both texts, plus
   `user\log\azahar_log.txt` if anything crashed or disconnected.

Then untick the switch - normal behaviour should return immediately. If the game ends up in
a strange state, a restart fixes it; nothing is written to the save file by this switch.

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
- `Force DoppelsEnabled` is a research switch, not a finished feature: expect desyncs.

## Files

- `TriforceRecompiled-v0.2.0-windows-x64.zip` - the game frontend (this is what you want)
- `TriforceRecompiled-v0.2.0-symbols.zip` - debug symbols, only needed to analyse crash reports
- `SHA256SUMS.txt` - checksums

## Source & license

Built from this repository (tag `v0.2.0`) with Azahar commit
`11eceda5e0e682a1f44e396bf954a1b9137097ca` plus the patches in `patches/`; `setup_game.bat`
reproduces the build. Project code: GPL-2.0-or-later. The release binary contains Azahar and is
distributed under the GNU GPL v3 - license texts and third-party notices are in the zip.
