# game.com cartridge unlock codes

The audit covered all 21 game ROMs in `originals/`, including the Castlevania prototype. The 21 checksums, as well as the two communications cartridges, matched the MAME software list. Each game was booted in Tigerbyte. The initial ROM pass searched for cheat, password, debug and unlock strings. Candidate routines were then checked with disassembly and input replays. A blank string search is inconclusive because a game can store a key sequence as binary data and draw its menus from graphics. No physical game.com was used. [MAME software list](https://github.com/mamedev/mame/blob/master/hash/gamecom.xml)

## Indy 500

The GBA port must offer cars D and E on the first visit to the standard car selection screen with a fresh save. Both cars must be selectable without entering a code.

Both car codes worked on the original ROM starting from a clean Tigerbyte session. Choose **Start** and wait for the screen showing **CAR A** and **NEXT CAR**. Tap near the very top of the LCD in the order **left, right, left, right**. The tested points are about **(23, 8)** and **(177, 8)** on the 200×160 LCD, measured from the upper-left corner. Lift the stylus between taps and let each tap register. The **ENTER CODE** box appears. Enter a sequence with the physical buttons, then tap **ENTER** at bottom right.

| Unlock | Sequence | Verified result |
|---|---|---|
| Car D | `B B C C D D` | The game displays `CAR D UNLOCKED` and selects `CAR D`. |
| Car E | `C C D D B B` | The game displays `CAR E UNLOCKED` and selects `CAR E`. |

The unlock path starts at `83:7938`. The four touches are checked at `83:7904`, and the two success paths start at `83:79C8` and `83:79E8`. The ROM tables for D and E start at file offsets `0x004C9E` and `0x004CA4`. The tested touch cells were `(1,0)` and `(11,0)` on the 13×10 grid, indexed from zero. Unlock persistence after a power cycle was not tested.

The **SUPER SECRET AREA** message appears after 500 separate taps near the upper part of the main menu logo. The replay used cell `(8,0)`, approximately `(130,8)` on the LCD. The counter is at `024C–024D` in RAM and is checked against `0x01F4` at `83:60EF`. The overlay is drawn at `83:68F1`. It displays a joke message over the main menu. The tested path did not enter a track.

The race-pause input tables also contain three codes. The screen hit regions choose which handler reads the sequence. The code paths and table entries were checked in the ROM. [Independent hardware account](https://www.diskman.com/presents/gamecom/indy500.htm)

| Where to tap while paused | Code | Effect | ROM offset |
|---|---|---|---|
| Timer in third-person time trial | `D B C D B C` | First place and final lap | `0x004CB6` |
| Left tire in first-person qualifying or time trial | `B C D B C D` | No damage | `0x004CB0` |
| Right tire in first-person qualifying or time trial | `D D B B C C` | Unlimited time | `0x004CAA` |

## Duke Nukem 3D

Start a level and press **PAUSE**. Enter six separate actions. The game displays a confirmation above the pause box.

| Effect | Sequence | Confirmation |
|---|---|---|
| God mode toggle | Tap the upper-right corner twice, then `C A A C` | `GOD MODE ON` or `GOD MODE OFF` |
| Weapons and ammunition | `B A D A D D` | `GIVEN ALL WEAPON` |
| Key and armor | `A C D C`, then tap the lower-right corner twice | `GIVEN ALL ITEMS` |

The tested touch cells were `(11,1)` and `(11,8)`, approximately `(177,24)` and `(177,136)` on the LCD. The button tables and touch checks are in cartridge bank `23` at `6B22`, `6B26`, `6B2A`, `6CEA` and `6D08`. All three confirmations were reproduced on the unchanged ROM in a test Tigerbyte build using byte indirection for opcode `5A` as in MAME. The installed build used register-pair indirection for that opcode, so each button overwrote the first slot of the pause input buffer instead of filling it. [MAME SM8500 instruction implementation](https://github.com/mamedev/mame/blob/master/src/devices/cpu/sm8500/sm85ops.h#L1246)

The three bonus maps use checksummed system-record flags read at `23:628A`. The pause cheat branches do not set these flags. The four-character checkpoint password is compared to the system record at `23:69D3`. `AAAA` works with the project's prepared checkpoints. A button code or self-contained password for the three bonus maps remains unconfirmed. [Original cartridge findings](duke3d/duke-nukem-3d-original-assets.md#bonus-maps)

## Mortal Kombat Trilogy

The GBA port must offer Ermac and Noob Saibot on the first visit to the standard fighter selection screen with a fresh save. Both fighters must be selectable without entering a code.

| Unlock | Where and how | Evidence |
|---|---|---|
| Ermac | At fighter selection, press `B C B B D`. Navigate to the left dragon emblem with the D-pad. | The original showed Ermac's name and preview. The code sets bit 6 of `02D4` at ROM offset `0x04F691`. |
| Noob Saibot | At fighter selection, press `D D C D B`. Navigate to the right dragon emblem with the D-pad. | The original showed Noob Saibot's name and preview. The code sets bit 5 of `02D4` at `0x04F63C`. |
| Secret options | Open **Options**, then press `Up C D B`. Highlight **?** and press `A`. | The original opened the switches for one-button finishing moves, instant aggressor and low damage. On a clean control run the cursor skipped the `?` icon. |

The fighter name table at ROM offsets `0x041B09–0x041B6B` includes Ermac and Noob Saibot. It has no Scorpion entry. The reported Scorpion combination remains unconfirmed.

## The Lost World: Jurassic Park

Touch **PASSWORD** on the main menu. Touch all seven characters. The accepted entries automatically open the corresponding area screen. All entries below reached their listed areas on the original ROM. The keyboard and its labels are in the ROM near `0x049563`. [Published passwords](https://www.diskman.com/presents/gamecom/lostworld.htm) · [Alternative Tembo passwords](https://gamefaqs.gamespot.com/gamecom/564275-the-lost-world-jurassic-park/cheats)

| Area | Harding | Tembo |
|---|---|---|
| 2 · Triceratops | `573CD26` | `5730D2I` |
| 3 · Pachycephalosaurus | `573DD25` | `5531D2J` |
| 4 · Velociraptors | `57GEDF4` | `5H32D26` |
| 5 · Tyrannosaurus rex | `573FD23` | `5H33D21` or `57HFDG3` |
| 6 · Pterodactyls | `5C3GD2H` | `5H34D20` or `BCGGDHH` |

## Other cartridges

| Cartridge | Findings |
|---|---|
| Batman & Robin | At the main menu, `B D B D B D` starts the ending animation. `C Left Down B Right` writes 30 lives at `02C1`. `D Up Left Left` sets an additional flag and initializes the ammunition-related state at `0208–020A`. All three original handlers executed on clean input replays. See `20:45C4` and the reversed sequence tables at ROM offsets `0x040671–0x040682`. The effect of the ammunition flag over sustained play was not measured. |
| Castlevania: Symphony of the Night (prototype) | No player-entered unlock code identified in the pass. The `secrets` match at `0x0702C1` is dialogue. The dump's release notes report a Fire Bat relic obtained with a cheat device. [Hidden Palace](https://hiddenpalace.org/Castlevania%3A_Symphony_of_the_Night_%28Game.com_Prototype%29) |
| Centipede | **Game Options → Start Level** offers a higher starting level directly. The menu label is at `0x00A73B`. No unlock code identified in the pass. [Hasbro manual](https://www.hasbro.com/common/instruct/CENTIPED.PDF) |
| Fighters Megamix | Completing courses 1–4 unlocks Kids Sarah, Janet, Hornet and Siba respectively, according to the manual and the published guide. Their names start at `0x0495FB`. The ROM also implements a gated stage-select code described below. The course unlocks were not replayed through completed courses in this audit. [Tiger manual](https://www.diskman.com/presents/gamecom/scans/Fighters_Megamix-gamecom_instructions.pdf) · [Course guide](https://gamefaqs.gamespot.com/gamecom/583714-fighters-megamix/faqs/14434) |
| Frogger | **Game Options → Start Level** offers a higher starting level directly. The menu label is at `0x04AD9F`. No unlock code identified in the pass. [Hasbro manual](https://www.hasbro.com/common/instruct/FROGGER.PDF) |
| Henry | No unlock code identified in the pass. |
| Jeopardy! | The `secret` matches are quiz content. No unlock code identified in the pass. |
| Lights Out | Published square sequences solve preset puzzles. No unlock code identified in the pass. [Tiger manual](https://www.diskman.com/presents/gamecom/scans/Lights_Out-gamecom_instructions.pdf) |
| Monopoly | No unlock code identified in the pass. |
| Quiz Wiz: Cyber Trivia | No unlock code identified in the pass. |
| Resident Evil 2 | No unlock code identified in the pass. The ROM text hits refer to in-game locks. The game.com campaign and typewriter save are covered in the cartridge notes. [Cartridge notes](resident-evil-2/cartridge.md) |
| Scrabble | No unlock code identified in the pass. |
| Sonic Jam | All three sequences in the pause table at `20:5471` executed their handlers on the original ROM. `A A D D` grants a life, capped at nine. The published `A A A D D` also worked. `A B C A` sets bit 2 of `020A` and takes the alternate damage path at `20:50F9`. `A B B A C A D A B A` sets bit 0 of `020A`, starts a counter at `02C0–02C1` and changes Sonic's display after resuming. The full gameplay effect of the third code remains unconfirmed. Wait for the pause box before entering the sequence. A reachable password screen was not established for the `PASSWORD` string at `0x052576`, part of a text block shared with Jurassic Park. [Published pause codes](https://www.diskman.com/presents/gamecom/sonic.htm) |
| Tiger Casino | **Options → Password → Yes → Pass** opens a four-symbol password screen. The validator at `20:4813` compares the entry with a password loaded from the console's game record. In the control replay, cashing out with 500 generated a password that re-entered the casino with that record present. The published `Plum / Two Bars / Plum / Two Bars` for 500,000 and `Plum / Plum / Plum / Plum` for the balance limit both went down the rejection path with clean console RAM. [Tiger manual](https://messui.polygonal-moogle.com/mess/gcom_casino.pdf) · [Published passwords](https://www.diskman.com/presents/gamecom/tcasino.htm) |
| Wheel of Fortune | Text matches occur in the puzzle data. No unlock code identified in the pass. |
| Wheel of Fortune 2 | Text matches occur in the puzzle data. No unlock code identified in the pass. |
| Williams Arcade Classics | The five game labels start at `0x000371`. No unlock code identified in the pass. |

### Fighters Megamix gated codes

The game reads enable flags from its `FMEGAMIX` system record. At `20:5256` it combines three saved flag bytes. At `20:52DC` it also checks three words located `0x2E`, `0x30` and `0x32` bytes from the start of the name field, or `0x31`, `0x33` and `0x35` bytes from the start of the 64-byte record. A Tigerbyte replay with those three words inserted into a copy of the system record reached all three handlers on the unchanged ROM. The first code added the menu item and opened the Jacky Stage preview. The two fight codes wrote the indicated health values. How the prerequisite flags are earned or distributed remains unconfirmed.

| Screen | Combination | Effect and gate |
|---|---|---|
| **Mode Select** | `D C D C` | Enables **STAGE SELECT** at `23:67DF`. Requires bit 2 of `0287`. The matching record word is `DCDC`, and the menu label is at `0x049484`. |
| Fight pause | `B A D D` | Sets player one's health to `0x40` at `21:789C`. Requires bit 0 of `0287`, no previous use of this code in the fight, and the fight-state checks at `21:7876`. The matching record word is `BADD`. |
| Fight pause | `D D D D D A` | Halves the opponent's health at `21:78DA`. Requires bit 1 of `0287`, no previous use of this code in the fight, and the fight-state checks at `21:78B3`. The matching record word is `1EAF`. |

**Internet** and **Tiger Web Link** were also screened. The Internet cartridge's `Password:` string at `0x002AB3` is on the communications path. Web Link can transfer saved records and display text from the PC. The catalog advertised game unlocks through the service. A game-specific code payload from Tiger's service remains unconfirmed. [Internet manual](https://www.diskman.com/presents/gamecom/scans/gamecom_Internet-instructions.pdf) · [Web Link cartridge research](tiger-web-link/README.md)
