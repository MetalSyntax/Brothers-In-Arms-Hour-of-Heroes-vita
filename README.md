<h1 align="center">
  <img src="extras/livearea/pic0.png" alt="Brothers In Arms: Hour of Heroes PS Vita Banner" width="700"><br>
  Brothers In Arms: Hour of Heroes · PS Vita Port
</h1>

<p align="center">
  <b>Native port of Brothers In Arms: Hour of Heroes (Gameloft) for PlayStation Vita and PlayStation TV.</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-PS%20Vita%20%7C%20PS%20TV-003791.svg?style=flat-square&logo=playstation" alt="Platform" />
  <img src="https://img.shields.io/badge/Origin-iOS-000000.svg?style=flat-square&logo=apple" alt="Origin" />
  <img src="https://img.shields.io/badge/Title%20ID-PSVBIAHOH-ff69b4.svg?style=flat-square" alt="Title ID" />
  <img src="https://img.shields.io/badge/Engine-Gameloft-brightgreen.svg?style=flat-square" alt="Engine" />
  <img src="https://img.shields.io/badge/Renderer-vitaGL%20%28GLES%201.x%29-orange.svg?style=flat-square" alt="Renderer" />
  <img src="https://img.shields.io/badge/Status-Playable%20%28beta%29-yellowgreen.svg?style=flat-square" alt="Status" />
</p>

<p align="center">
  <a href="#-description">Description</a> •
  <a href="#-features">Features</a> •
  <a href="#-controls">Controls</a> •
  <a href="#-installation">Installation</a> •
  <a href="#-known-issues">Known Issues</a> •
  <a href="#-building-from-source">Build</a> •
  <a href="#-credits">Credits</a>
</p>

---

## 📖 Description

An unofficial, native **iOS-to-PS Vita wrapper port** of *Brothers In Arms: Hour of Heroes* (iOS v1.3.1, developed by **Gameloft**).
There is no `.so` here: this port ships its own **Mach-O loader** that maps the official armv6 iOS executable (`BIA`, from the 1.3.1 IPA)
into PS Vita RAM and replaces the Apple frameworks it uses (Foundation, UIKit, CoreFoundation, CoreGraphics, AudioToolbox, OpenAL, OpenGL ES)
with native implementations on top of **vitaGL**, **openal-soft** and the Vita system libraries.

---

## 🎮 Status & Playability

**Status: BETA v0.1 — PLAYABLE, mission 1**

On physical PlayStation Vita hardware the game boots to the menu and mission 1 runs from the start to the grenade tutorial at about 14.5 fps.
A later commit reports mission 1 completed on the Vita; the remaining missions are not confirmed yet.

Release notes: [RELEASE.md](RELEASE.md) · GitHub release: [v0.1](https://github.com/MetalSyntax/Brothers-In-Arms-Hour-of-Heroes-vita/releases/tag/v0.1).

- [x] **Mach-O loader**: loads the armv6 executable at a fixed base (`MACHO_SLIDE = 0x98000000`), with rebase data generated from the IPA by `tools/macho_prep.py`. *Verified on console.*
- [x] **Graphics**: GLES 1.x → **vitaGL** (`SOFTFP_ABI=1`). The game's 480×320 landscape picture is shown full screen (960×544) by default; the original aspect with side bars can be selected in the options menu. Foliage textures are correct after the RGBA5551 → RGBA8 upload fix. *Verified on console.*
- [x] **Touch & Controls**: the front touchscreen drives the game's touch slots, and the physical buttons press the on-screen buttons at the default positions in `input.c`. The buttons can be moved with `controls.txt`. *Verified on console.*
- [x] **Options menu** (Select + Start): button remap, on-screen controls (L+R), screen size, filter, MSAA, FPS counter and limit. See [Port options](#port-options-select--start).
- [ ] **Tilt**: the grenade tutorial asks for device tilt. The motion mapping is implemented but its axis orientation is not verified on hardware.
- [x] **Audio**: background music plays on console (the intro video and BGM ran in console run #3); the music is converted to PCM WAV by `tools/convert_audio.sh`. *Verified on console.*
- [ ] **Sound effects**: wired to OpenAL; not confirmed on console yet.
- [ ] **Missions 2 and onward**: not confirmed on console.
- [ ] **Trophies**: not implemented.

### 📋 Changelog (v0.1)

The full v0.1 changelog is in [RELEASE.md](RELEASE.md): black title screen, crashes in `Level::RunScript`, on grenade
pickup and in `glDrawElements`, video decoder memory, BGM port type, foliage textures, touch and button mapping,
the options menu and tilt input. The per-run diagnosis history is in [`port_progress.md`](port_progress.md).

---

## ✨ Features

| Feature | Status | Notes |
| :--- | :---: | :--- |
| Boots to gameplay | ✅ | Boots to the menu and runs mission 1 to the grenade tutorial on console (about 14.5 fps). Missions 2+ not confirmed. |
| Graphics / renderer | ✅ | GLES 1.x → vitaGL (`SOFTFP_ABI=1`), 480×320 shown full screen (960×544) by default. Foliage textures fixed (RGBA5551 → RGBA8). |
| Audio & music | 🟡 | Background music plays on console (`tools/convert_audio.sh`). Sound effects are wired to OpenAL but not confirmed on console. |
| Video / cutscenes | ✅ | The intro video plays on console (console run #3). |
| Physical buttons | ✅ | Each physical button taps the game's own on-screen button at its default position (`input.c`, movable with `controls.txt`). |
| Analog sticks / camera | 🧪 | Virtual move joystick (`LSTICK`) and look drag (`RSTICK`) are implemented; not confirmed on console. |
| Touch screen | ✅ | Front touchscreen drives the game's touch slots. |
| Button remapping / port menu | ✅ | Select + Start options menu (button remap, L+R on-screen controls, screen size, filter, MSAA, FPS counter and limit). [RELEASE.md](RELEASE.md) says it works on console; the Controls table still marks it "not tested" <!-- TODO: reconcile -->. |
| Save data | 🟡 | `data.sav` is written at checkpoints and on return to the menu (console run #9 log). Progress persistence across restarts is not confirmed. |
| Native trophies | ❌ | Not implemented. |
| Languages | ➖ | ❔ <!-- TODO: not documented in the repo --> |
| Performance | 🟡 | About 14.5 fps in mission 1 (console run #10). |
| Online / IAP / ads | ➖ | Online features are disabled: reachability (SystemConfiguration) and socket calls are stubbed (offline). |
| PS TV | ➖ | ❔ <!-- TODO: not tested --> |

Legend: `✅ Working (verified on console) · 🧪 Implemented, not yet verified on console · 🟡 Partial · ❌ Not working · ➖ Not applicable · ❔ Not documented yet`

---

## 🎯 Controls

Physical buttons are translated into touches on the game's own on-screen buttons. The default positions are set in `input.c`; they can be edited in `ux0:data/brothersinarmshourofheroes/controls.txt` (coordinates on the 480×320 game screen) without recompiling.

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
| **Select + Start** | Options menu (see below) | ⚠️ Not tested on console |
| **L + R** | Show/hide the game's on-screen touch controls (they keep working) | ⚠️ Not tested on console |

### Port options (Select + Start)

Hold **Select + Start** in game to open the options menu. The game stays paused behind a translucent panel. **Up/Down** moves between rows, **X** changes the selected row (or assigns a new button), **Left/Right** cycles a setting, **Triangle** clears a button, and **Circle** or **Start** closes the menu and saves.

| Row | Values | Notes |
| :--- | :--- | :--- |
| One row per `controls.txt` action | the button(s) bound to it | **X**, then press the new button. If another action already uses that button, the two actions swap. **Triangle** leaves the action with no button. **Start** cancels the capture. |
| On-screen controls (L+R) | SHOWN / HIDDEN | Same as **L+R** in game. Hidden controls keep responding to touch. |
| Screen size | FULL SCREEN / ORIGINAL ASPECT | Default: full screen (960×544). Original aspect is 816×544 with side bars. |
| Screen filter | BILINEAR (SMOOTH) / SHARP BILINEAR | Default: sharp. The `pixel` filter exists in the code but is only offered when the port is built with `PORT_2D`, which this port is not. |
| Anti-aliasing (MSAA) | OFF / 2X / 4X | Applies after restarting the game. |
| FPS counter | OFF / ON | Top-left corner of the screen. |
| FPS limit | OFF (GAME) / 30 FPS | |

There is no rotation row in this port.

Settings are saved to `ux0:data/brothersinarmshourofheroes/options.txt` (the button bindings go to `controls.txt`). The file can be edited by hand: `filter=bilinear|sharp`, `msaa=0|2|4`, `stretch=0|1` (1 = full screen), `show_fps=0|1`, `fps_cap=0|30`, `hud_hidden=0|1`.

> [!TIP]
> The button positions can be calibrated without rebuilding: edit `controls.txt` over FTP, then restart the game.

---

## ⚙️ Options

`options.txt` is created by the options menu. One `key=value` per line:

| Key | Values | Meaning |
| :--- | :--- | :--- |
| `filter` | `bilinear` / `sharp` | Screen filter. Default: sharp. |
| `msaa` | `0` / `2` / `4` | Anti-aliasing (OFF / 2X / 4X). Applies after restarting the game. |
| `stretch` | `0` / `1` | `1` = full screen (960×544), `0` = original aspect with side bars. |
| `show_fps` | `0` / `1` | FPS counter in the top-left corner. |
| `fps_cap` | `0` / `30` | FPS limit (`0` = game default). |
| `hud_hidden` | `0` / `1` | Hide the on-screen controls (they keep responding to touch). |

---

## 📋 Prerequisites

To run this port on your PS Vita or PS TV, you will need:

1. A PS Vita / PS TV console running Custom Firmware (**Enso 3.60 / 3.65** recommended).
2. [**kubridge**](https://github.com/TheOfficialFloW/kubridge/releases) installed as a kernel plugin (`ur0:tai/config.txt` under `*KERNEL`):
   ```ini
   *KERNEL
   ur0:tai/kubridge.skprx
   ```
3. [**libshacccg.suprx**](https://github.com/Rinnegatamante/ShaRKBR33D/releases/latest) installed in `ur0:data/` (extract it with **ShaRKBR33D**).
4. A legally obtained, **decrypted** copy of **Brothers In Arms: Hour of Heroes 1.3.1** for iOS (`cryptid 0`). Other versions will not match `rebase.bin`.

---

## 📦 Installation

1. **Install the VPK**: install `brothersinarmshourofheroes.vpk` (Title ID `PSVBIAHOH`) with VitaShell. It contains the loader and the LiveArea only.
2. **Game files**: open the decrypted 1.3.1 `.ipa` as a ZIP and extract `Payload/<App>.app/` (the executable is `BIA`). Stage it with the repo's tools (see [Building from Source](#-building-from-source)), which creates `ux0_data/brothersinarmshourofheroes/` with `game_app/` and `rebase.bin`.
3. **Music**: `tools/stage_data.sh` converts the background music (`.m4a` → PCM WAV) with `tools/convert_audio.sh`, which needs `ffmpeg` (or macOS `afconvert` as a fallback).
4. **Copy to the Vita**: copy `ux0_data/brothersinarmshourofheroes/` to `ux0:data/brothersinarmshourofheroes/` (`game_app/`, `rebase.bin`). `controls.txt`, `options.txt`, `saves/` and `logs/` are created automatically.
5. **Options**: hold **Select + Start** in game to change button bindings, on-screen controls, screen size, filter, MSAA, FPS counter and FPS limit.

### Final File Structure in `ux0:data/brothersinarmshourofheroes/`

```text
ux0:data/brothersinarmshourofheroes/
├── game_app/        <- The contents of the .app folder (BIA executable, res/, sounds.bar...), renamed to game_app
├── rebase.bin       <- Rebase table generated from the executable
├── controls.txt     <- (optional) Button positions, see Controls
├── options.txt      <- Created automatically by the options menu
├── saves/           <- Created automatically: your save games
└── logs/            <- Created automatically: one log per run
```

---

## 🐛 Known Issues

- **Tilt axes**: the iOS-to-Vita axis mapping for the accelerometer was derived from the code, not yet checked on hardware. It is used by the grenade tutorial.
- **Foliage textures** were corrupted before the RGBA5551 fix; re-check on a new console run if they reappear.
- **Frame rate**: about 14.5 fps in mission 1 (console run #10).
- **Not verified on console yet**: missions 2 and onward, sound effects, and the stick rows of the control table.
- **Trophies**: not implemented.
- **Online features disabled**: reachability (SystemConfiguration) and socket calls are stubbed (offline play).
- **Crash dumps** are written by the system to `ux0:data/` (not to the game folder). Pull them, with the logs, using `python3 -I tools/deploy_data.py --logs`.

---

## 🔨 Building from Source

### Prerequisites

- [VitaSDK](https://vitasdk.org/) (`$VITASDK`, softfp) with `vitaGL` (vendored, built with `SOFTFP_ABI=1`), `vitashark`, `openal-soft` and `pthread`.
- CMake 3.14+, Python 3 (for the tools), `ffmpeg` (for the music conversion).
- The decrypted `.app` folder, linked as `game_app` in the repo root (never committed).

### Compilation Commands

```bash
git clone https://github.com/MetalSyntax/Brothers-In-Arms-Hour-of-Heroes-vita.git

cd Brothers-In-Arms-Hour-of-Heroes-vita
ln -s /path/to/Payload/<App>.app game_app

# Stage the console data: copies the .app, converts the music, generates rebase.bin
tools/stage_data.sh

# Build eboot.bin / brothersinarmshourofheroes.vpk
cmake -S . -B build
cmake --build build
```

### Deploy & Debug

```bash
python3 -I tools/deploy_data.py          # incremental upload of ux0_data/brothersinarmshourofheroes/
python3 -I tools/deploy_data.py --logs   # fetch logs and crash dumps into ./logs/
```

A crash PC in `0x98xxxxxx` minus `0x98000000` is the address in the Mach-O: look it up with `python3 -I tools/annotate.py`.
Architecture and progress notes: [`PORTING_PLAN.md`](PORTING_PLAN.md) and [`port_progress.md`](port_progress.md).

---

## 📁 Project Structure

- `source/`: native loader, runtime and the Vita-side implementations (`input.c`, `gles.c`, and the rest of the iOS framework layer).
- `tools/`: `macho_prep.py` (rebase table), `stage_data.sh` (staging), `convert_audio.sh` (music), `annotate.py` (crash address lookup), `deploy_data.py` (FTP upload and log pull), `cgbi_check.py`, `printf_check.py`, `imports_table.py`, `make_livearea.py`.
- `extras/livearea/`: LiveArea assets (`pic0.png` is the banner used above).
- `docs/`: `imports.md` and `macho_report.txt`.
- `PORTING_PLAN.md`: living port plan. `port_progress.md`: per-run progress log. `RELEASE.md`: release notes.

---

## 📜 Disclaimer

**Brothers In Arms** is a registered trademark of **Gearbox Software**. The iOS version was developed and published by **Gameloft**. The work presented in this repository is unofficial and is not produced, endorsed, or sanctioned by the rights holders or any affiliated entities.

This software does NOT contain original game code, binaries, copyrighted graphics, or game assets. The authors do not promote or condone piracy in any way. To play, you must provide your own legally obtained, **decrypted** copy of the iOS `.ipa`.

---

## ☕ Support My Work

This project is **free** and will stay free. If you enjoy it, a tip from **$1** helps me keep porting games to the PS Vita:

<p align="center">
  <a href="https://ko-fi.com/metalsyntax"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support me on Ko-fi"></a>
</p>

- **Ko-fi:** [ko-fi.com/metalsyntax](https://ko-fi.com/metalsyntax)
- **PayPal:** [paypal.me/MetalSyntax](https://paypal.me/MetalSyntax)
- **Bitcoin (BTC):** `133HdCsaWxdjTfmwpgGVykByhNBV338uRc`

**Ko-fi supporters get:**
- **Early access** to new ports before their public release (test builds of upcoming projects).
- **Data preparation scripts and step-by-step guides** to set up your own game files for each new port.
- A say in which games I port next.

> Game files are never distributed. You always need your own legally obtained copy of the game.

---

## 👥 Credits

- **Brothers In Arms: Hour of Heroes** is intellectual property of Gearbox Software, and the iOS version was developed by **Gameloft**.
- **vitaGL** graphics library by [Rinnegatamante](https://github.com/Rinnegatamante/vitaGL).
- **kubridge** plugin by [Bythos](https://github.com/bythos14/kubridge).
- **openal-soft** project.
- The **iOS Mach-O loader** is based on [`ios-loader-boilerplate`](https://github.com/MetalSyntax/ios-loader-boilerplate) and [`Inotia2-vita`](https://github.com/MetalSyntax/Inotia2-vita).
- Special thanks to the PS Vita homebrew and porting community.

---

## 📄 License

<!-- TODO: this repository has no LICENSE file. Add one or state the license here. -->
