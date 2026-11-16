# Changelog

## 0.1.1 (2026-11-16)
### Game
- Controllers SDL does not recognise as a game controller (vJoy, emulator apps that fake a
  pad, Windows' synthetic Xbox pads, ...) are now read as generic pads with the usual Xbox
  layout instead of being ignored.
- Optional `user/gamecontrollerdb.txt` for extra SDL controller mappings.
- Settings → Controls: list of detected controllers with live button/axis state and a
  "Copy controller info" button for bug reports.
- Newly connected pads are picked up while the game runs; joystick events also arrive while
  the window is in the background.

## 0.1.0 (2026-11-15)
### Launcher
- Launcher stays open for the whole session; **Esc** in the game brings it to the front.
  Closing it while the game runs only minimises it (it hosts the online connection).
- Setup assistant: requirements, how to decrypt your game with GodMode9, file picker with
  validation (encrypted / damaged / wrong game / tested version), one-click installation with progress.
- Tabs: Setup, Play, Online, Settings (Graphics / Audio / Controls), Info. Everything in English.
- Start and stop the game from the launcher; the game can be started again without restarting.

### Game
- Own game window on Azahar's core (Vulkan), portable `user/` folder next to the exe.
- Keyboard and all controllers at the same time; every key remappable; stick deadzone.
- Player name from Steam or custom (3DS console user name).
- Settings: internal resolution 1x–10x, fullscreen, VSync, aspect ratio, layouts, swap screens,
  texture upscaling/sampling, game speed, async shaders, volume. Saved in `user/config.ini`.
- Crash handler: `user/crash.txt` + `crash.dmp`; builds include PDB symbols.

### Discord Rich Presence
- Shows *Playing Triforce Recompiled* with play time, launcher/in-game state, online lobby and
  party size (lobby code is never sent). Own IPC client (named pipe / Unix socket), no Discord SDK;
  reconnects when Discord starts later; updates rate-limited.
- Level detection from loaded game files: Azahar patch `azahar-romfs-read-hook.patch` reports RomFS
  reads, the frontend maps offsets to file paths with its own RomFS parser (`src/formats/romfs.*`).
  Level table (`game/levels.h`) is empty until real play logs exist (`user/log/game_files.txt`,
  `user/log/romfs_index.txt`).
- Settings → Discord: on/off, show level, show lobby, live detection view.

### Online
- Steam lobbies (friends / public), 6-character codes, lobby browser, invites, member list with ping.
- Azahar patch: `RoomMember` can use an external transport. Packets are routed like Azahar's room
  server (broadcast to all others, unicast to the matching MAC; MACs derived from Steam IDs) and sent
  with Steam Networking Messages (relay / NAT traversal by Steam).

### Tools
- `ctrinspect`: CIA/NCSD/NCCH/ExHeader/ExeFS parser, BLZ decompression, SHA-256, JSON report; 62 unit checks (incl. RomFS parser).
