# Duke Nukem 3D for Game Boy Advance

This in-development port of Tiger's game.com cartridge runs natively on GBA. It keeps the original's three episodes, four skill levels, and step-by-step first-person combat. Duke moves forward, back, and sideways through rooms and corridors. The weapons are the boot, pistol, shotgun, and RPG.

## Build a playable ROM

From the repository root on Windows, run the converter with your own **Duke Nukem 3D (USA)** cartridge dump. It accepts a ZIP or a raw ROM and checks the cartridge revision before converting it. The cartridge alone is enough.

```powershell
.\dist\gamecom2gba.exe ".\originals\Duke Nukem 3D (USA).zip"
```

If you are starting from a source checkout, [build the converter first](../README.md#development). The converter writes both files next to `gamecom2gba.exe` and prints their full paths.

- `duke3d-12-maps.gba` follows the original campaign route with a fresh system record.
- `duke3d-15-maps.gba` includes all three bonus maps in the campaign route.

Open either `.gba` file in a GBA emulator such as mGBA. [Bonus maps and routes](duke-nukem-3d-original-assets.md#bonus-maps)

## Play

Press **A** or **Start** to skip the cartridge animation. Choose **New Game**, then pick an episode and skill level with the D-pad. Press **A** or **Start** to confirm, or **B** to go back. **Options → Sounds** lets you mute or unmute the game. **Quit** returns to the opening animation.

| GBA control | Action during play |
|---|---|
| D-pad | Move forward, back, left, or right |
| **A** | Use the door or exit in front of you. Away from a door, cycle through available weapons. |
| **B** | Attack straight ahead |
| **L** / **R** | Attack to the left or right |
| **Start** | Pause or resume |

At a locked door, the first press of **A** uses the key and the second opens the door. Move forward to pass through. The pistol reloads after six shots. Weapon cycling skips empty guns.

The pause and death screens show your four-character checkpoint password. Keep the corresponding GBA save file. To resume after restarting, open **Options → Password**, select each character with the D-pad, and press **A** to enter it. **B** deletes the last character. **Start** or **R** submits the four characters. The game resumes at the start of the saved map with the stored skill level and loadout. At the death screen, **A** immediately retries the same map with the starting pistol and full health.

While paused, **A, L, B, L, Select, Select** gives you a key and 100 armor. Release **Select** between presses. **R, A, B, A, B, B** grants the weapons and replenishes ammunition.

## Differences from the game.com cartridge

| Feature | game.com | GBA port |
|---|---|---|
| Display | 200 × 160 LCD | The original playfield stays at 200 × 160, centered on the 240 × 160 screen. The side panels carry the game.com and Tiger logos. GBA background layers and sprites draw the scene. |
| Doors and combat | Four face buttons handle the three attack directions and weapon cycling. Up plus **D** operates doors. | **A** operates doors by itself. **B** attacks forward and the shoulder buttons handle side attacks. |
| Pause secret | The key-and-armor code ends with two touches in the lower-right corner. | The code ends with two separate **Select** presses. |
| Bonus maps | A fresh system record takes the 12-map route. Three flags control access to the additional maps. | The converter builds both routes. In the 15-map build, regular exits lead through all 15 maps and Launching Facility sits between Toxic Dump and Abyss. |
| Passwords | The game.com system memory stores the checkpoint. The original password screen submits with **C**. | The GBA save stores the checkpoint and the current password. The password is also visible while paused. Submit it with **Start** or **R**. |
| Sound | Two music voices and sampled effects run through the cartridge's sound driver. | The port plays cartridge music and samples through GBA audio hardware. Gunfire can play over Duke's speech. Music runs at about 20.8 ms per duration unit, compared with about 24.9 ms on the original. |

The original control and password instructions are in the [Tiger game.com manual](https://messui.polygonal-moogle.com/mess/gcom_dukenukem.pdf). The [original cartridge notes](duke-nukem-3d-original-assets.md) describe the bonus maps and sound samples in more detail.

Both campaign routes have been exercised in mGBA. Timing and playability on physical GBA hardware remain to be verified. Continuing with a password on a fresh GBA save is still open.
