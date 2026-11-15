# Static recompilation – plan

The current build runs the game on Azahar's emulation core (dynamic recompiler for the ARM11 CPU).
The long-term goal is a **native static recompilation**: translate the game's ARM/Thumb code into
C++ ahead of time (like N64Recomp / XenonRecomp) and keep Azahar's system services, renderer and
audio as the runtime. This document collects the analysis so far.

## Input
- 3DS executable: ARM11 MPCore (ARMv6K), ARM + Thumb-1, VFPv2, ARMv6 SIMD, LDREX/STREX.
- `ctrinspect` (this repo) validates the dump and extracts `.text/.rodata/.data` with verified hashes.
- Tested target: EUR v0 (`CTR-P-EA3P`). All addresses and patches refer to this executable.
  See [SYSTEM_SURFACE.md](SYSTEM_SURFACE.md).

## Recompiler design
| Topic | Approach |
|---|---|
| Code generation | One C++ function per guest function, `void f_00123456(Context&)`; registers, flags (NZCV/Q/GE) and VFP state in `Context`. |
| Memory | Reserve 4 GiB of host address space, `host = base + guest`; special regions via guard pages. |
| Function discovery | Entry point + BL/BLX targets + function pointer tables + prologue heuristics, cross-checked with a Ghidra export. |
| Indirect branches | Lookup table guest address → host function; unknown targets are logged, never ignored. |
| ARM/Thumb interworking | Bit 0 of BX/BLX/POP pc targets; both modes analysed. |
| Literal pools | CFG-based (recursive) disassembly instead of linear sweep. |
| Coverage gaps | Fallback interpreter only for addresses without recompiled code, with logging. |
| CRO modules | The game requests `ldr:ro`; if it loads CROs they must be recompiled and relocated too. |

## Runtime
The emulator-core build already provides exactly the runtime a recompiled binary needs:
kernel objects and threads, all system services, the PICA200 → Vulkan renderer, DSP audio and the
local-wireless layer with the Steam transport. The CPU backend can therefore be replaced step by step.

## Milestones
1. ARM + Thumb decoder with table-driven tests and capstone cross-check.
2. Recursive disassembly and function database (≥ 95 % of `.text` assigned).
3. First recompiled leaf function, differential-tested against a reference interpreter.
4. All integer instructions, then Thumb, then VFP (bit-exact).
5. Whole `.text` recompiled and linked, interpreter fallback for the rest.
6. Swap the CPU backend in the existing frontend.
