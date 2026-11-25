<h1 align="center">
  Brothers In Arms: Hour of Heroes · PS Vita Port
</h1>

<p align="center">
  <a href="#-status--playability">Status & Playability</a> •
  <a href="#setup-instructions-for-players">How to install</a> •
  <a href="#controls">Controls</a> •
  <a href="#known-issues">Known Issues</a> •
  <a href="#build-instructions-for-developers">How to compile</a> •
  <a href="#credits--acknowledgments">Credits</a>
</p>

An unofficial, native **iOS-to-PS Vita wrapper port** of *Brothers In Arms: Hour of Heroes* (iOS v1.3.1, developed by **Gameloft**). There is no `.so` here: this port ships its own **Mach-O loader** that maps the official armv6 iOS executable (`BIA`, from the 1.3.1 IPA) into PS Vita RAM and replaces the Apple frameworks it uses (Foundation, UIKit, CoreFoundation, CoreGraphics, AudioToolbox, OpenAL, OpenGL ES) with native implementations on top of **vitaGL**, **openal-soft** and the Vita system libraries.

---

## 🎮 Status & Playability

**Status: BETA v0.1 — PLAYABLE, mission 1**

On physical PlayStation Vita hardware the game boots to the menu and mission 1 runs from the start to the grenade tutorial at about 14.5 fps. A later commit reports mission 1 completed on the Vita; the remaining missions are not confirmed yet.

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

---

## Disclaimer

**Brothers In Arms** is a registered trademark of **Gearbox Software**. The iOS version was developed and published by **Gameloft**. The work presented in this repository is unofficial and is not produced, endorsed, or sanctioned by the rights holders or any affiliated entities.

This software does NOT contain original game code, binaries, copyrighted graphics, or game assets. The authors do not promote or condone piracy in any way. To play, you must provide your own legally obtained, **decrypted** copy of the iOS `.ipa`.

---

## Setup Instructions (For Players)

1. **CFW & Plugins**: PS Vita on Custom Firmware (**Enso 3.60 / 3.65** recommended) with `kubridge` under `*KERNEL` in `ur0:tai/config.txt`:
   ```ini
   *KERNEL
   ur0:tai/kubridge.skprx
   ```
2. **Shader Compiler**: `libshacccg.suprx` must be in `ur0:data/` (extract it with **ShaRKBR33D**).
3. **Game Files**: a decrypted **Brothers In Arms: Hour of Heroes 1.3.1** iOS `.ipa` (FairPlay `cryptid 0`). Open it as a ZIP and extract `Payload/<App>.app/` (the executable is `BIA`). Other versions will not match `rebase.bin`.
4. **Music**: `tools/stage_data.sh` converts the background music (`.m4a` → PCM WAV) with `tools/convert_audio.sh`, which needs `ffmpeg` (or macOS `afconvert` as a fallback).
5. **Copy to the Vita** (`ux0:data/brothersinarmshourofheroes/`):
   ```
   ux0:data/brothersinarmshourofheroes/
     game_app/      the contents of the .app folder (BIA executable, res/, sounds.bar...), renamed to game_app
     rebase.bin     rebase table generated from the executable
     controls.txt   optional — button positions (see Controls)
     options.txt    created automatically by the options menu
     saves/         created automatically — your save games
     logs/          created automatically — one log per run
   ```
6. **Install VPK**: install `brothersinarmshourofheroes.vpk` (Title ID `PSVBIAHOH`) with VitaShell.

---

## Controls

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

## Known Issues

- **Tilt axes**: the iOS-to-Vita axis mapping for the accelerometer was derived from the code, not yet checked on hardware.
- **Foliage textures** were corrupted before the RGBA5551 fix; re-check on a new console run if they reappear.
- **Frame rate**: about 14.5 fps in mission 1 (console run #10).
- **Online features disabled**: reachability (SystemConfiguration) and socket calls are stubbed (offline).
- **Crash dumps** are written by the system to `ux0:data/` (not to the game folder).

---

## Build Instructions (For Developers)

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

A crash PC in `0x98xxxxxx` minus `0x98000000` is the address in the Mach-O: look it up with `python3 -I tools/annotate.py`. Architecture and progress notes: [`PORTING_PLAN.md`](PORTING_PLAN.md) and [`port_progress.md`](port_progress.md).

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

## Credits & Acknowledgments

- **Brothers In Arms: Hour of Heroes** is intellectual property of Gearbox Software, and the iOS version was developed by **Gameloft**.
- **vitaGL** graphics library by [Rinnegatamante](https://github.com/Rinnegatamante/vitaGL).
- **kubridge** plugin by [Bythos](https://github.com/bythos14/kubridge).
- **openal-soft** project.
- The **iOS Mach-O loader** is based on [`ios-loader-boilerplate`](https://github.com/MetalSyntax/ios-loader-boilerplate) and [`Inotia2-vita`](https://github.com/MetalSyntax/Inotia2-vita).
- Special thanks to the PS Vita homebrew and porting community.
