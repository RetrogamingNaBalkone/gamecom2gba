# Duke Nukem 3D game.com cartridge findings

The USA cartridge contains three playable maps outside the fresh-save campaign and 27 indexed digitized sound samples. The maps can be played in the original game through prepared checkpoints. The sound bank contains five samples that the current GBA runtime never requests.

## Bonus maps

A fresh game.com system record plays four maps per episode and loops back to Hollywood Holocaust after Hotel Hell. The cartridge contains a fifth map in each episode. The low three bits of the Duke system record's byte at `0x16` control which bonus maps the original campaign can visit. A way to set those flags through ordinary play on a stock console has yet to be established.

| Episode | Bonus map | Stage ID | Access mask | Next map in the original | Next map in the full GBA route |
|---|---|---|---|---|---|
| L.A. Meltdown | Launching Facility | `0x15` | `0x04` | Space Port | Abyss |
| Lunar Apocalypse | Lunatic Fringe | `0x25` | `0x02` | Raw Meat | Raw Meat |
| Shrapnel City | Freeway | `0x35` | `0x01` | Hollywood Holocaust | Hollywood Holocaust |

In the 15-map GBA build, the affected stretches are **Toxic Dump → Launching Facility → Abyss → Space Port**, **The Dark Side → Lunatic Fringe → Raw Meat**, and **Hotel Hell → Freeway → Hollywood Holocaust**. The converter sets all three access flags for that build. [Port build instructions](duke-nukem-3d.md#build-a-playable-rom)

### Play the maps in the original game

1. Launch the original Duke cartridge in a game.com emulator so it creates its system record. Export the emulator's 8,192-byte NVRAM image as `originals/gamecom.nv`.
2. From the repository root, create the `build/` directory if needed and run the following command. Use `lunatic-fringe` or `freeway` for the other maps and choose a new output filename. The script writes a separate image and updates the checkpoint checksum.

   ```powershell
   py scripts/bonus_checkpoint.py originals/gamecom.nv launching-facility build/launching-facility.nv
   ```

3. Load the resulting NVRAM image in the game.com emulator. Open **Options → Password**, enter **AAAA**, and press **C**. The checkpoint starts Duke with full health and a pistol with 48 rounds.

The original cartridge was played from each prepared password through the regular exit in Tigerbyte, with normal movement and combat. The reference routes recorded 48 frags on Launching Facility, 17 on Lunatic Fringe, and 11 on Freeway. [Checkpoint tool](../scripts/bonus_checkpoint.py) · [Reference replays](../scripts/test_preview.py)

The cartridge's map and room pointer tables are in bank `27`. The original exit and bonus-flag logic is at `21:6A18` and `23:62B0`. [Ghidra labels](../analysis/ghidra/DukeMenu.java)

## Digitized sound samples

The original stores speech and effects in one directory of 27 entries, numbered 0–26. The music uses separate note sequences. The converter decodes all 27 samples into the GBA ROM. A sample can therefore be present in a build even when no gameplay event plays it.

| Sample ID (decimal) | Offset in unpacked USA ROM | Approx. duration | Evidence from the original |
|---:|---|---:|---|
| 2 | `0x059212` | 1.87 s | Playback trigger unconfirmed |
| 6 | `0x05E35B` | 0.72 s | The cartridge queued it immediately after sample 9 on nonfatal close-range hits in reference traces. GBA plays sample 9 for those hits. |
| 15 | `0x06568C` | 3.79 s | Playback trigger unconfirmed |
| 19 | `0x06F40D` | 0.89 s | The original weapon code passes this ID to the sound driver at `21:7214`. The exact playback conditions remain unverified in a replay. |
| 22 | `0x0723AD` | 0.24 s | Playback trigger unconfirmed |

The five IDs above were identified by following the GBA playback calls, including the weapon, pickup, enemy, and speech lookup tables. In a Tigerbyte replay of Hollywood Holocaust, the first nonfatal close-range hit triggered sample 9 on frame 4731 and sample 6 on frame 4732, counting from BIOS startup. The trace establishes that the cartridge selected sample 6. Its audibility in the original mix remains unverified. Playback triggers for IDs 2, 15, and 22 remain unconfirmed. [GBA playback code](../gba/duke.c)

To locate the samples in the original, use the directory at CPU bank and address `26:8C71` (file offset `0x04CC71`). Each entry is a three-byte big-endian file offset. At that offset, a three-byte big-endian length includes the length field itself. The remaining bytes each encode two differential PCM samples, with the low nibble first. Durations in the table use two samples per payload byte at approximately 7.88 kHz. The original driver reads the selected directory entry at `20:5114` before decoding it. [Decoder and directory reader](../converter/DukeAudio.cs) · [Reference harness](../analysis/reference.c)
