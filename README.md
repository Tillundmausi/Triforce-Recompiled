# Triforce Recompiled

**The Legend of Zelda: Tri Force Heroes on PC** – with modern controls, graphics options and
**online co-op over Steam**.

> Unofficial fan project. Not affiliated with or endorsed by Nintendo.
> This repository contains **no Nintendo code, assets or keys** – you need your own,
> legally obtained copy of the game and a 3DS to dump it from.

**[⬇ Download the latest release](https://github.com/Tillundmausi/Triforce-Recompiled/releases/latest)**
(Windows 10/11, 64-bit) – unzip, start `TriforceRecompiled.exe`, follow the **Setup** tab.

## Features

- **Launcher with setup assistant** – explains every step, checks your game file
  (decrypted? right game? right version?) and installs it with one click.
- **Online co-op for 3 players over Steam** – create a lobby, invite friends or share a
  6-character code, see everyone's ping. The game's own *Local Play* mode is tunnelled
  through Steam, so the original multiplayer logic is used unchanged.
- **Your name in game** – your Steam name or a custom one (instead of a generic console name).
- **Graphics** – internal resolution 1x–10x (up to 4K), fullscreen, VSync, aspect ratio,
  screen layouts (stacked, side by side, large top screen, single screen, hybrid),
  swap screens, texture upscaling and filtering, game speed, asynchronous shaders.
- **Controls** – keyboard and controllers (Xbox, PlayStation, …) at the same time, every key
  remappable, adjustable stick deadzone, mouse as touch screen.
- **Settings while playing** – press **Esc** in the game to bring up the launcher; changes
  apply immediately and are saved.
- **Discord Rich Presence** – friends see that you play Triforce Recompiled, for how long,
  whether you are in an online lobby (and how full it is) and – once the level list is
  filled in – which area you are in. Can be turned off in *Settings → Discord*.
- **Crash reports** – `user/crash.txt` + minidump for debugging.

## Status

| Area | Status |
|---|---|
| Single player | ✅ playable (tested: European version, no update) |
| Online co-op (Steam) | ✅ working in first tests |
| Launcher, setup assistant, settings | ✅ |
| Native static recompilation (no emulator core) | 🔬 long-term goal, see [docs/RECOMPILATION.md](docs/RECOMPILATION.md) |
| Discord Rich Presence | ✅ status, play time, lobby / party size · level names: being collected (see below) |
| 2 players + 1 doppel | 💡 idea, needs game-code research |

Under the hood the game currently runs on the core of the open-source 3DS emulator
[Azahar](https://github.com/azahar-emu/azahar) (successor of Citra), embedded into a
dedicated game frontend. See [How it works](#how-it-works).

## What you need

- Your own copy of **Tri Force Heroes** and a **3DS/2DS with custom firmware**
  (Luma3DS + GodMode9) to make a decrypted copy – see [3ds.hacks.guide](https://3ds.hacks.guide).
- Tested version: **European release (CTR-P-EA3P) without update**. Other regions/versions may work.
- **Windows 10/11 (64-bit)** with a **Vulkan**-capable graphics card.
- For online co-op: **Steam** running and signed in. Every player needs their own copy of the game.

## Getting started

**Easiest:** download the zip from [Releases](https://github.com/Tillundmausi/Triforce-Recompiled/releases/latest),
unzip it anywhere, start `TriforceRecompiled.exe` and follow the **Setup** tab. Nothing else to install.

**Building from source** – the scripts do everything automatically:

1. **Visual Studio C++ tools** – double-click `install_cpp_tools.bat` (adds
   "Desktop development with C++" to Visual Studio 2022; install
   [Visual Studio 2022 Community](https://visualstudio.microsoft.com/) first if you don't have it).
2. **Online play (optional)** – download the Steamworks SDK from
   [partner.steamgames.com](https://partner.steamgames.com/downloads/list) (free Steam login) and put
   the `steamworks_sdk_*.zip` into this folder. Without it the game is built for single player only.
3. **Build** – double-click `setup_game.bat`. It downloads Azahar (pinned version), Dear ImGui,
   Git/Python if missing, applies the patches and builds `dist\TriforceRecompiled.exe`.
   The first run takes 15–40 minutes.
4. **Play** – start `dist\TriforceRecompiled.exe` and follow the **Setup** tab.

### Making a decrypted copy of your game

3DS game files are encrypted. Decrypt them on your own console with GodMode9:

1. Copy the game's `.cia` to the SD card (or use the cartridge / installed title).
2. Hold **START** while turning the 3DS on – GodMode9 starts.
3. `[0:] SDCARD` → press **A** on the `.cia` → *CIA image options…* → *Decrypt file*.
   (Cartridge: `[C:] GAMECART` → the `.3ds` file → *Decrypt file*.)
4. Copy the result from `gm9/out/` to your PC and select it in the launcher.

## Controls (default)

| 3DS | Keyboard | Controller |
|---|---|---|
| Circle Pad | **W A S D** (Shift = walk slowly) | left stick |
| A / B / X / Y | **K / J / I / U** | by position: right / bottom / top / left |
| L / R | **Q / E** | LB / RB |
| D-Pad | arrow keys | D-Pad |
| Start / Select | Enter / Backspace | Start / Back |
| Touch screen | mouse (bottom screen) | – |

**Esc** = launcher/settings · **F11** = fullscreen · **F10** = screen layout · **F9** = swap screens.
All keys can be changed in *Settings → Controls*. That page also lists the controllers the game
sees (with live buttons/axes); pads it shows as *generic pad* are pads SDL does not know - they
still work with the usual Xbox layout, and a mapping line for their GUID in `user/gamecontrollerdb.txt`
fixes wrong buttons.

## Online co-op

1. Everyone starts Steam and Triforce Recompiled.
2. One player creates a lobby in the **Online** tab and invites the others (Steam overlay) or
   shares the **lobby code**.
3. When all three are in the lobby, press **PLAY**, then in the game choose
   **Multiplayer → Local Play**. One player creates the group, the others join it.

Keep the launcher open while playing – it hosts the connection. Steam shows you as playing
*Spacewar*: the project uses Valve's free test app ID (480) for lobbies and networking.

## Discord Rich Presence

With the Discord desktop app running, your profile shows *Playing Triforce Recompiled* with
the play time and whether you play offline or in an online lobby (party size, e.g. 2 of 3).
The lobby code is never sent to Discord. Turn it off or hide parts of it in
**Settings → Discord**.

**Level names – help wanted.** The current level is detected from the game files that are
loaded (no guessed memory addresses). Each session writes `user/log/game_files.txt`: every game
file the first time it is loaded, with the seconds since the game started. To add level names:
play a few areas, note roughly when you entered each one, and open an issue with the log and
your notes. The names go into [`game/levels.h`](game/levels.h). Until an area is listed there,
Discord shows *Playing*.

## How it works

```
TriforceRecompiled.exe
├── Launcher (Dear ImGui)      setup assistant, lobby, settings – own window, stays open
├── Game window (SDL2)         keyboard/controller/touch input, Vulkan output
├── Azahar core (pinned)       ARM11 CPU, 3DS system services, PICA200 → Vulkan, audio
│     ├── patch: RoomMember external transport
│     └── patch: RomFS read hook (which game files are loaded → level for Discord)
├── Steam (Steamworks)         lobbies, invites, P2P relay
│     └── carries the game's local-wireless (UDS) packets between players
└── Discord IPC                Rich Presence (talks to the local Discord app, no SDK)
```

- `game/` – the frontend: launcher UI, game window, input, config, Steam networking, crash handler.
- `patches/` – small patches applied to Azahar : letting the local-wireless layer use an
  external transport instead of Azahar's own room server, and a hook that reports RomFS reads.
- `src/`, `tools/ctrinspect`, `tests/` – the project's own 3DS container parser (CIA/NCSD/NCCH,
  ExHeader, ExeFS, BLZ, SHA-256) used to validate game files, plus unit tests. It is also the
  first step towards the static recompiler.
- `docs/` – technical notes: [system interface of the game](docs/SYSTEM_SURFACE.md),
  [recompilation plan](docs/RECOMPILATION.md), [roadmap](docs/ROADMAP.md), [changelog](docs/CHANGELOG.md).

### Building the tools only

```
build.bat                          (Windows, Visual Studio 2022)
cmake -S . -B build && cmake --build build && ./build/tfh_tests   (Linux/macOS)
```
`inspect.bat` / `ctrinspect <dump>` prints title ID, region, version, encryption state, code
segments, the system services and kernel calls the game uses, and extracts the executable from a
decrypted dump.

## Credits

- [Azahar](https://github.com/azahar-emu/azahar) and the Citra team – the 3DS emulation core (GPLv2+)
- [Dear ImGui](https://github.com/ocornut/imgui) – launcher UI (MIT)
- [SDL2](https://www.libsdl.org/) – windowing and input (zlib)
- Steamworks SDK by Valve – not included, downloaded by you (Steamworks SDK license)
- [3dbrew](https://www.3dbrew.org/) – documentation of the 3DS file formats

## License

GPL-2.0-or-later – see [COPYING](COPYING).
*The Legend of Zelda* and *Tri Force Heroes* are trademarks of Nintendo.
