# Brothers In Arms: Hour of Heroes (PS Vita) — Beta Release v0.1

First public build of the native iOS-to-PS Vita wrapper port of *Brothers In Arms: Hour of Heroes* (iOS v1.3.1). On real hardware the game boots to the menu, the intro video and music play, and mission 1 runs from the start to the grenade tutorial at about 14.5 fps. The options menu (Select + Start) and the on-screen controls work on the console. The remaining missions are not confirmed yet.

## 📦 Release Package Contents

- **`brothersinarmshourofheroes.vpk`** — the loader (Title ID `PSVBIAHOH`, version `01.00`), with the LiveArea. No game code or assets.
- **`rebase.bin`** — relocation table for the 1.3.1 `BIA` executable (13,661 words).

## 🛠️ What is rebase.bin?

The loader maps the armv6 iOS executable at a fixed slide (`0x98000000`). `rebase.bin` lists every word holding an absolute address that must be moved by that slide. It is generated from your own executable by `tools/macho_prep.py` (run by `tools/stage_data.sh`); the attached copy matches the 1.3.1 IPA.

## 🚀 Player Installation Guide

1. **Requirements**: Enso 3.60/3.65, **kubridge** in `ur0:tai/config.txt` (`*KERNEL` section), and `libshacccg.suprx` in `ur0:data/`.
2. **Game files**: a decrypted *Brothers In Arms: Hour of Heroes* **1.3.1** IPA (`cryptid 0`). Clone the repo, extract `Payload/<App>.app/` (the executable is `BIA`), link it as `game_app` and run:
   ```bash
   tools/stage_data.sh
   ```
   This creates `ux0_data/brothersinarmshourofheroes/` with `game_app/` and `rebase.bin`, and converts the music (needs `ffmpeg`, or `afconvert` on macOS). Other versions will not match `rebase.bin`.
3. **Copy** `ux0_data/brothersinarmshourofheroes/` to `ux0:data/brothersinarmshourofheroes/` on the Vita (`game_app/`, `rebase.bin`). `controls.txt`, `options.txt`, `saves/` and `logs/` are created automatically.
4. **Install** `brothersinarmshourofheroes.vpk` with VitaShell.
5. **Options**: hold **Select + Start** in game to change button bindings, on-screen controls, screen size, filter, MSAA, FPS counter and FPS limit.

## 🎮 Controls

| Input | Action | Status |
| :---: | :--- | :---: |
| **Front Touchscreen** | iPhone touch screen (menus, HUD) | ✅ Working |
| **Left Stick** | Drags the virtual move joystick (default centre 75,245, radius 33; `LSTICK` in `controls.txt`). The D-pad drives it too while its directions are unmapped. | ⚠️ Not confirmed |
| **Right Stick** | Look drag from the `RSTICK` point in `controls.txt` (speed set on the same line) | ⚠️ Not confirmed |
| **Cross (✖)** | Taps the on-screen Cross button at (375, 290) | ✅ Working |
| **Square (◼)** | Taps the on-screen Square button at (445, 290) | ✅ Working |
| **Triangle (▲)** | Taps the on-screen Triangle button at (435, 275) | ✅ Working |
| **Circle (●)** | Taps the on-screen Circle button at (445, 165) | ✅ Working |
| **L** | Taps the on-screen L button at (30, 145) | ✅ Working |
| **R** | Taps the fire point of the right on-screen stick at (413, 261) | ✅ Working |
| **Start / Select** | Taps the on-screen Start (453, 27) and Select (174, 16) buttons at the top of the screen | ✅ Working |
| **Select + Start** | Options menu | ⚠️ Not tested on console |
| **L + R** | Show / hide the on-screen touch controls | ⚠️ Not tested on console |

## 📋 Changelog (v0.1)

- **Black title screen**: two `__DATA` words equal to `0x00010000` that point into `__text` were rebased, which turned the modelview `w` into a negative value and clipped every vertex → only known function entries are rebased.
- **Crash in `Level::RunScript`**: a jump-table literal was not rebased, because a register `cmp` bound was read as an immediate → the bound is taken from the preceding literal load (+760 table entries).
- **Crash on grenade pickup**: false rebase positives in the `__map_char` table shifted the digit module ids → only known function starts are rebased from `__DATA`, and 16-bit pairs are skipped.
- **Crash in `glDrawElements`**: the game stores GL buffer names as 16-bit values, while vitaGL names are pointers → small names are mapped to vitaGL names.
- **Video decoder**: `sceVideodecInitLibrary` ran out of physical pages → vitaGL is initialised with 16 MB of RAM and 16 MB of PHYCONT memory.
- **Music**: the BGM port was opened as the MAIN audio port, which only accepts 48,000 Hz → it uses the BGM port type.
- **Foliage textures**: RGBA5551 uploads were corrupted when minified → uploaded as RGBA8.
- **Touch & physical buttons**: physical buttons press the game's own on-screen buttons at their default positions, which can be moved with `controls.txt`.
- **Options menu**: Select + Start panel for button remap, on-screen controls (L+R), screen size, filter, MSAA, FPS counter and FPS limit, saved to `options.txt`.
- **Tilt**: the accelerometer feeds the game's tilt input (sceMotion, same scaling as the iOS code); the axis orientation is not verified on hardware yet.

## ⚠️ Known Issues

- **Tilt axes**: the iOS-to-Vita axis mapping for the grenade tutorial is not verified on hardware.
- **Frame rate**: about 14.5 fps in mission 1.
- **Not verified on console yet**: missions 2 and onward, sound effects, the stick rows of the control table.
- **Trophies**: not implemented.
- **Online features disabled**: reachability and socket calls are stubbed (offline play).

Installation and build instructions: see [README.md](README.md).
