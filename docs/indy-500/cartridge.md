# Indy 500 cartridge research

The supported US cartridge has 1,048,576 bytes and the SHA-256 `58cc3a3a4c33fec49bb9260ae83650a6d22b1004b4767a61307d197f6e423d5a`. The converter, the Ghidra importer and the capture tool check this hash before using the image. The port also needs the US external BIOS (SHA-256 `4cade226b44f09ebf5897b137d5564e5579b4d11b23dcabacd308fb0d18e7077`), because the cartridge calls 42 kernel services.

## Tools

| Task | Command |
|---|---|
| Ghidra project and pseudo-C listing | `py analysis/indy-500/import-cartridge.py --fresh --coverage build/indy-500/<capture>/coverage.bin` |
| BIOS kernel in Ghidra | `py analysis/gamecom-bios/import-bios.py --fresh` |
| Original capture in Tigerbyte | `py scripts/indy-500/reference/capture.py TAPE OUT [--coverage] [--watch 00cd-00ce] [--trace 0080-03ff] [--dma]` |
| Port in mGBA | `py scripts/indy-500/mgba.py dist/indy-500.gba TAPE OUT` |

The Ghidra module in `analysis/SM8500` now covers the full SM8521 instruction set. `analysis/SM8500/generate.py` produces the SLEIGH file. Compile it with Ghidra's `sleigh` tool and copy `sm8500.sla`, the `.slaspec` and the support files into `Ghidra/Processors/SM8500/data/languages`. The language version is now 2.0, so older private projects such as `build/resident-evil-2/ghidra` must be imported again.

The cartridge importer seeds disassembly only at addresses that Tigerbyte actually executed. The capture tool writes this coverage map with `--coverage`. Each executed address is recorded together with the bank mapped at that moment. A call into `6000–7FFF` therefore goes to bank 81 or to the bank 83 overlay, depending on which one ran there. The output is written to `build/indy-500/ghidra/decompiled/` (`base.c`, `bank83.c`, `listing.txt`).

## Memory layout

| CPU window | Contents |
|---|---|
| `4000–5FFF` | Bank 80, file `0000`. The cartridge header points to `80:4020`. |
| `6000–7FFF` | Bank 81 (file `2000`) or bank 83 (file `6000`), switched through MMU3 (`27`). |
| `8000–9FFF` | Bank 82 (file `4000`): data tables only; no instruction there executed in the captured routes. |
| `2000–3FFF` | External BIOS page 2, the kernel. |

Graphics live in 16 KiB DMA pages. A page row is one screen column (256 pixels at 2 bits). Most pictures are stored as an even page with the image and the following odd page with a white mask. The blitter call sets `r8/r9` (destination), `r10/r11` (source), `r12/r13` (size), `r14` (page) and `r15` (mode), then calls `5A45` and `5EB2`. `5EB2` requests kernel service `1D`.

The entry at `80:4020` dispatches the four BIOS requests through the table at `402E`. Request 1 enters the main loop at `40DB`. The game state in `00D5` selects a handler from `412D`: `4168` races, `45B0`, `41F6`, `4430`, `44BB` and `480C` handle other states, and the remaining states run code in bank 83 from `6000`.

## Race variables

These addresses were found by tracing RAM during a third-person race with car A and the automatic transmission.

| RAM | Meaning | Evidence |
|---|---|---|
| `00CD–00CE` | Speed in mph | Displayed values 060, 148, 202 and 226 while A was held. |
| `00CB` | Automatic gear | Rose 1, 3, 5, 6 while accelerating. |
| `00C8–00C9` | Position on the lap | `00C8` runs from 0 to 255 per lap. The wrap at `81:754F` increments `00CA`. |
| `00CA` | Completed laps | Incremented with the wrap. |
| `00F6` | Car index | Selects the acceleration and top-speed tables at `82:8B0A`, `8B14` and `8B19`. |
| `00DC/00DE` | Buttons now / previous scan | P0: bit 7 A, bits 0–3 D-pad, bit 4 MENU, bit 5 PAUSE. `00DD/00DF` hold B, C and D. |

`80:53A3` folds the lap position with `c8 < 80 ? c8 : ~c8`, so the oval's two halves share one curve description. The thresholds `28, 30, 35, 3A, 46, 4B` select the track segment.

In Tigerbyte the race shows a new picture every 6 display periods (9.95 Hz). The cartridge waits for kernel events between pictures with service `2F`, so the pace comes from BIOS timer events, not from CPU speed. A typical race picture uses about 53 blits.

## BIOS state at the cartridge entry

The BIOS calls `80:4020` with request 3, `SP=03F4`, `PS0=00`, `PS1=24` and `SYS=46`. Timers are stopped. Only the clock interrupt is enabled. 135 non-zero bytes of RAM, I/O and extended RAM and eight register-file bytes describe the rest. `gba/indy-500/entry_state.h` holds this snapshot. The port starts from it and returns into the BIOS launcher through the stack, as the original does. The internal ROM only provides interrupt vectors: each vector jumps to the kernel at `2039 + 3n`. The runtime supplies these jumps.

## Sound

Timer 1 drives the kernel PCM handler at `2:357F` and Indy 500's own copy at `80:5C7E`. While a sample plays, the timer interrupts at 4.9152 MHz / 208. Every third interrupt writes one DAC value to `4E` and decodes a 4-bit difference through the kernel table `36D4`. Every 40 samples the handler runs the 200 Hz system tick (`35FA` and `5CFB`). The end of the sample continues at `35DC` and `5CDD`.

## Hidden and unused content

The audit required by AGENTS.md has not been done for this cartridge yet. The table lists the findings so far.

| Category | Status |
|---|---|
| Menus and codes | Car D and E codes, the 500-tap SUPER SECRET AREA overlay and three pause codes are documented in [the unlock code audit](../gamecom-unlock-codes.md). Routines `83:7938`, `83:7904` and `83:68F1` are labelled in the Ghidra project. |
| Graphics | 64 DMA pages were rendered as contact sheets. Their references from code and tables are not checked yet. |
| Sounds | Not audited. |
| Levels | The game has one oval. Track tables beyond the segment thresholds above are not decoded yet. |
