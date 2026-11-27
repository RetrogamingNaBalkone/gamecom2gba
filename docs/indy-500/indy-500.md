# Indy 500

**Status: in development, not playable yet.**

```powershell
.\scripts\build.ps1
.\dist\gamecom2gba.exe ".\originals\indy-500\Indy 500 (USA).zip" ".\originals\[BIOS] Tiger Game.com (USA) (External).zip"
```

The converter accepts the ZIP files or the raw images. It checks both SHA-256 values and writes `dist/indy-500.gba`.

## How the port works

The port uses static recompilation. The cartridge code and the BIOS kernel code it calls run as native code on the GBA. The game.com hardware is replaced at the register level:

- The MMU windows map cartridge and BIOS pages from the GBA ROM.
- Blitter transfers become native layers. Opaque blits that cover no sprite go into BG0. Each of the two game.com VRAM pages has its own tile set, so an LCDC page flip only changes the BG0 character base. All other blits become hardware OBJs in blit order. An OBJ disappears once later opaque blits have covered its whole area. The side panels are on BG1.
- Timers, the clock interrupt, the key matrix and DMA completion follow the MAME gamecom driver and the Tigerbyte reference.
- The frequent steps of the kernel and cartridge PCM handlers run natively with the same register and RAM effects. The 200 Hz tick and the end of a sample run the original code.

Emulated time advances 82,288 CPU cycles (4.9152 MHz) per 59.73 Hz display period. A HALT skips to the next timer, DMA or display event.

Shared runtime: `gba/sm8500.c` (CPU core), `gba/gamecom.c` (machine model), `gba/video.c` (presentation), `gba/pcm_isr.c`. Game-specific setup: `gba/indy-500/`. Converter: `converter/indy-500/IndyConversion.cs`.

## Current state

The runtime starts the cartridge from the BIOS's request-0 call, as on the original. In mGBA the image shows the game's first screen: the INDY 500 logo and copyright notice on BG0.

The CPU core still executes cartridge instructions one at a time. It will remain as the fallback for code that is reached only indirectly. The same core, built for Windows, matched Tigerbyte RAM in every display period of a 3,100-period route. The route runs from the launch through the menus into a race. See the [lockstep check](cartridge.md#lockstep-check).

The port is still too slow: 51 game.com display periods took 1,200 GBA frames. Skipping idle time is not an option for this cartridge. The game advances the car on every pass of its polling main loop, so the distance covered depends on how many instructions the CPU executes ([details](cartridge.md#race-variables)). The port has to execute about 12,000 instructions per display period in every scene, in the same cycle timing as the original. Exact shortcuts remain possible: a delay loop or an interrupt handler can run natively if it charges the same cycles. The key scan's NOP delay loops alone account for about 15 % of the instructions in a race. The original never idles here: it runs 9,000 to 11,000 instructions per display period, mostly decompressing graphics in BIOS page 5 (`5BEE–5C19`) and polling kernel service `4F`. The recompiled code has to reach about 20 GBA cycles per instruction.

## Controls

The user approved this mapping.

| GBA | game.com |
|---|---|
| D-pad | D-pad (Up switches to the cockpit view in a race) |
| A | A (accelerate, confirm) |
| B | B (brake) |
| L | C (shift up) |
| R | D (shift down) |
| Start | PAUSE |
| Select | MENU |

### Touch controls on the original

Each screen was tried in Tigerbyte on the unchanged cartridge, with buttons and with touches. Touch cells are on the 13×10 BIOS grid.

| Screen | Touch target | Button equivalent on the cartridge |
|---|---|---|
| Main menu | ENTER (11,8) | A confirms the highlighted START or OPTIONS. |
| Main menu | EXIT (2,8) | None. It opens the game's "Do you want to Exit?" dialog. Right/Left and A answer it. YES returns to the BIOS menu. MENU opens the BIOS's own exit dialog. |
| Car selection | NEXT CAR (1,9), (2,8) | Right or Left cycles through the cars. |
| Options | Speaker icon (11,4), (12,4) | A plays the selected effect. Up/Down select the row, Right/Left change the value, and EXIT is reachable with the D-pad. |
| Race | Centre, corners, bottom | No effect while driving. |

Only EXIT in the main menu has no button equivalent. It leaves the cartridge for the BIOS menu, which does not exist in the port, so the port needs no mapping for it. The code entry box for cars D and E, the SUPER SECRET AREA and the pause codes are not part of the port: all cars will be unlocked from the start.

Race buttons on the original with the automatic transmission: A accelerates and B brakes (198 mph fell to 167 mph within 50 display periods). Up switches to the cockpit view. C, D and Down had no visible effect.

With the 6-speed manual transmission, C shifts up and D shifts down. The gear in `00CB` changes in the display period of the press. Tigerbyte recorded this sequence after the rolling start, with A held:

| Display period | Button | Gear | Speed (mph) |
|---|---|---|---|
| 3901 | D | 3 → 2 | 159 → 121 |
| 3949 | D | 2 → 1 | 119 → 63 |
| 3997 | C | 1 → 2 | 57, then climbing to 122 |
| 4045 | D | 2 → 1 | 122 → 60 |
| 4093 | D | stays 1 | 62 |

The race starts in third gear. Shifts during the rolling start had no effect while the speed was held at 160 mph. In another run, C took the car from 3 to 4 and from 4 to 5.

## Required before release

1. Translator: SM8500 to Thumb code in the converter, with direct branches. Indirect jumps go through a dispatch table, and the current core serves as the fallback.
2. Lockstep check of the recompiled code. The interpreter already matches Tigerbyte; see the [cartridge notes](cartridge.md#lockstep-check).
3. Cars D and E on the first visit to car selection, without codes. The unlock flags written at `83:79C8` and `83:79E8` still have to be located.
4. Audio output: the DAC values written to `4E`, played with Direct Sound.
5. First playable route: title, START, car A, AT, one completed lap. Run it in Tigerbyte and in mGBA.
6. Hidden-content audit; see the [cartridge notes](cartridge.md).

## Verification

| Environment | Checked |
|---|---|
| Tigerbyte (original) | Boot to the main menu, options, car selection, transmission choice, rolling start, acceleration to 226 mph, crash into the wall. Captures in `build/indy-500/`. |
| Windows build of the runtime | RAM matched Tigerbyte in all 3,100 display periods from the launch through the menus, a race and the crash. |
| mGBA (port) | Launch to the game's first screen at reduced speed. |
| Physical GBA | Not tested. |
