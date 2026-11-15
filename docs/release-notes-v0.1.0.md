**The Legend of Zelda: Tri Force Heroes on PC** – first public release.

> Unofficial fan project, not affiliated with Nintendo. **No game files are included** –
> you need your own copy of the game, decrypted on your own 3DS.

## Download & start

1. Download **`TriforceRecompiled-v0.1.0-windows-x64.zip`** below and unzip it anywhere
   (e.g. `Documents\Triforce Recompiled`).
2. Start **`TriforceRecompiled.exe`**. The **Setup** tab explains everything step by step:
   how to decrypt your game with GodMode9, choosing the file, one-click installation.
3. Press **PLAY**.

Windows may show a SmartScreen warning because the exe is not signed:
*More info → Run anyway*.

## Requirements

- Windows 10/11, 64-bit, graphics card with **Vulkan**
- Your own **Tri Force Heroes**, decrypted (`.cia` / `.3ds`). Tested: **European version
  (CTR-P-EA3P) without update**
- Online co-op (optional): **Steam** running and signed in – every player needs their own copy
- Discord status (optional): the **Discord** desktop app

## What's in it

- **Launcher** that stays open next to the game: setup assistant, play, online lobby,
  settings, info. **Esc** in the game brings it to the front.
- **Online co-op for 3 players over Steam** – lobbies for friends or public, 6-character codes,
  lobby browser, invites, ping. In the game choose *Multiplayer → Local Play*.
- **Discord Rich Presence** – play time, offline / online lobby with party size. Level names
  are still being collected (see *Help wanted*).
- **Graphics** – up to 10x internal resolution, fullscreen, VSync, aspect ratio, screen layouts,
  swap screens, texture filters, game speed, async shaders.
- **Controls** – keyboard and controllers at the same time, every key remappable, stick deadzone,
  mouse as touch screen. Your **Steam name or a custom name** in game.
- Portable: saves, settings and logs stay in the `user` folder next to the exe.

## Known issues

- Steam shows you as playing *Spacewar* – the project uses Valve's free test app (480) for
  lobbies and networking.
- Only the European version without update has been tested.
- Keep the launcher open while playing online – it carries the connection.

## Help wanted: level names for Discord

Each session writes `user\log\game_files.txt` (which game files were loaded, and when).
Play a few areas, note roughly when you entered each one, and open an issue with the log and
your notes – that is how level names get into Discord.

## Files

- `TriforceRecompiled-v0.1.0-windows-x64.zip` – the game frontend (this is what you want)
- `TriforceRecompiled-v0.1.0-symbols.zip` – debug symbols, only needed to analyse crash reports
- `SHA256SUMS.txt` – checksums

## Source & license

Built from this repository (tag `v0.1.0`) with Azahar commit
`11eceda5e0e682a1f44e396bf954a1b9137097ca` plus the patches in `patches/`; `setup_game.bat`
reproduces the build. Project code: GPL-2.0-or-later. The release binary contains Azahar and is
distributed under the GNU GPL v3 – license texts and third-party notices are in the zip.
