# Two players + one doppel (research notes)

Status: **research only - nothing implemented.** This file records what the game actually does
and what we have verified in this build, so the next attempt does not start from zero.

## Why this is a game-code problem, not a frontend one

The two modes are built differently inside the game:

| | Single player | Online (Local Play tunnelled over Steam) |
|---|---|---|
| Bodies | 3 bodies exist on one console | one body per console |
| Control | one index picks which body the pad drives, the rest are doppels | each console sends input for its own body |
| Doppels | yes - the Hall of the Doppels binds them to you | no |
| Entry | gateway opens for the doppel team | gateway needs 3 heroes |

So "2 humans + 1 doppel" needs either

* an online session that is allowed to start with **2** consoles **and** that spawns the third
  body as a doppel (on both clients, in sync), or
* a networked single-player session (which does not exist).

Both are game-code changes. The frontend can only supply inputs and memory access.

## Verified for our build (EUR v0)

The addresses published by the Tricord plugin are for game version **2.1.0**. This project runs
title version **0**, and the two builds really do differ: in a memory dump of our build the whole
camera block that Tricord uses (`0x32C81400`-`0x32C81680`) is zero. So the addresses below were
found in-game for *our* build, by dumping memory and comparing several states.

How it was found: dump the game's memory (`Settings -> Controls -> Research -> Dump memory`),
walk a few steps, dump again - a field that changed by the walking distance is a position. All
dumps and their checksums are reproducible with `code.bin` (the dump contains the first 64 KB of
`.text`, which must match byte for byte).

| What | Address | Notes |
|---|---|---|
| Player slot 0 block | `0x3281A808` | slots 1 and 2 at `+0x10000` / `+0x20000` |
| Position X | block `+0x1AC` | float; `+4` = Y, `+8` = Z |
| Body present | block `+0x1C8` | `1` = a body exists in this slot, `0x80000000` (-0.0) = empty |
| Player table | `0x32C5AE30` | pointers to the three blocks at `+0x14`, `+0x18`, `+0x1C` |
| Lobby player records | `0x32C5BBFC` | stride `0xF4` (three slots); name as UTF-16 at `+0x40`, as ASCII at `+0xC4` |

Measured states:

* **Single player, Hytopia:** all three slots hold a body. Link stands at `x = 0.01`, the two
  doppels at `x = -5.33` and `x = +5.33` - the game really keeps three full hero bodies with the
  same structure in one session.
* **Online lobby, alone:** only slot 0 holds a body; slots 1 and 2 exist but are marked empty.
* Walking changed `X`/`Z` of slot 0 by the movement distance and left `Y` alone, exactly as a
  world position should.

Still unknown: the flag/enum that decides single player versus network mode, the level/stage
ids, and the index of the controlled body. Finding them is possible with the same dump method established above, but
it is the smaller half of the problem - see below.

## Evidence that networked bodies can be doppels

There is a documented glitch ("Multiplayer Doppels", Zelda Wiki): while standing above the
Triforce Gateway with the Water Rod, one player disconnects - the remaining bodies keep existing
but behave like doppels that the host can switch between (the host cannot pause or switch back
until a game over). So a body created by the network can be re-flagged as a doppel at runtime.
That is the mechanism a patch would have to trigger deliberately, instead of by accident.

The game's own manual/wiki also states the boundaries: in the multiplayer lobby the gateway only
activates once **all three heroes** have gathered, while the Hall of the Doppels exists for
single player. The Coliseum is the one place that accepts two Links - that is a separate mode.

## Paths, with honest effort estimates

### A. Session patch - the real feature (large, uncertain)

1. Let a session start with 2 consoles: find and bypass the "three heroes" check (probably both a
   lobby/ready check and the gateway trigger).
2. Spawn the third body as a doppel on **both** clients, with identical state - the doppel is
   not a passive doll: it is used for totems, switches and throwing, so its position has to be
   synchronised like a real player's.
3. Both clients must run the identical patch, otherwise the session desyncs.

Needs ARM patching of `code.bin` (LayeredFS code patch or runtime writes from the frontend),
plus a lot of in-level testing. A GBAtemp thread asked for exactly this in 2016 and was told it
means reverse engineering the whole game; it never went anywhere. Nothing to guess at.

### B. Memory-flag prototype - cheap, but only answers a partial question (hours)

Now that the player blocks are known, one can still poke single flags in a running 3-console
session and watch what the game does. Useful outcome either way:

* it tolerates the single-player state -> the remaining work is the 2-player start check,
* it fights the network (bodies snapping, desync) -> a flag alone is not enough.

This needs two clients and a save in the Hytopia lobby, and it is *not* a feature yet.

### C. Third instance as a bot - rejected

Run a third game instance that joins and follows the group. Blocked by Steam (one account per
lobby) and it would need a position-following bot on top. Not worth it.

## Tooling in the app

`Settings -> Controls -> Research: hero bodies` reads the three slots from the running game,
copies them to the clipboard, writes a memory dump (`user/memdump/mem_NNN_*.bin`, ~100 MB),
and lets you read/write single bytes at an address typed in hex. That is the setup that produced
the addresses above and it is what the next round of reverse engineering needs.

## Credits

The Tricord project (IcefirePhoenix) published an address collection for version 2.1.0 - no code
copied, only facts, all of which were re-checked against this build. The "Multiplayer Doppels"
glitch is documented on Zelda Wiki.
