# Tiger Web Link

Tiger Web Link (1997, model 71-747) exchanges game records stored in the game.com's battery-backed RAM with a PC over the serial cable. The operating sequence is to earn a score or save in a game, replace that cartridge with Web Link, and select **START**. The original cartridge performed the tested operations with only Web Link inserted. The session made no accesses to the second cartridge slot.

Tiger advertised submitting scores to per-game Top 25 tables and retrieving codes for hidden features, characters, and powers. The ROM can both overwrite a matching record and display a message sent by the PC. Which operation Tiger's website used for individual bonus codes and which games received them remain unverified. [Tiger packaging](https://www.diskman.com/presents/gamecom/scans/gamecom_Internet_and_Tiger_Web_Link_boxes.pdf) · [Tiger's 1998 catalog, p. 3](https://www.diskman.com/presents/gamecom/scans/gamecom-Holiday_Edition_1998-catalog.pdf)

## Operations

| Operation | What the original cartridge does |
| --- | --- |
| Read the full list | Returns ten 64-byte records from console RAM at `FC60–FEDF`, including empty slots, followed by a checksum. |
| Read one game's record | Receives the stored game name up to the `01` delimiter, looks up the record, and returns its 64 bytes plus a checksum. The name `TEST` found the seeded test record. |
| Overwrite a game record | Receives a complete 64-byte record and checksum, then overwrites the matching record in console RAM. With an empty table the same operation reports that there is no match. |
| Display a text message | Receives text ending in `01` and a checksum. The original cartridge staged `HELLO` at `FA00` as a NUL-terminated string and displayed it at the upper left of the link screen. |

The protocol exposes console records. The command table has no operations for dumping a game ROM, patching a game cartridge, launching a game, or playing it over the network.

Joshua Neal's **gclink** uses the original Web Link cartridge to list, export, and restore these records locally. Its source describes transfers between consoles and the requirement that a matching record already exist on the destination. Resident Evil 2 stores a typewriter save across two records. The Web Link test retrieved both records. gclink's notes describe limitations when editing or restoring the continuation record against a different save. Tiger's Duke Nukem 3D manual documents transferring that game's score and restricts qualifying scores to uninterrupted runs. [gclink 0.2 source and Resident Evil 2 notes](https://www.diskman.com/presents/gamecom/downloads/gclink-0.2.zip) · [Duke Nukem 3D manual, p. 7](https://messui.polygonal-moogle.com/mess/gcom_dukenukem.pdf)

## ROM and protocol

The verified 256 KiB cartridge has SHA-1 `d4e08fc050c006ed6c1a177a1f4c41b4a68c26bb` and SHA-256 `50cf190d108197e857aff4484a43c1de417b9f3c0ceafd0210846d46e2a31347`. In the session code view, MMU2–4 map cartridge banks `20–22` at CPU addresses `4000–9FFF`. The command handlers are in bank `21`. Their addresses are listed at CPU `6370` (file offset `2370`) and the corresponding status-string pointers at `637C`. [MAME cartridge catalog](https://github.com/mamedev/mame/blob/master/hash/gamecom.xml)

| PC command | CPU entry | Tested response |
| --- | --- | --- |
| `01 FE` | `61F7` | `02 FD`, session open |
| `03 FC` | `61FC` | `04 FB`, session close |
| `05 FA` | `621D` | `06 F9` then 640 bytes and checksum |
| `07 F8` | `6243` | `08 F7` then 64 bytes and checksum, or `0A F5` for an absent name |
| `09 F6` | `62B6` | `0C F3` if updated, or `0A F5` if no matching record |
| `0B F4` | `6316` | `0C F3` for valid text, `0F F0` for a bad checksum |

Each command uses a byte followed by its bitwise complement. Data checksums bring the sum of the payload and checksum to zero modulo 256. The delimiter counts toward the checksum for the name and text payloads. gclink configures the PC port for 9600 baud, 8 data bits, no parity, and one stop bit. [gclink 0.2 source](https://www.diskman.com/presents/gamecom/downloads/gclink-0.2.zip)

### Verification

In Tigerbyte, boot the console with a clean save area, select the Web Link cartridge on the system menu, wait for the title, and press **START**. The original reaches the link screen. The serial test injected receive bytes at the SM8521 UART register and captured transmitted bytes, RAM accesses, and the LCD. The test shim used `2B` for transmit, `2C` for receive, `2D` for status, and `2E` for control. [Sharp SM8521 datasheet](https://jp.sharp/ic/datasheet/micon/pdf/sm8521.pdf)

Each operation started in a fresh session. The empty table returned 640 zero bytes. The seeded table began with `12 34 00`, the ASCII bytes for `TEST`, `01`, the ASCII bytes for `00`, and zero padding. Reading the full list or looking up `TEST` returned the seeded bytes. Overwriting the existing record changed the score text to `12` at `FC68–FC69` and returned success. An overwrite with no corresponding record returned `0A F5`. The message `HELLO` appeared on the LCD and changing just its checksum returned `0F F0`.

For a second test, the table from the original [Resident Evil 2 police-station typewriter replay](../../scripts/resident-evil-2/reference/police-save.txt) was copied into console RAM just before opening the Web Link session. The full-list response matched the table byte for byte, including both save records. The padded name `RE2` returned the first record. Sending each record back unchanged returned `0C F3` and wrote to `FC60` and `FCA0` respectively. This tested matching records from the same save.

The tests ran the original cartridge and BIOS code in Tigerbyte. The electrical connection, Tiger's Windows client and website, and physical game.com hardware were outside the tested path.

### Cartridge asset audit

| Category | Finding |
| --- | --- |
| Graphics | The boot animation, title, and linking graphics were observed on the LCD. The trace visited banks `20–30` by the title and `31–3F` by the end of the linking animation. The command table references six status strings at `21:6388–6405`. Individual graphics hidden within the visited banks remain unconfirmed. |
| Menus | The title's `START` at `21:6406` leads to the session screen. The receive command at `21:6316` reaches the text display through serial input. Additional menu reachability remains unconfirmed. |
| Sounds | The opening reached the cartridge's sound writes in bank `20`, including a DAC write ending at `4B3E` and pitch writes ending at `4B38` and `4B3A`. The sound inventory and unused IDs remain unconfirmed. |
| Levels | The identified command handlers operate on the link session and records. No playable level entry was identified in that dispatch. |

### Reopen the analysis

Install the repository's [SM8500 Ghidra language module](../duke3d/duke-nukem-3d-development.md#inspect-the-original-cartridge-in-ghidra), then run the importer with your copy of the verified ROM or ZIP.

```powershell
python analysis/tiger-web-link/inspect-rom.py "originals/tiger-web-link/Tiger Web Link (USA).zip"
```

The importer labels the six handler addresses and verifies both pointer tables in `build/tiger-web-link/ghidra/WebLink.gpr`. Pass `--ghidra` to select a different installation.
