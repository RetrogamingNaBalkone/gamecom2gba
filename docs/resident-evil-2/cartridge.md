# Resident Evil 2 cartridge research

The supported US cartridge contains 2,097,152 bytes. Its unpacked SHA-256 is `80d449b89d02ba07ec386c54724e02033eb5373cb6621d5762f7184029ff2cac`. The converter and the Ghidra importer verify the content before extraction.

## Ghidra project

Run the importer from the repository root with the shared SM8500 processor installed in Ghidra. The private project is saved under `build/resident-evil-2/ghidra/RE2Startup.gpr`. Repeating the command reopens it.

```powershell
python analysis/resident-evil-2/import-cartridge.py "originals/resident-evil-2/Resident Evil 2 (USA).zip"
```

The cartridge entry is `20:4020`. The dispatch for BIOS request 1 at `20:4092` maps page 21 at CPU address 6000 and page 25 at 8000. CPU pages are 8 KiB. The DMA graphics pages are 16 KiB. The script imports those startup windows and alternate banks for the scene catalog, enemy logic, camera transitions, room directories and the pursuit tables in bank 36. Ghidra checks the scene pointers, graphic descriptor bounds and placement counts. The partial SM8500 language stops on unsupported opcodes. Instruction boundaries and register assignments in the routines below were also inspected with Tigerbyte's SM8521 decoder.

## Graphics, scenes and sound

| Bank and address | Evidence |
|---|---|
| `20:42F0`, `42FC` | Code pages in bank 21 and dispatches through 23 big-endian service vectors. |
| `21:6330`, `6365`, `30:8B04` | A two-byte pointer per scene selects a last slot index, delay byte and big-endian slot words. Scene 1 has last index 147, scene 2 has 152. A slot of the form FFxx requests sample xx and immediately advances the slot cursor. |
| `21:63A1`, `6098`, `6169` | A placement contains crop height, crop width, a descriptor pointer, destination X/Y, flags and a four-color remap. FF ends a list. A graphic descriptor contains width, height, source X/Y and the DMA page. Addresses starting at A000 refer to bank 30 with 2000 subtracted. Other descriptors use bank 25. |
| `21:63C1`, `63EE` | Four separate title and splash lists use the same placements. |
| `20:52F7`, `21:7122` | A one-based ID indexes 24 three-byte absolute sample pointers. The contiguous sample region runs from file offset E0000 through FAE80. Each three-byte big-endian length includes the header. Lower nibble first, two differential samples per data byte. |
| `20:547D`, `533F` | Reload 208 on the 4,915,200 Hz clock, one DAC write per three interrupts. The nominal timer rate gives 7,876.923 samples per second before interrupt and instruction delays. With the game running from the same street checkpoint, sample 0D took 2,067,859 master cycles between its first and last DAC writes (5,897.22 samples per second). The decoder starts at index 3F and reads three bytes beyond the formal block end, producing six additional samples. All 24 native PCM streams matched both the original DAC samples and their lengths. |
| `20:5546`, `5799`, `582F`, `58C8` | Two musical voices have separate score cursors, three-byte note records and a shared frequency table. Their use remains unconfirmed. A clean-boot 4,292-frame opening/street tape and separate combat and inventory tapes produced zero pitch-register writes, zero wave-channel enables and zero score enables while the cartridge occupied the startup window. |

The two-voice score timer at `20:5470` skips its call to `5546` unless RAM 0169 has bit 0 or bit 4 set. The voice entry points at `20:5789` and `581F` initialize the score cursors at RAM 016A and 016C. A scan of code banks 20 through 24 found no direct calls or jumps to either initializer. The separate write at `20:5956` clears both voice-enable flags. Indirect reachability outside the verified routes remains unconfirmed.

The cinematic directory contains 40 sequences, 752 slots, 156 distinct placement lists, 257 placements and 75 source descriptors. The native cinematic asset block stores 182 distinct pieces after cropping, mirroring and color remapping. It occupies 1,025,700 bytes. Source zero is transparent when placement bit 0 is set. Opaque zero is drawn with the selected palette. Bits 6 and 7 mirror horizontally and vertically. Comparing reconstructed opening slots 6, 32 and 105 to Tigerbyte frames 1928, 2408 and 3608 found zero differing LCD pixels across each full 200×160 frame.

To reproduce the original sample and music activity trace for the opening, run `python analysis/resident-evil-2/trace-sound.py scripts/resident-evil-2/reference/start.txt`. The first scene event requests decimal sample 18. Starting play requests 16.

A separate 15,708-frame idle trace recorded sample 18 at frames 1,838, 4,255, 7,658 and 11,061. The clean-boot route to the police-station save also completed 28,958 frames with zero pitch writes, wave-channel enables or score enables.

## Campaign and saves

The camera directory at `28:8000` has seven area entries. Their camera tables have 33, 76, 41, 43, 112, 78 and 37 slots respectively. Each camera record has four big-endian pointers for the background, boundaries in page 33, lane obstacles in page 28 and foreground. Slot 3:1C has a null boundary pointer.

`24:6000` selects one of four directional boundary programs. `24:6276` processes entry instructions 00 through 11. The first camera in the street leads left to camera 1 and enters at X=180 in the front lane. Returning across its right edge places the player at X=20. Doors with the high command bit set additionally check the action button, facing, position and any required key. `24:6D73` and `6DE9` project X between depths using camera-specific parameters at `24:6E79`.

The seven four-byte typewriter entries at `24:6D56` specify area, camera, complete facing/depth byte and the left edge of a 15-pixel interaction range. `24:6CDA` checks the action, player pose and remaining ribbons at RAM 0298, consumes one ribbon, saves and selects scene 1D. `20:44A9` writes 31 bytes to BIOS record 1B3A and 21 to 1B3B using RAM-address lists at `20:4525` and `4565`. Continue reads the same lists at `20:44DB` and reinstates the player from the default pose at `20:524C`, initially X=100 and Y=155.

The ground-item directory at `29:9236` contains 83 room appearances of 81 distinct records with 11 item IDs. Each record identifies a RAM presence bit, an item ID, a graphics descriptor and a placement. `23:6FAC` checks the player's X range and lane. Picking up ID 07 supplies 40 handgun rounds, ID 10 supplies 20 shells and ID 0B supplies four ribbons. The eight storage locations at `24:6CC1` use the six carried slots and 13 box slots. Each container maintains its own stack counts.

Seven rooms have movable puzzle objects. `24:7619` selects a room and `24:78AC` indexes its parts. `24:7783` tests contact and `24:7677` updates the position at 020E and completion bits at 020F. Completing the two position pairs awards item 18 or 1C when a carried slot is free. The native converter stores each drawable piece as up to four 64×64 OBJ squares. A host test against the converted cartridge data exercised pickup confirmation and cancellation, a storage transfer, both puzzle rewards, the first pair of camera transitions and a ribbon save. The continuous Tigerbyte route below separately exercised both puzzle rewards, a storage transfer and the typewriter.

The ending path at `20:4F79` runs scene 28 (hexadecimal) and updates the record time at `20:440A`. Reducing the 125 HP of enemy type 0C in area 4, camera 6B to zero sets the ending flag at `22:70B5`. The native mGBA route defeated the boss with the handgun and reached the ending. The continuous original-cartridge route below reached the same ending through ordinary controller and touch input.

## Hidden and unused content

| Category | Finding |
|---|---|
| Sounds | All 24 sample offsets and block ends are valid. The completed campaign requested 20 of the 24 IDs. A separate knife and defeat route added 01 and 06. Scene slots explicitly request 03, 10, 12, 13, 14, 15, 16 and 18 (all hexadecimal). IDs 11 and 17 have valid sample data. Their use and active use of the two-voice score engine remain unconfirmed. |
| Graphics | All 75 source descriptors in the scene directory have placement references. Reachability of every graphics page, every camera and all 40 sequences remains unconfirmed. |
| Levels | The normal graph of 420 camera slots has no incoming edge to area 3 camera 1C. Area 6 cameras 13 through 16 form a group with incoming edges only from within the group. The direct camera writes in executable banks 20 through 24 select other cameras or use the normal destination. Continue restores a previously saved camera. No normal entrance to those five slots was identified. Reachability through any further indirect dispatch remains unconfirmed. |
| Menus | The Pause handler at `21:7152` can select only entry 1 of the three patterns addressed by `21:71EB`. It takes five B presses, then D and B. The cartridge sets D6 bit 2, requests sample 10 (hexadecimal) and sets the loaded ammunition display to one at `23:70D1`. Patterns at `21:71F1` and `71FF` have data pointers but no selection branch in this handler. A separate rolling input check at `20:469F` can assign area IDs 0 through 6. Its entry follows a return instruction at 469E. No direct code pointer or incoming short branch was found in the inspected executable banks. Activation through the regular interface remains unconfirmed. |


## Original campaign replay

The input tape in `scripts/resident-evil-2/reference/police-save.txt` begins at the system menu after a clean boot. In Tigerbyte, the first street pickup in area 0, camera 5 increases the handgun reserve from 45 to 85. Leon enters the police station at frame 27,384 and reaches area 1, camera 4 at 28,365, with 40 HP and seven loaded rounds. He collects four ink ribbons at 28,697. At 28,958 the typewriter has removed one ribbon and started save scene 1D. The tape contains button and touch inputs only.

A continuation in the original also collected item 17 in area 1, camera 1. At the storage box in area 1, camera 0F, exchanging the knife for first-aid spray 0E and using it restored health from 40 to 100. Exiting the box with D returned RAM 0261 to 2 and cleared 0270.

In the side area, ordinary inputs moved the two props in cameras 13 and 14 to X=82 and X=10. The cartridge set the low pair of bits in RAM 020F and placed item 18 in the bag. The two props in cameras 03 and 04 reached X=41 and X=83, set the next pair of bits and awarded item 1C. Equipping the built-in lighter (0D) and using it in area 1, camera 20 yielded the other stone, item 14. Camera and item identifiers in this paragraph are hexadecimal. The original consumed both stones at the sockets in area 5, cameras 35 and 36. At frame 104,536, D3 held 63, the bag contained key 15 and the final socket had selected scene 26.

The original route used key 15 to reach key 16 in area 1, camera 08 and entered area 2 at frame 116,740. Moving the prop in area 2 camera 0A opened the route to area 3, first entered at 122,658. A second prop in area 3 camera 26 gave access to key 1A in camera 27. An independent cold replay of these first 133,544 frames recorded zero score enables, wave enables and pitch writes. The player used key 1A at the door in camera 0C, reached area 6 at frame 147,977 and operated the sewer switch in camera 19 at X=34. The original set D6 bit 5 and played scene 1A. Entering camera 18 then spawned the type 0D crocodile at X=220. Area, camera and item identifiers in this paragraph are hexadecimal.

On returning from sewer camera 18 to 19, the original cached the live crocodile in RAM 023B. The pursuit directory at 36:86F8 links these two rooms. The delayed entry at 20:4D2F requires a random result of 100 from 120 and limits Leon to X=100 or less at 20:4D79. Waiting at X=73 in the front lane brought the crocodile into camera 19. It approached the gas cylinder, selected action 6 and set C2 bit 5. Shooting from the middle lane reduced its 100 HP to zero. The death sequence selected action 8 and set C2 bit 6. Leon retained all 35 HP and entered laboratory camera 04:00 at frame 181,666. Independent clean replay through frame 181,678 registered zero score, wave or pitch writes.

In the laboratory the cartridge gave Leon item 1B in camera 1F. Using it in camera 1B consumed the item and selected scene 0D. Item 11 in camera 5F opened the route through camera 4B. The player entered camera 6B with 70 HP and 4 loaded rounds plus 120 in reserve; the final enemy had 125 HP. The ending flag was set at frame 235,426, with 50 HP and 12 loaded rounds plus 66 in reserve. The game displayed a clear time of 00:51:00 and THE END at frame 236,285, then returned to the title. Replay the complete 236,588-frame tape in `scripts/resident-evil-2/reference/campaign-gamecom.txt` from a clean boot.

A second copy of the emulator replayed the published tape through frame 236,588. The ending flag rose at frame 235,426. The cartridge made zero pitch-register writes and zero wave-channel or score enables. Its requested PCM IDs, in hexadecimal, were 02, 03, 04, 05, 07, 08, 09, 0A, 0B, 0C, 0D, 0E, 0F, 10, 12, 13, 14, 15, 16 and 18. The separate clean-boot tape `scripts/resident-evil-2/reference/knife-and-defeat.txt` recorded a knife swing (06) at frame 4,796 and Leon's defeat cue (01) at 7,658. The pistol-only winning route and that 9,661-frame alternate route together exercised 22 distinct sample requests.
