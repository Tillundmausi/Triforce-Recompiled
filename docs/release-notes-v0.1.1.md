**Controller fix** – pads that only show up as a generic joystick now work.

> Unofficial fan project, not affiliated with Nintendo. **No game files are included** –
> you need your own copy of the game, decrypted on your own 3DS.

## What's new in 0.1.1

### Controllers that were ignored before

Until now the game only used controllers that SDL recognises as a *game controller*. Everything
else was dropped without a message – no error, no input. That hit, among others:

- **vJoy** devices and other virtual pads (e.g. apps that forward a Switch 2 Pro Controller),
- raw gamepads whose GUID is missing from SDL's mapping database (the Switch 2 Pro Controller is
  not in the SDL 2.32 database yet),
- Windows' synthetic Xbox pads (`SWD\SYNTHETICGAMEPADENUM`).

These are now opened as **generic pads** with the usual Xbox layout: sticks, triggers as axes,
D-pad via hat *or* buttons 11–14, radial deadzone for the sticks. Steering wheels, pedals, HOTAS,
guitars, dance mats and MIDI devices are still skipped. Pads SDL *does* know behave exactly as
before, so nothing changes for working Xbox/PlayStation controllers.

### Finer control

- **`user/gamecontrollerdb.txt`** – optional file with extra SDL controller mappings (the usual
  [`gamecontrollerdb.txt`](https://github.com/mdqinc/SDL_GameControllerDB) format). If a pad's
  buttons are swapped, one mapping line for its GUID fixes it and the pad is treated as a fully
  recognised controller.
- **Settings → Controls → "Detected controllers"** – every pad the game sees, whether it is
  recognised or a generic pad, with its axes/buttons/hats and a **live view** of what is currently
  pressed. **"Copy controller info"** puts all of it on the clipboard for a bug report.
- Pads connected while the game is running are picked up automatically, and input still arrives
  while the game window is in the background.

## Download & start

1. Download **`TriforceRecompiled-v0.1.1-windows-x64.zip`** below and unzip it anywhere
   (e.g. `Documents\Triforce Recompiled`). Over an existing 0.1.0 folder is fine – `user\` stays.
2. Start **`TriforceRecompiled.exe`**. The **Setup** tab explains everything step by step.
3. Press **PLAY**.

Windows may show a SmartScreen warning because the exe is not signed:
*More info → Run anyway*.

## Requirements

- Windows 10/11, 64-bit, graphics card with **Vulkan**
- Your own **Tri Force Heroes**, decrypted (`.cia` / `.3ds`). Tested: **European version
  (CTR-P-EA3P) without update**
- Online co-op (optional): **Steam** running and signed in – every player needs their own copy
- Discord status (optional): the **Discord** desktop app

## Still in it from 0.1.0

- **Launcher** that stays open next to the game: setup assistant, play, online lobby, settings,
  info. **Esc** in the game brings it to the front.
- **Online co-op for 3 players over Steam** – lobbies for friends or public, 6-character codes,
  lobby browser, invites, ping. In the game choose *Multiplayer → Local Play*.
- **Discord Rich Presence** – play time, offline / online lobby with party size.
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
- Background-input hint: if a pad works everywhere except in game, check that Steam Input is not
  claiming the device, and look at *Settings → Controls → Detected controllers*.

## Help wanted: level names for Discord

Each session writes `user\log\game_files.txt` (which game files were loaded, and when).
Play a few areas, note roughly when you entered each one, and open an issue with the log and
your notes – that is how level names get into Discord.

## Files

- `TriforceRecompiled-v0.1.1-windows-x64.zip` – the game frontend (this is what you want)
- `TriforceRecompiled-v0.1.1-symbols.zip` – debug symbols, only needed to analyse crash reports
- `SHA256SUMS.txt` – checksums

## Source & license

Built from this repository (tag `v0.1.1`) with Azahar commit
`11eceda5e0e682a1f44e396bf954a1b9137097ca` plus the patches in `patches/`; `setup_game.bat`
reproduces the build. Project code: GPL-2.0-or-later. The release binary contains Azahar and is
distributed under the GNU GPL v3 – license texts and third-party notices are in the zip.
