# Resident Evil 2 cartridge reference

The reference cartridge is the US 2 MiB `.tgc` image with SHA-256 `80d449b89d02ba07ec386c54724e02033eb5373cb6621d5762f7184029ff2cac`. The headless capture uses the installed Tigerbyte sources and the internal and external game.com BIOS archives in `originals/`. It verifies all three inputs before exposing them to the emulator.

```powershell
python scripts/resident-evil-2/reference/capture.py scripts/resident-evil-2/reference/start.txt build/resident-evil-2/start-reference --dma --state
```

Use an unused output directory. Each line of a tape specifies a duration in display periods, a checkpoint label, and optional held inputs. `8 press A` holds game.com A for eight periods. `8 cartridge @2,2` touches BIOS grid column 2, row 2. A line without inputs releases all controls. The capture starts from a clean BIOS boot.

The tool writes a PNG and a raw shade-array SHA-256 for each checkpoint, a per-frame CPU/MMU and sound trace, and stereo PCM in a WAV file. The optional DMA trace records the actual transfer start, the cycle within the frame, the CPU banks, and registers $30–$3F. CPU writes to LCDC and LCH are separate events. `--state` adds the original RAM ranges $C0–$DF and $0208–$029B. These local captures stay under `build/`. Frame numbers below are one-based Tigerbyte display periods at 59.732155 Hz. For a long route, `--checkpoints-only` advances every recorded input and produces snapshots and traces at named checkpoints and the final row. It omits the WAV and intermediate rows labelled `input`. Both capture modes produced identical PNGs for all 13 original opening checkpoints.

## Boot and first encounter

| Frame | Original observation on the start tape |
|---:|---|
| 1201–1208 | The BIOS accepts the cartridge icon at touch column 2, row 2. |
| 1516 | The cartridge enters scene 1. The title and copyright are visible at frame 1568. |
| 1838 | Scene cursor $CD reaches slot 2. The copyright has cleared by frame 1928. |
| 2408, 3608 | The uninterrupted opening reaches two distinct narrated images. |
| 3609–3616 | A skips the opening. The New Game / Load Game menu is visible by 3616. |
| 3737–3744 | A selects New Game. The opening street shows Leon and the first zombie at 3984. |

Independent runs from the same street state distinguish aiming, firing and inventory. Two short C+A attempts while aiming consumed one round by the observation point (17 loaded, 45 in reserve). The same C+B tape left 18 loaded and 45 in reserve. Original RAM $0295 and $0296 store these quantities as binary integers.

In Tigerbyte with the US cartridge and in mGBA with the port, Leon started in his initial position on the first street screen, with 18 rounds loaded and 45 in reserve. C on game.com and R on GBA raised the handgun. After one shot and release of C or R, Leon stayed in the aiming pose with 17 rounds loaded. Pressing A by itself fired again, leaving 16. A second press of C or R lowered the handgun. Pressing A again left the ammunition count at 16.

## Walking toward and away from the camera

In the first street camera, Leon starts at X=100, Y=155, facing the screen. Holding Down takes him backwards through Y=123 to Y=97. Releasing Down and holding Up brings him back across both planes. A second route turns Leon away from the screen, releases Up during the first step, then uses Down to return. In Tigerbyte the step finishes after release. The cartridge selects actions 6A/6B for forward steps and 6C/6D for backward steps.

The cartridge renderer at `21:6715` interprets frame markers 81 and 82 as perspective movement. The first two poses shift by 10 and 20 pixels vertically between the near and middle planes, or by 8 and 16 pixels between the middle and far planes. The third pose lands at the new position. The previous GBA build drew both intermediate poses at the starting coordinates and used forward-facing movement sequences when backing up. On the rebuilt ROM, the 27-by-119-pixel crop around Leon matches the Tigerbyte LCD at all 18 sampled poses in mGBA. The regression runs in `scripts/resident-evil-2/reference/test_gameplay.py`. Physical GBA verification is pending.

## Backsteps, wounds, knife stance and zombie landings

Controlled probes start in the first street camera. Tigerbyte and mGBA use X=100, Y=155 for Leon. The zombie probes put the first zombie at X=60 so the horizontal part of perspective movement is visible. Coordinates below are in game.com screen pixels.

| Starting state and input | US cartridge | Previous GBA behaviour |
| --- | --- | --- |
| Facing right, full health, hold Down and B | Backs left while facing right (action 62). The first three steps reach X=92, 82 and 72. Facing left, Down selects 63 and moves right. | Used a forward walk or run facing the direction of travel. |
| Facing right, 60 HP, idle then Up and B | Idle 0A alternates two poses. Injured walk 64 reaches X=108, 111 and 118. | Healthy standing pose followed by a run. |
| Facing left, 30 HP, idle then Down | Idle 16 alternates two poses. Backstep 67 reaches X=108, 111 and 118. | Healthy standing pose followed by a forward walk. |
| Facing right, knife equipped, raise weapon | Draw 3A ends in knife stance 22. | Draw ended in injured idle pose 0A. |
| Zombie shifting one plane at X=60 | Frame 3 of action 0E lands at (64,123) from the near plane. Frame 3 of 0F lands at (56,155) from the middle plane. | X stayed at 60 and Y changed after the last landing pose. |

Bank 23 selects backsteps at `63A4` and applies the 60 and 30 HP thresholds at `6A86`. The knife stance is in the 20-27 range. The zombie sequences use the same 81/82 perspective markers as Leon. The mGBA regression checks the action, frame and position in seven scenarios. It also compares LCD crops for backsteps, injured walking and idle poses, the drawn knife and both zombie landings against Tigerbyte. Zombie pursuit decisions after landing may differ. Physical GBA verification is pending.

## Inventory and pause

| Original action | Observed result |
|---|---|
| Pause on the street | The current scene freezes behind “Press PAUSE to continue.” Another Pause resumes the scene. |
| D on the street | Opens the equipment screen. D returns to the street from the item screen or file reader. |
| Touch MAP at (7,0) | Displays the STREET floor plan and a position marker. |
| Touch FILE at (5,0) | Opens the list of twelve files. Down selects the next file and A reads it. A in the document returns to the list. All twelve first pages were opened from a new game. |
| Touch the knife at (11,3) twice | Selects the knife and opens EQUIP / CHECK / COMBINE. CHECK shows its description. |
| Touch EQUIP at (7,3), then YES at (6,9) | Equips the knife, sets $0268 from 1 to 3, empties the magazine and transfers its contents to the reserve (18/45 becomes 0/63). |
| Touch QUIT at (11,0) | Returns to the title and restarts the opening. |
| Menu on the street | Opens the system exit confirmation. Right followed by A can return to gameplay. |

The map's count and five-byte drawing records start at ROM offset `0x6803B`. The file pages have big-endian addresses, `FF` line breaks, and `FE` page terminators. The first two document directories are at `0x6BB54` and `0x6E000`. The inventory uses its own graphics. The reader and the simple pause message use the system font on game.com. Supplying the supported external BIOS reproduces the original 8×8 FILE and 8×16 PAUSE glyphs from DMA page 6. A cartridge-only conversion uses a replacement font. The first page begins at (0,8) and each FF advances eight pixels. The cartridge arrow occupies (185,152) through (199,159).

The comparison environments were Tigerbyte for the original cartridge and mGBA for the port. Original cartridge hardware and physical GBA timings remain unverified.

The US cartridge marks the selected answer in the pickup and equipment confirmations with a triangle. The GBA port underlines the selected answer to keep the YES and NO glyphs clear. In mGBA, both focus positions retained the original glyphs in both dialogs. On the street, choosing NO left 45 reserve rounds and choosing YES raised the reserve to 85.

## Police station and records

The tape `inventory-street.txt` shoots the first zombie, equips the knife, switches back to the handgun and crosses two camera boundaries. Tigerbyte reached camera 1 at frame 4978 and camera 2 at 5095. Both platforms counted 12 loaded and 45 in reserve after the encounter, 0/57 with the knife, and 18/39 with the handgun re-equipped.

The longer original `police-save.txt` tape reached the typewriter in area 1, camera 04 at frame 28958. Saving consumed one of four ink ribbons. With the resulting NVRAM, `load.txt` selected KEEP at BIOS boot and Load Game in the cartridge. It restored 7 loaded rounds, 13 in reserve, three ribbons and the saved time of 00:05:50. The player had 40 health when saving and 100 after loading.

`python scripts/resident-evil-2/reference/test_gameplay.py --bios-font` exercises the GBA build with the external BIOS font. The test compares the FILE list, the first page of all twelve documents, the PAUSE panel, floor plan, handgun description, knife and ammunition against Tigerbyte. It reaches the first police typewriter from New Game with ordinary controls, checks the TIME graphic against the original, then cold-boots the saved game. Omit `--bios-font` for cartridge-only builds. Route timing and ammunition depend on the inputs and encounters.

A subsequent mGBA route from this checkpoint obtained item 17 in 01:01, exchanged the knife for a first-aid spray in 01:0F, pushed both pairs of movable objects in 05:13/14 and 05:03/04 for 18 and 1C, and equipped the lighter in 01:20 for 14. The two sockets in 05:35/36 yielded 15. This unlocked the room containing 16 in 01:08, followed by the locked door in 01:3E and entry into area 02. Area and camera identifiers in this paragraph are hexadecimal.
In area 02 the route pushed the object in camera 0A to unlock the path through 19. In area 03 it exchanged items at the box, pushed the object in 26, obtained key 1A in 27 and spent it at 0C. In area 06 the route activated the trap in camera 19, returned from 18 and defeated the crocodile with a handgun shot before proceeding through 17 and 24 to the laboratory. Laboratory item 1B from 04:1F changed the route at 04:1B, and item 11 from 04:5F opened the final corridor beyond 04:4B.

On the original, the trap switch in 06:19 at x=30 on the front plane plays scene 1A and sets $D6 to 30h. The crocodile starts in camera 18. After returning to 19, staying on the front plane near x=73 allows a random encounter check to move it into the room. Leon changes to the middle plane and shoots right once the crocodile has the canister. The native replay verifies the resulting $C2 bit 6 before proceeding.

## Complete cartridge campaign

`python scripts/resident-evil-2/reference/capture.py scripts/resident-evil-2/reference/campaign-gamecom.txt build/resident-evil-2/original-campaign --verify-campaign` replays a fresh Tigerbyte BIOS boot through the return to the title. Every transition comes from the recorded controls, including touch coordinates. The test reads the crocodile and final enemy flags, initial boss health and the complete THE END image.

The cartridge route uses the first police typewriter at display period 28958, defeats the crocodile by period 174583, and enters the laboratory at 181678. Leon reaches 04:6B at period 228988 with 70 HP, a handgun with four loaded rounds and 120 in reserve. The enemy starts with 125 HP and its defeat flag is set at period 235426. Leon survives with 50 HP. Scene 40 displays a clear time of 00:51:00, and the original THE END checkpoint is period 236285. The original and GBA routes use different movement, combat and menu inputs, so the clear times describe the two recorded runs.

## Complete GBA campaign

`python scripts/resident-evil-2/reference/test_gameplay.py --bios-font --campaign` begins with New Game, reaches the first typewriter, cold-boots with its SRAM, selects Load Game, and plays through the return to the title. The campaign replay uses the recorded buttons in `campaign-gba.txt`; it reads emulated RAM to verify rooms, acquired keys, the ending and the clear-time record. Input lines hold the listed buttons for the stated number of display periods. An empty button list releases the controls.

The verified BIOS-font build has SHA-256 `8ecc7986fd6c676fd9fb2d1c11e5507732defab1b415592cca0f53495ce6e15b`. The typewriter was reached at display period 18411 from a clean boot, showing 00:03:56. After the cold boot and load, Leon defeated the crocodile at period 64842 and reached the final encounter in 04:6B at period 87408 with 80 HP. The handgun reduced the 125-HP enemy to zero. The ending displayed a clear time of 00:23:31, removed TIME at slot 30 on period 93880, and returned to the title at period 94234. The complete THE END image at slot 33 matched the original SHA-256 `84ac973156d3f3169e420eaae99511607d3a879292acecf68d8adda3600b1b3c` across all 32,000 LCD pixels. The replay also checked the recorded clear time in SRAM. Period counts start at each boot.

To regenerate the tape after an engine change, run `python scripts/resident-evil-2/reference/generate_campaign.py`. Its default output is `build/resident-evil-2/campaign-gba.txt`. The pilot reads room, pose, carried items and puzzle flags, then sends ordinary D-pad and button presses. The waypoint file covers the specific audited route. At the final encounter the generator explores button sequences using mGBA snapshots. It writes a tape only after a separate run replays those buttons from the ordinary SRAM save, matches THE END to Tigerbyte and returns to the title.
