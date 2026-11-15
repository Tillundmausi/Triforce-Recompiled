# System interface of Tri Force Heroes (EUR, v0)

Source: `ctrinspect` run on a real, decrypted dump. All hashes verified.

| Field | Value |
|---|---|
| Title ID / product code | `0004000000177000` / `CTR-P-EA3P` (Europe), title version 0 |
| Internal name (ExHeader) | `Alice` |
| `.code` (decompressed) | 6,451,200 bytes, SHA-256 `ebfe789862bd748dac5e971c5ac4efbc59c90f8e47efea4372c6717a49ec8694` |
| `.text` | `0x00100000`, size `0x0057B240` (~5.5 MB ARM/Thumb code) |
| `.rodata` | `0x0067C000`, size `0x000777D0` |
| `.data` | `0x006F4000`, size `0x000325DC`; `.bss` `0x000B6CB0` |
| Stack | `0x00040000` |
| Kernel | 2.50, 51 allowed SVCs (standard user-mode calls only) |
| System mode | Old 3DS: 0, New 3DS: 1, 804 MHz enabled |
| Contents | game 524 MB, manual 1.6 MB, Download Play child 24 MB |

## Services (33)
`APT:A`, `ac:u`, `act:u`, `am:app`, `boss:U`, `cam:u`, `cecd:u`, `cfg:u`, `dlp:FKCL`, `dlp:SRVR`,
`dsp::DSP`, `frd:u`, `fs:USER`, `gsp::Gpu`, `hid:USER`, `http:C`, `ir:USER`, `ldr:ro`, `mic:u`,
`ndm:u`, `news:u`, `nfc:u`, `nim:aoc`, `nwm::UDS`, `ptm:u`, `qtm:u`, `soc:U`, `ssl:C`, `y2r:u`,
plus the developer ports `$hioFIO`, `$hostio0`, `$hostio1`, `pxi:dev` (unused on retail units).

## Conclusions
- **Multiplayer:** `nwm::UDS` (local wireless) and `dlp:SRVR`/`dlp:FKCL` (Download Play) are used,
  so tunnelling UDS packets over Steam reproduces the original local multiplayer.
- **Original online mode:** `soc:U`, `http:C`, `ssl:C`, `frd:u`, `act:u` (Nintendo Network) – not used by this project.
- **`ldr:ro`:** the game may load CRO modules at runtime. Irrelevant for the emulator-core build;
  a static recompiler must check the RomFS for `.cro` files.
- `nfc:u` (amiibo), `cam:u`, `mic:u`, `ir:USER`, `qtm:u` are requested but not needed for normal play.
