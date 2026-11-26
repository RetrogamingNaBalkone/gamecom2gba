# Resident Evil 2

Build the shared Windows converter and give it your US game.com cartridge dump to play the campaign on GBA.

```powershell
.\scripts\build.ps1
.\dist\gamecom2gba.exe ".\originals\resident-evil-2\Resident Evil 2 (USA).zip"
```

The converter accepts the ZIP or the extracted 2 MiB ROM. It verifies the cartridge SHA-256 before reading the asset catalog and writes `dist/resident-evil-2.gba` when run from the build shown above.

To include the original FILE and PAUSE fonts, pass your US external BIOS as the second input. The converter verifies its SHA-256 before reading the font. With the cartridge alone, these screens use the built-in replacement font.

```powershell
.\dist\gamecom2gba.exe ".\originals\resident-evil-2\Resident Evil 2 (USA).zip" ".\originals\[BIOS] Tiger Game.com (USA) (External).zip"
```

## Controls

| GBA | game.com action |
|---|---|
| D-pad | Turn left or right, move forward or backward, or highlight a menu option |
| A | Confirm, interact or use the drawn weapon |
| B + forward | Run |
| R | Draw the equipped weapon or push an enemy away |
| Select | Open or close the inventory |
| Start | Pause or resume the game |

The cartridge has separate inventory and pause screens. Pause freezes the current view and asks you to press Pause again. The inventory contains FILE, MAP, ITEM and QUIT. The GBA uses directional focus and A for touch controls.

To enter the cartridge's hidden ammunition code, press Start to pause, tap B five times, then Select and B. Press Start to resume.

## Development

The port centers the original 200 × 160 picture between the shared game.com and Tiger side panels. GBA backgrounds display the room and its foreground scenery. Hardware objects display Leon, enemies and items. Room backgrounds share a tile dictionary, and animated actors reuse pieces from their cartridge animation sequences. Direct Sound plays the 24 cartridge samples at their original rate. The display refreshes at 59.73 Hz. Game actions and scene changes use separate timers.

## Verification

Tigerbyte and mGBA both reached the first police-station typewriter from a new game. Leon picked up handgun ammunition on the street, collected four ink ribbons in the station and spent one to save. After a cold restart, Load Game restored the room, the remaining ribbons and the ammunition from each run. The two runs used different tactics. A separate matched street fight checked six shots and the magazine and reserve counts after switching to the knife and back.

The mGBA campaign replay continues from that save, solves the police-station puzzles, uses the two stones, visits all seven areas and defeats both bosses. Leon lures the alligator to the gas cylinder, fires a single shot, then reaches the final arena and wins with the handgun. The ending displays 00:20:27 followed by THE END and returns to the title. The replay uses controller input and observes progress in memory. To reproduce it on the build with the BIOS font, run `python scripts/resident-evil-2/reference/test_gameplay.py --campaign --bios-font`.

The original cartridge replay starts at BIOS power-on and uses the game.com buttons and touchscreen. It also defeats both bosses, displays 00:51:00 and returns to the title after 236,588 display periods. Both endings have the same 32,000-pixel THE END picture. The routes use different inputs and spend different amounts of time in rooms. The matched camera traversals below compare gameplay pace.

The scene tests replayed both complete opening sequences against the cartridge, compared every picture change and checked all 38 subsequent scene timelines. Four opening pictures and the first page of each of the twelve FILE documents matched across all 32,000 LCD pixels in mGBA. Separate comparisons checked the crane behind an actor, the death overlay above the street, the ending time and the final picture.

All 24 sounds matched the cartridge DAC output sample for sample. A separate test compared the GBA FIFO playback position at each VBlank with the timestamp of the same sample on the original cartridge in Tigerbyte. The largest measured timing difference was 1.33 ms. The full original route requested 20 PCM sounds. Across 237,226 display periods, including the ending, the cartridge made zero pitch-register writes and never enabled its two-voice score player.

In mGBA, the two street benchmarks produced about 48 visible updates per second while running. The first camera traversal took 119 display periods compared with 118 in Tigerbyte. The second took 119 compared with 123. The port has yet to be tested on a physical GBA. The [performance measurements](performance.md) give the original and port timings, ROM sizes and VBlank results.

The headless original capture is documented in [reference notes](reference.md). See the [cartridge analysis](cartridge.md) for Ghidra imports, resource tables and reachability. Working captures and Ghidra projects belong in `build/resident-evil-2/`.
