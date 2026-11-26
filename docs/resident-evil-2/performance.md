# Resident Evil 2 rendering and timing

The GBA presents at each hardware VBlank. The game.com cartridge advances individual animation poses much less frequently. The port retains the original poses, runs gameplay on a separate clock and moves Leon's hardware sprites between confirmed walking and running positions. Room transitions, collisions, hit detection, inventory and camera selection use his logical position. The renderer interpolates a completed horizontal displacement within the same camera. It snaps to the current position on a camera change, hit or grab.

## Visible movement

The audited 7,177,276-byte GBA image has SHA-256 `1dd64118f147e7d87da96761b3a8d11a4043b0403b237beda4331da4058c4422`. The measurements count adjacent 200×160 LCD pictures with different hashes. They include the time spent entering each camera. Tigerbyte runs the original cartridge and BIOS. The GBA measurements use the Windows mGBA libretro core. For the GBA control, the benchmark builds the same runtime with Leon's OAM X coordinate taken directly from the logical position. Both GBA variants read the same converted assets and sample the same controls.

| Camera and scenario | Version | Display periods in camera | Visible changes | Changes per second |
| --- | --- | ---: | ---: | ---: |
| Street 0:01, run after six handgun shots | game.com | 118 | 13 | 6.64 |
| | GBA control | 119 | 12 | 6.07 |
| | GBA interpolated | 119 | 94 | 47.58 |
| Street 0:07, run past two active enemies | game.com | 123 | 12 | 5.88 |
| | GBA control | 119 | 12 | 6.07 |
| | GBA interpolated | 119 | 94 | 47.58 |

The first route starts from New Game. Both versions enter 0:01 with 12 rounds and X=180. The successive logical X positions are 161, 142, 120, 98, 79, 55, 36 and 17. They enter 0:02 with X=165. In the second test the harness copies a typewriter save, selects room 0:06 and restores the encounter bits in the copy. After Load Game, holding Forward and Run enters 0:07 through its normal exit. One enemy starts in the rear lane and one in the front lane. The two GBA traversals differ from their cartridge references by one and four display periods, respectively. The measurements cover these routes and the emulator's timing. Physical GBA timing has not been measured.

On build D5CB the two GBA traversals took 113 and 114 periods. DAC timing explained part of the difference. At 7,877 Hz the native footstep finished after 18.8 periods. On the cartridge the same 2,482 samples occupied 25.1 periods. The cartridge also requests the step at `23:6016` before a camera transition resets the walking pose. Restoring both behaviors brought the first displacement in 0:07 from 14 periods to the original 18. The four-period residual accumulates during the remaining run. A control build integrated the same two measured clock rates continuously over each audio start and stop. That changed the 0:07 run to 124 periods and 106 visible updates (51.47/s), and the 0:01 run to 122 periods. The shipped clock selects its threshold once per street update, including the time accumulated since the preceding logical tick. In the first camera test, the original third handgun hit knocked the wounded zombie down. The port implements the one-in-three roll at `22:6926` when a hit leaves 1-10 HP. The rolled outcome changes when the player fires at a different time, which also changes the sound already playing at camera entry.

The cartridge reloads Timer 1 every 208 master clocks. The PCM interrupt handler outputs one value per three delivered interrupts. While decoding the last source bank the end-pointer check at `20:5397` holds the global interrupt mask across another timer overflow. In one original gameplay period, 99 of 395 overflows took place while interrupts were masked, leaving 99 DAC writes. Sound 13 showed 2,472 intervals of approximately four overflows. Longer sounds also contain three-overflow intervals before their final bank. MAME likewise checks the interrupt enable and priority bits when the timer overflows. [MAME game.com timer implementation](https://github.com/mamedev/mame/blob/master/src/mame/tiger/gamecom_m.cpp#L570-L581)

GBA Direct Sound reads the original decoded samples in order. At each VBlank Timer 1 supplies the playback position. Timer 0 takes its next reload from the measured mean for that 128-sample block, with a single mean period for the final source bank. Across all 24 sounds, the largest measured timing error at a VBlank on mGBA was 1.33 ms.

In the sound-placement audit, the BIOS-font image grew from 7,177,704 to 7,178,220 bytes. The same 0:01 movement probe took 119 display periods before and after the changes. Visible updates went from 94 to 97 (47.58 to 49.10 per second) with the corrected cue order.

## Combat timing checks

An original typewriter save provided diagnostic starting points in rooms adjacent to encounters. Holding Forward entered each target through an actual camera transition. Type 7 crossed the screen from the edge and dealt five damage. In the type 8 encounter, Leon reached pose 6F, then 73, and lost 15 HP per bite. Type 9 crossed Leon's lane, knocked him into pose 75 and dealt 15 damage. Type 10's tongue preceded its sustained grasp. These samples cover one entry to each encounter. Other starts, enemy combinations and random branches can produce different timings.

The final arena test entered area 4 camera 107 from camera 101. Both platforms spawned type 12 at X=195, middle lane, facing left, in pose 01, with 125 HP. Controlled hit events on the cartridge confirmed handgun damage 3, knife damage 1 and shotgun damage 6. A lethal handgun hit from 3 HP selected pose 0F; the victory flag and pose 11 followed 148 display periods later. A diagnostic GBA run at 3 HP used the normal aim and fire controls and reached the same pose after 151 periods. The original strike test at `20:502A` hit Leon at 35 pixels in front of the boss on all three lanes. Tests at 4, 65 and 104 pixels in front and at two positions behind it preserved his health. The code checks the inclusive interval from 25 to 50 pixels once the attack reaches frame 2.

Type 13 has a separate damage gate. Original controlled hit events left its 100 HP intact while protected and after a knife hit when vulnerable. A vulnerable handgun or shotgun hit selected pose 07 with zero HP. Pose 08 and flag C2.6 followed 181 display periods later. These controlled cases measure the hit handlers and final poses separately from the normal campaign route. A diagnostic original save in 6:1A reached 6:19 through normal movement. The switch at X=32 played scene 1A and left 6:19 empty for more than 600 periods. On a second route Leon first met type 13 in 6:18, retreated to 6:19 and activated the switch. On eight returns to 6:18 the original restored the boss at X=180 in pose 01. On D5CB it respawned at X=220 in pose 02. The updated GBA room cache reproduces X=220/02 on first entry and X=180/01 on return, as specified at `22:61A8`. A separate button-driven route activated the switch, visited 6:18 and returned to 6:19. The cartridge's chase table at 36:8F05 supplies the previous room, lane and spawn position. The transfer at 20:4D2F requires an available slot, a clear entry and a one-in-120 roll. In the successful original test Leon waited at X=73 on the front lane. In mGBA he waited at X=60. The boss entered at X=220, became vulnerable at X=168 and died from one handgun hit fired to the right on the middle lane. Both runs finished in pose 08 with flag C2.6 set.

## Video memory and DMA

Mode 0 uses a room background, a separate foreground, an OBJ layer for actors and movable objects, and the shared bezel. A temporary BG supplies the death and crane overlays. The palette uses the game.com LCD shades from MAME. During a camera change, the engine prepares the inactive background banks and selects the new maps at VBlank. Each 8×8 tile is stored once in a global room dictionary. Maps index a local tile bank. Consecutive dictionary entries are uploaded in a single DMA transfer.

Across 543 distinct background and foreground tilemaps, the converter found 87,674 unique native tiles. The tile payload plus dictionary references fell from 6,297,216 to 2,968,028 bytes. Two development images spanning that change measured 10,490,748 and 7,163,892 bytes. Changes elsewhere in the runtime also contributed to the image sizes.

Area 4 camera 77 needs 201 dictionary transfers for its two planes, the largest measured fragmentation in the catalog. In a matched mGBA loop the contiguous and dictionary layouts took approximately 30,000 and 45,000 CPU cycles per preparation, respectively. The fixture alternated the two layouts for 177 consecutive display periods with no missed VBlank. These are camera preparation costs. Steady gameplay keeps those maps resident.

The separate maximum-load fixture changed 512 background tiles, queued 768 OBJ tiles in 64 DMA transfers, and published 128 OAM records each frame. It presented 178 times consecutively with a measured interval of 4,389 timer ticks at 64 cycles per tick. The latest transfer completed on scanline 208, before visible scanning resumed at line 0.

## Reproducing the measurements

Run the commands from the repository root with Zig, gbafix, mGBA and Tigerbyte available. The original comparisons also require the supported cartridge and BIOS under `originals/`.

```powershell
python scripts/resident-evil-2/perf.py
python scripts/resident-evil-2/perf_rooms.py dist/resident-evil-2.gba
python scripts/resident-evil-2/perf_motion.py dist/resident-evil-2.gba --original --unblended
python scripts/resident-evil-2/perf_motion.py dist/resident-evil-2.gba --original --unblended --checkpoint build/resident-evil-2/police-persist/original.nv
```

The last command accepts an original RE2 typewriter NVRAM produced by the reference recorder. To generate one, run `scripts/resident-evil-2/reference/capture.py` on `scripts/resident-evil-2/reference/police-save.txt` with the destination `build/resident-evil-2/police-persist` and `--nvram-out`.
