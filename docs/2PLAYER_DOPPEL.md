# Two players + one doppel (research notes)

Status: **research only - nothing implemented yet.** This file records what the game
actually does, so the next attempt does not start from zero.

## Why this is a game-code problem, not a frontend one

The two modes are built differently inside the game:

| | Single player | Online (Local Play tunnelled over Steam) |
|---|---|---|
| Bodies | 3 bodies exist on one console | one body per console |
| Control | `ActiveLink` picks which body the pad drives, the rest are doppels | each console sends input for its own body |
| Doppels | yes (`DoppelsEnabled = 1`) | no (`DoppelsEnabled = 0`) |
| Entry | gateway opens for the doppel team | gateway needs 3 heroes |

So "2 humans + 1 doppel" needs either

* an online session that is allowed to start with **2** consoles **and** spawns the third
  body as a doppel, or
* a networked single-player session (which does not exist).

Both are game-code changes. The frontend can only supply inputs and memory access.

## What the game exposes (verified addresses, EUR v0)

Taken from Tricord's public address collection (a 3DS plugin for this game); the
addresses are the same in Azahar because the address space is the emulated one.
Per-player state is laid out in blocks with a stride of `0x10000` (players 0/1/2).

| Name | EUR address | Notes |
|---|---|---|
| `DoppelsEnabled` | `0x32D81EC6` | u8. 1 = single-player/doppel mode, 0 = network mode |
| `ActiveLink` | `0x31A038D1` | u8. 0/1/2 = controlled hero, **3 = this pad controls all heroes** |
| `PositionX` | `0x32841358` | float, `+4` Y, `+8` Z, per-player `+0x10000` |
| `HealthCurrent` | `0x32C82574` | float, per-player `+0x10000` |
| `IsSpawned` | `0x32841490` | per-player |
| `CurrLevelID` / `CurrStageID` | `0x3280EE44` / `0x3280EE48` | |
| `PauseStatus` | `0x3280EE0C` | |
| `HostDataStart` | `0x31A03664` | network state; `0xFFFFFFFD` = not connected |

Two things follow from this:

1. The doppel mechanic is **not** a separate mode, it is a flag (`DoppelsEnabled`) plus an
   index (`ActiveLink`). Height and camera follow the same index (`CameraLinkFocus`).
2. `ActiveLink = 3` lets one pad drive all three heroes at once - that is what the Tricord
   plugin calls *Control all players*. The engine already knows how to move a body that
   "belongs" to nobody.

## Evidence that networked bodies can be doppels

There is a documented glitch ("Multiplayer Doppels", Zelda Wiki): while standing above the
Triforce Gateway with the Water Rod, one player disconnects - the remaining bodies keep
existing but behave like doppels that the host can switch between. So a body created by the
network can be re-flagged as a doppel at runtime. That is the mechanism a patch would have
to trigger deliberately, instead of by accident.

## Paths, with honest effort estimates

### A. Session patch - the real feature (large, uncertain)

1. Let a session start with 2 consoles: find and bypass the "three heroes" check (probably
   both a lobby/ready check and the gateway trigger).
2. Spawn the third body as a doppel for both clients (same state on both sides).
3. Both clients must run the identical patch, otherwise the session desyncs.

Needs ARM patching of `code.bin` (LayeredFS code patch, or runtime writes from the
frontend), plus a lot of in-level testing. Not something to guess at.

### B. Memory-flag prototype - the cheap first experiment (hours, low risk)

In a running 3-console session, set `DoppelsEnabled = 1` on one client and see what the game
does. Two possible outcomes:

* it tolerates it -> the remaining work shrinks to the 2-player start check (path A step 1),
* it fights the network (bodies snapping back, desync) -> we know a flag alone is not enough.

This can be exposed as an experimental toggle in the launcher (read/write the two addresses
above, off by default). It needs two clients and a save in the Hytopia lobby to test.

### C. Third instance as a bot - rejected

Run a third game instance that joins and follows the group. Blocked by Steam (one account
per lobby) and it would need a position-following bot on top. Not worth it.

## Experiment plan (needs ~1 hour, two clients, a save in Hytopia)

1. Both clients: 3-player session as usual, but with the experiment toggle available.
2. Stand in the lobby, read `DoppelsEnabled` / `ActiveLink` / positions on both clients -
   confirm the addresses match what the game is doing (values change when walking).
3. Enter a level with 3 players, set `DoppelsEnabled = 1` on one client only. Note:
   camera, the other two bodies, input response, and whether the session stays up.
4. Repeat with the flag set on both clients.
5. Only then decide whether path A is worth it.

## Credits

Memory layout discovered by the Tricord project (IcefirePhoenix) - no code copied, only
addresses/facts, which we re-check against the game before using.
