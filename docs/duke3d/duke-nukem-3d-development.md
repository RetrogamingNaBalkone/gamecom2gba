# Duke Nukem 3D: development notes

How to build and verify the converter, how the GBA runtime draws the game, and where to find the original game logic in Ghidra. For playing the port, see the [player guide](duke-nukem-3d.md).

## Build the converter on Windows

```powershell
.\scripts\duke3d\build.ps1
.\scripts\duke3d\test-reference.ps1
```

The build uses Zig 0.13.0, `gbafix`, and the Windows .NET Framework C# compiler. The build script accepts `-ZigExe` and `-GbaFix` and also searches PATH and the per-user tools directory under `%LOCALAPPDATA%\Programs`. It produces the standalone Windows converter at `dist/duke3d/gamecom2gba.exe`. [devkitPro gbafix source](https://github.com/devkitPro/gba-tools/blob/master/src/gbafix.c)

The converter accepts only cartridges from its whitelist:

| Supported cartridge | Unpacked size | SHA-256 |
|---|---:|---|
| Duke Nukem 3D (USA) | 2,097,152 bytes | `faf6912c5094477fa83a19864f43eb2acd5929166d8b9267689d6661f5ceb5bd` |

## Verify

The reference test builds `analysis/duke3d/reference.c` against a local Tigerbyte installation. Put the two BIOS archives in `originals/` as `[BIOS] Tiger Game.com (USA) (Internal).zip` and `[BIOS] Tiger Game.com (USA) (External).zip`. The script accepts `-PythonExe` and `-Tigerbyte`. Set `MGBA_LIBRETRO` to the mGBA core path if needed. [Tigerbyte](https://github.com/codingncaffeine/Tigerbyte)

For GBA-only verification after building the converter, run:

```powershell
py scripts/duke3d/test_preview.py "originals/duke3d/Duke Nukem 3D (USA).zip"
```

The tests compare ZIP and raw cartridge inputs, check that an unsupported ROM leaves the previous output intact, and exercise the opening, both campaign routes, pickups, combat, death and retry, password continuation, and audio in mGBA. The game.com reference also checks the first 150 lines of the initial gameplay frame. The bottom 10 lines include numeric HUD fields and the system font. [Test suite](../../scripts/duke3d/test_preview.py)

## Rendering and timing

The Duke runtime draws walls and the bezel on BG0, doors on BG1, and scenery on BG2. It draws pickups, enemies, and weapon frames with hardware sprites. The converter shares pixel rows across the graphics. During play, the runtime updates changed groups of three background tiles. A hardware timer drives movement and weapon cadence independently of redraws. The palette follows the game.com LCD mode and the bezel lettering was redrawn from the original console. [GBA runtime](../../gba/duke3d/duke.c) · [MAME video driver](https://github.com/mamedev/mame/blob/master/src/mame/tiger/gamecom_v.cpp) · [Bezel reference](https://www.zaonce.com/images/consoles/game.com.png) · [Console photograph](https://commons.wikimedia.org/wiki/File:Tiger-Game-Com-FL.jpg)

On the original opening street, the reference measured 16–17 display intervals between pistol shots and 78 across a reload. In the one-step-forward combat replay, both versions matched the damage from all 11 hits and the 50-frame interval between the first two hits. The first GBA hit arrived nine frames later in that replay. The port has also been checked on physical GBA hardware.

## Inspect the original cartridge in Ghidra

Install the partial `SM8500:BE:16:cart` processor module from `analysis/SM8500` in Ghidra's `Processors/SM8500` directory. Compile `data/languages/sm8500.slaspec` with Ghidra's `sleigh` tool, placing the compiled `sm8500.sla` next to `sm8500.ldefs`. From the repository root:

```powershell
.\analysis\duke3d\import-duke.ps1 "originals/duke3d/Duke Nukem 3D (USA).zip"
```

The script imports the cartridge's bank-switched startup window into `build/duke3d/ghidra/DukeStartup.gpr` and reopens that project on subsequent runs. Use `-View Actors` for frame tables in banks 22 and 26 or `-View Menu` for password, bonus-flag, and pause routines.

| CPU bank and address | Verified behavior |
|---|---|
| `20:4020` | Startup dispatcher maps banks `21` and `26` into the `6000` and `8000` windows for gameplay. |
| `21:65B9`, `65C6`, `65CD` | Read rectangle parameters and eight-byte scene entries. |
| `24:82C4` | Points to the opening background entry list. The high bit of the final entry's last byte terminates the list. |
| `26:86C2` | Idle pistol descriptor. The neighboring graphics page supplies the light pixels of the mask. |
| `22:649D`, `6B3E`, `6B94` | Compute damage from skill and enemy type. Armor absorbs half, rounded down, up to the remaining armor. Health takes the remainder. |
| `21:60F7`, data bank `27` | Select the level and room through two 16-bit pointer tables. Hollywood Holocaust has 13 room entries. Its opening area is 11 positions wide and 9 deep. |
| `21:6621`, `6718`, `67A8`, `67F9` | Movement handlers. RAM locations `C4` and `C6` hold the player's position in the room. |
| `20:43BE`, `21:69A2`, `23:6EC7` | Collision checks use the scenery or a table of blocked cells, depending on the room. |
| `21:6A18`, `23:62B0` | Select the next map using the episode and bonus flags. |
| `23:628A`, `6B87`, `6CB2`, `6CD7` | Read system record flags and handle pause secrets. |
| `21:6D4D`, `70BF`, `712B` | Weapon damage, weapon cycling, and attack and reload frame tables. The boot reaches one cell and firearms reach four. |

The [original cartridge findings](duke-nukem-3d-original-assets.md) cover the bonus maps and the digitized sound bank.
