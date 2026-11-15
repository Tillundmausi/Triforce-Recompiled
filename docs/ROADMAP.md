# Roadmap

## Done
- Game file validation (`ctrinspect`) and system-interface analysis of the real dump.
- Playable PC build on Azahar's core with its own frontend (no emulator UI).
- Launcher with setup assistant, settings (graphics/audio/controls), player name, crash reports.
- Online co-op for 3 players over Steam (lobbies, codes, invites, ping) – UDS tunnelled over Steam.

## Next
- Prebuilt Windows releases (no Visual Studio needed for players).
- Online polish: reconnect handling, clearer status inside the game window, lobby chat.
- In-game overlay (FPS, ping) without leaving fullscreen.
- Widescreen: patch the game's projection (needs the matching function in the game code).

## Research
- **2 players + 1 doppel**: let two human players play the 3-player mode with a doppel that
  either player can switch into. Needs reverse engineering of the game's multiplayer and doppel
  logic – large, uncertain effort.
- **Static recompilation**: see [RECOMPILATION.md](RECOMPILATION.md).

## Decisions
- License GPL-2.0-or-later (allows using Azahar code).
- Target version: European release v0 (`CTR-P-EA3P`).
- Networking: Steam (lobbies, relay); development uses Valve's test app ID 480.
