# Brothers In Arms Hour of Heroes — Port Progress (iOS Mach-O)

> Phases section kept identical to `PORTING_PLAN.md`. Console run #1 checklist and session log below it.

## Phases

Each phase: do the steps in order, build (`psvita-toolkit build`), deploy (`psvita-toolkit deploy`),
run on hardware, read the newest `logs/brothersinarmshourofheroes_NNN.log`, and only then tick the item.

**Legend**
- `[x]` = done **and verified at the desk** (build output, hosttest run, script output, disassembly). The evidence
  (file, command, log excerpt) is written on the item.
- `[~]` = implemented, **pending verification on the real Vita**. The item says exactly which log line / behaviour
  to check on console run #1+.
- No item is left `[ ]`. Hosttest = the same loader built for Linux ARM32 softfp in Docker (`hosttest/run.sh`,
  method in `/Volumes/TOSHIBA EXT/PSVITA Develop/Docs/Prueba-ARM32-sin-consola.md`): it proves logic/memory bugs, not
  vitaGL/GXM, SceAvcdec, audio hardware or performance.

### Phase 1: Repo and tools
- [x] IPA extracted, `game_app` symlink, ios-loader-boilerplate merged, CMake/`hle.h` adapted to `brothersinarmshourofheroes`.
- [x] Executable decrypted (cryptid 0).
- [x] `rebase.bin` + `docs/macho_report.txt` generated (`python3 -I tools/macho_prep.py game_app/BIA ux0_data/brothersinarmshourofheroes/rebase.bin --report docs/macho_report.txt`).
- [x] (new) `tools/macho_prep.py` replaced by Hero of Sparta II's version: BIA is **ARM-mode, non-PIC** code with absolute
  addresses in `__text` literal pools and 129 `ldrls pc,[pc,rX,lsl#2]` switch tables. Rebase grew 5559 -> 12964 words
  (`__text_lit` 7 kinds, `__text_jumptable` 2018 entries; `docs/macho_report.txt`). Extra fix found by hosttest:
  2-byte-aligned literals into `__TEXT,__const` (Level::InitAnimTextures -> TTextureParam* `0x1659f6`) are rebased too.
- [x] (new) `tools/annotate.py` fixed for the 7-value `parse()` of the new macho_prep (PoP fix).
- [x] (new) `.gitignore`: `Makefile` -> `/Makefile` (it excluded vendor/vitaGL/Makefile, so `psvita-toolkit build`, which
  copies the tree respecting .gitignore, failed with "no makefile found"); added `/decompiled/`, `vendor/**/*.o|*.a`,
  `.claude/`, artwork, `hosttest/out/`.

### Phase 2: Decompilation and inventory
- [x] `decompiled/macho/` dumps (frameworks, ObjC classes, symbols, imports) -- section 2.
- [x] Ghidra pseudo-C of `BIA`: **not needed** -- the binary is not stripped (2617 local `t` symbols incl. C++ methods and
  every `-[Class sel]`), so triage uses `otool -tV` + `tools/annotate.py` + symbols. Decision recorded; run Ghidra
  (`psvita-toolkit decompile`) only if a crash lands in a function whose disassembly is not enough.
- [x] Classify `imports.txt`: libc/math/pthread -> `darwin.c`, C++ ABI -> `cxx_imports.c`/`darwin.c`, GL -> `gles.c`,
  ObjC/CF/CG/UIKit -> `objc.c`, AudioToolbox -> `coreaudio.c`, OpenAL -> `openal.c`+`coreaudio.c`,
  SystemConfiguration/sockets/CC_MD5 -> stubs ("offline / not reachable").
- [x] Table in `docs/imports.md` (generated: `python3 -I tools/imports_table.py --check` -> "313 imports: 268 real, 45 stub,
  0 unimplemented", exit 0).

### Phase 3: Real engine analysis
- [x] Engine (symbol evidence): **Gameloft in-house C++ engine**, not cocos2d/Unity/SIO2. `cGame` (59 methods),
  `CLib3D`/`cLib3DOGL`, `CMesh::RenderVbo`, `GTextureManager`, `Level`, `CSoldierHERO`, `SoundManager`/`CSoundWrap`,
  `RecordStore` (J2ME heritage), `IGP*` (store promo), `CNetworkManager`; source path in
  `__GLOBAL__I__Volumes_bia_iphone_sdk3_src__iphone_bia3d_iphone_.._.._BIA_Lib3D_render.cpp`. Same `CTouchPad` input layer
  as Skater Nation (another Gameloft port on this disk).
- [x] Lifecycle (disassembly): `UIApplicationMain` -> `-[AppController applicationDidFinishLaunching:]` (0x15b060):
  `initDisplay` (UIWindow + `EAGLView initWithFrame:` 320x480), accelerometer 1/30 s, `[[UIDevice currentDevice]
  systemVersion] cString`, `GetVersionNumber`, `LoadMovie("bia_intro.mp4")`, `StartThread()` -> `pthread_create(ThreadRoutine
  0x1252a4)`. The **game thread** runs `GameStart` then loops `CFRunLoopRunInMode` / `cGame::TimerCallback` /
  `presentRenderbuffer:` / `usleep`. No NSTimer/CADisplayLink drives frames.
- [x] Resources: `ChangeCurrentDirToApplicationDir` (0x124ac0) takes `CFBundleCopyBundleURL` -> `CFURLCopyFileSystemPath`,
  strips the last path component (app home) and the game opens `<home>/BIA.app/*.bar` and `<home>/Documents/data.sav`,
  `achievements.bin` with `fopen` (strings `/BIA.app/pack%02d.bar`, `/Documents/data.sav`). `darwin_path()` maps
  `/BIA.app/` -> `game_app/` and `/Documents/` -> `saves/`. Hosttest log: `fopen(.../saves/data.sav ...) -> NULL (not found)`
  on first boot, then the files are written.
- [x] Middleware: OpenAL + AudioToolbox (Apple "SoundEngine" sample: `SoundEngine_*`, `BackgroundTrackMgr`,
  `SoundEngineEffect`, `SoundDataParser`) -> implemented; PowerVR SDK texture loader (`PVRTTextureLoadFromPointer`) uses
  OES paletted textures 0x8B94/0x8B99 (seen in hosttest) -> vitaGL supports them (`textures.c` GL_PALETTE*_OES);
  LZMA archives (`LZMAFile`) are game code; IGP/Reachability/DemoDownload/sockets = stub (offline). No zlib/libpng/
  Flurry/StoreKit/GameKit linked.

### Phase 4: Loader and ObjC bootstrap (GATE: log reaches the app delegate)
- [x] `macho_load()` maps `__TEXT`/`__DATA` at `0x98000000`, applies `rebase.bin`, binds `__nl_symbol_ptr`/`__la_symbol_ptr`.
  Hosttest log: `Applied 493 / 493 external relocations.` / `Rebase complete: 12808 words relocated.` /
  `Mach-O loaded successfully. Thunks used: 206 / 8192`.
- [x] Every import resolves to a shim (no `ml_unimpl` fallback needed): `tools/imports_table.py --check` exit 0; hosttest
  logs contain no `unimpl import:` line.
- [x] `macho_run_inits()` runs the 115 `__mod_init_func` constructors. Hosttest found and fixed a real ABI bug:
  `gettimeofday()` into the guest's 8-byte Darwin `timeval` (Profiler ctor @0x49fc) -> `darwin.c` now converts
  `timeval`/`time_t` to 32-bit explicitly (`d_gettimeofday`, `d_time`, `d_localtime`, `d_mktime`).
- [x] ObjC: guest classes registered from `__objc_classlist` (17 classes, hosttest log `Registered guest class AppController
  (size 44, 16 methods)`), alloc/init/new/retain/release, NSString (ring), NSBundle, NSUserDefaults `AppleLanguages`,
  CFArray, CFURL, NSString-shaped constants (`NSLocaleCountryCode` was a bare `char*`: hosttest crash in `objectForKey:`).
- [x] Delegate called: hosttest log `[BOOT] Calling -[AppController applicationDidFinishLaunching:] at 0x9815B060` ...
  `[BOOT] delegate ok`, then the game thread prints ` THREAD START ` / `GAME_START` / `InitAppData Done!`.
- [~] Same gate on the Vita: check `[BOOT] delegate ok` and `pthread_create(0x001252A4) -> 0` in log 001.

### Phase 5: Graphics (GATE: first frame on screen)
- [x] vitaGL init at 960x544, SoftFP build (`SOFTFP_ABI=1`, plus PoP's `SOFTFP_LIBS=1` detection for the Mac's
  vitasdk-softfp and Skater's `NO_TILE_CLIPPER=1`); EAGLContext/CAEAGLLayer/`renderbufferStorage:fromView:` shims backed
  by the 320x480 FBO (`gles.c`). `psvita-toolkit build` -> `[+] Build OK`, no warnings.
- [x] Orientation: the game keeps `CTouchPad::s_currentDeviceOrientation` = 4 (initial `__DATA` value; nothing sends
  accelerometer events) and rotates its own rendering; `gles_present` blits the portrait FBO rotated (uv table `uv4`,
  `uv2` = 180 degrees if the game ever switches to 2), fullscreen stretch 480x320 -> 960x544. Hosttest log
  `[GL] first frame presented (orientation 4)`.
- [x] `presentRenderbuffer:` -> `gles_present()` -> `vglSwapBuffers`, driven by the game thread itself (ThreadRoutine).
  The blit saves/neutralises/restores fog, lighting, cull, stencil, texture unit 1, texture matrix, VBO binding (Skater
  Nation's present, same engine).
- [~] Textures: OES paletted (0x8B94/0x8B99) + RGBA via vitaGL; CgBI PNG decoder kept (`cgbi.c`) but the game loads
  `.bar`/LZMA packs, not PNGs. Hosttest decodes the palettes in software and the game reports `Loaded Sprite 0..125`
  (23 sprites), but **the hosttest image stays black**: `HOSTTEST_GLTRACE` shows the game drawing indexed quads into
  the FBO with sane state (ortho rotated 90 degrees, LEQUAL, SRC_ALPHA blend, MODULATE) and the `[GL] perf ... fbo
  centre 000000` probe reads black. Mesa/llvmpipe was not resolved (depth clamp / no depth test made no
  difference), so the visual result is a console check: menu art visible, `fbo centre` not `000000`.
- [x] GLES2 shaders: N/A -- the game is GLES 1.1 fixed function (`glTexEnv*`, `glFog*`, no `glCreateShader` import).
- [~] First frame on the Vita: check `[GL] first frame presented (orientation 4)` and the image not mirrored/upside
  down. If upside down: swap `uv4`/`uv2` in `gles_present`; if mirrored, compare with Skater Nation's `uv3` table.

### Phase 6: Touch
- [x] Front touch -> `CTouchPad::EventTouchDown/Move/Up(*s_touchPadWrite, slot, x, y)` in 480x320 landscape
  (`input.c`), the same calls `-[EAGLView touchesChangedWithEvent:]` makes after `TransformCoordinatesByOrientation`
  (0x15c618 disassembly). No UITouch/NSSet emulation needed. Hosttest log `[INPUT] CTouchPad bound (s_touchPadWrite at
  0x98170b94)`.
- [x] Multitouch: 2 finger slots (0-1) + 3 synthetic slots (sticks, button); CTouchPad has 5 slots (`getTouchIndex:`).
- [x] Rear panel: not sampled (disabled) -- nothing in the game needs it.
- [~] On console: tap menu items with the finger; they must react where drawn (fullscreen stretch, `t.report * 480/1920`).

### Phase 7: Physical controls
- [x] Default mapping = synthesized taps on the HUD rectangles the game itself registers (`Level::InitTouchElements`,
  confirmed at runtime by the new `[INPUT] element ... rect` log): left stick/D-pad -> move joystick (75,245 r33),
  right stick -> look drag from (240,140), R -> right stick/fire (413,261), CROSS (375,290), SQUARE (445,290), TRIANGLE
  (435,275), CIRCLE (445,165), L (30,145), START (453,27), SELECT (174,16).
- [x] Analog deadzone 10 (of 127) and look speed 6 px/poll defaults.
- [x] `controls.txt` in `ux0:data/brothersinarmshourofheroes/` (`BUTTON = x,y`, `LSTICK = x,y,r`, `RSTICK = x,y,speed`,
  `DEADZONE = n`); missing file -> defaults written. Hosttest: first run `controls.txt missing: defaults written`, second
  run `controls.txt: 15 entries (deadzone 10)`.
- [~] Which HUD button does what (fire/crouch/grenade/reload/pause) is not knowable at the desk: play level 1 and
  remap in `controls.txt` using the `[INPUT] element` lines (13 elements listed when a level loads).

### Phase 8: Audio
- [x] Backends identified (disassembly): SFX = `SoundEngineEffect::AttachFilesToSource` -> `AudioFileStreamOpen/ParseBytes`
  (WAV in `sounds.bar`) -> `alcGetProcAddress("alBufferDataStatic")` -> OpenAL; BGM = `BackgroundTrackMgr` AudioFile +
  AudioQueue (AAC). `coreaudio.c`: AudioFileStream WAV parser (props `dfmt`/`bcnt`/`doff`/`redy`, packets callback via
  `call_guest6`), AudioFile/ReadPackets over WAV, `alBufferDataStatic` -> `alBufferData`, `alGetError` iOS semantics.
  BGM: `SoundEngine_*BackgroundMusic*` hooked (`patch.c`) onto a WAV streamer thread (sceAudioOut MAIN port; openal-soft
  owns the BGM port -- Skater Nation run 23).
- [x] Formats inventoried: 215 SFX WAVs in `sounds.bar` = PCM s16 (112x mono 16 kHz, 102x mono 22.05 kHz, 1x stereo
  22.05 kHz) -> no conversion. 20 music tracks `m_*.m4a` = AAC-LC (18x 44.1 kHz, 2x 22.05 kHz, ~700 s total) -> offline
  `tools/convert_audio.sh` (ffmpeg, afconvert fallback) to WAV s16 stereo 44.1 kHz, called by `tools/stage_data.sh`
  (`[+] 20 music tracks -> ... (WAV s16 stereo 44.1 kHz)`; `wave` check: 44100 Hz, 2 ch, 16-bit).
- [x] Hosttest: `[AUDIO] alcGetProcAddress(alBufferDataStatic) -> 0x98211ce0`, `Total Sound Raw Memory Size: 12533674`,
  `[AUDIO] BGM loaded .../m_title.wav (44100 Hz, 2 ch, 10334208 bytes)`, `[AUDIO] BGM port 0 open`, `BGM start (loop=1, ok)`.
  Fix found: the game opens device "DirectSound3D", unknown to openal-soft -> SFX ran on a dummy device; now
  `alcOpenDevice(NULL)`.
- [~] On console: hear menu music (m_title) and button SFX; check `[AL] alcOpenDevice` is NOT followed by `using dummy
  handle`. No crackle/drift; music pauses during the intro video (bgm_pause around PlayMovie).
- [~] Pause on suspend / volume: `SoundEngine_SetBackgroundMusicVolume` -> software gain; master volume
  (`SoundEngine_SetMasterVolume`) only reaches SFX (BGM manager is bypassed) -- check the options menu sliders.

### Phase 9: Videos / FMV
- [x] Inventory: 1 video, `bia_intro.mp4` (H.264 Constrained Baseline 480x320 + AAC-LC 44.1 kHz stereo, 36.4 s;
  ffprobe, also checked by `stage_data.sh`: `[+] video bia_intro.mp4: h264 ok`). Played by `video_avc.c` (Zombie
  Infection / PoP: MP4 demux + SceAvcdec H.264 + FFmpeg AAC, letterboxed).
- [x] Hooks (`patch.c`): `LoadMovie`, `PlayMovie` (plays synchronously on the game thread, as `cGame::update3D` then polls
  `IsMoviePlaying`/`IsMoviePaused` and calls `StopMovie`), `IsMoviePlaying`/`IsMoviePaused` -> 0, Stop/Suspend/Resume -> noop.
  `MPMoviePlayerPlaybackDidFinishNotification` is never needed (MoviePlayer class bypassed).
- [x] Graceful skip: hosttest has no SceAvcdec -> `[VIDEO] bia_intro.mp4 finished (missing/undecodable: skipped)` and the
  game prints `Play movie end` and continues to `_state = 8/17/18`.
- [~] On console: intro plays with sound; CROSS/CIRCLE/START skips (`video: cutscene skipped by user`); after it the menu
  draws correctly (gles_reapply_arrays restores the guest vertex arrays).

### Phase 10: Assets, data, language and saves
- [x] `Documents` -> `saves/`, bundle -> `game_app/` (`darwin_path`); `NSHomeDirectory`/`NSSearchPathForDirectoriesInDomains`
  -> `saves/`. Hosttest: `data.sav`/`achievements.bin` probed, then written into `saves/`.
- [x] Boot checker (`main.c`): missing `game_app/BIA` or `rebase.bin` -> sceMsgDialog with the exact path, then exit;
  missing converted music -> warning only. Hosttest prints the dialog text when data is absent.
- [x] Language: `GetDeviceLanguage` reads `[[NSUserDefaults standardUserDefaults] objectForKey:@"AppleLanguages"][0]`;
  `cGame::GetOwnerLanguage` compares "en","de","fr","it","es","ja". `objc.c` answers from
  `SCE_SYSTEM_PARAM_ID_LANG` (ja/fr/es/de/it/ko, else en).
- [~] Saves survive restart and suspend: finish a mission, quit, relaunch -> progress kept (`saves/data.sav` size > 0).

### Phase 11: Performance
- [x] 444/222/222/166 clocks at boot (`main.c`).
- [~] Frame time / memory / long session: measure on console (`psvita-toolkit perf-telemetry`, `mem-profile`). Hosttest
  cannot (qemu). Watch the 12.5 MB SFX + streaming BGM + 33 MB `sprites_tex.bar` against the 192 MB newlib heap.

### Phase 12: Configurator and options
- [x] Persistent settings: `controls.txt` (mapping, sticks, deadzone, look speed) written with defaults on first boot.
  FPS lock / filter / touch-overlay options deliberately not added (the game paces itself with `usleep`; GL_LINEAR blit).
- [~] Game's own options (sound volume, sensitivity) persist in `saves/` through RecordStore: change one, relaunch, check.

### Phase 13: Trophies
- [x] No GameKit linked (`frameworks.txt`); the game has its own `CAchievements` (`achievements.bin`, 31 symbols).
  Trophies skipped for v1 (no trophy pack); a later short list could hook `Level::setAchievementMessage(int)`.

### Phase 14: LiveArea and VPK
- [x] LiveArea from the IPA's iTunesArtwork (`tools/make_livearea.py`): icon0 128x128, bg0 840x500 (64 colours, 113 KB),
  startup 280x158, pic0 960x544, all 8-bit indexed -- `psvita-toolkit livearea --validate` -> all OK.
- [x] `param.sfo` with Title ID `PSVBIAHOH` (build log `Generating param.sfo`); VPK `build/brothersinarmshourofheroes.vpk`.
- [~] VPK installs without warnings in VitaShell.

### Phase 15: Real-hardware testing and release
- [~] Full playthrough without crashes; suspend/resume OK (`applicationWillResignActive:` is not wired to the PS button
  yet: check that resuming from LiveArea keeps audio/video alive); Vita 1000/2000/PSTV status stated.
- [~] Production build: `psvita-toolkit build` builds Debug (`-DDEBUG_LOADER`, `[DEBUG]` lines); use a Release build
  (`cmake -DCMAKE_BUILD_TYPE=Release`) once stable.
- [~] README: state IPA 1.3.1 tested and that a **decrypted** IPA is required (README not rewritten in this session).

### Hosttest (desk verification, ARM32 softfp Docker)
- [x] `hosttest/` adapted from Skater Nation: `video_fake.c` (no SceAvcdec -> tests the skip path), `gl_thread.c`
  (`--wrap=pthread_create` hands the EGL context to the game thread), paletted-texture decoder, `mallopt` so the heap
  sits above 0x80000000 like newlib (objc.c dispatch depends on it), crash handler with r7 frame chain,
  sceMsgDialog fakes, no `_TIME_BITS=64`.
- [x] Fixes it found in the loader (all would have crashed or hung on the Vita or hidden bugs): halfword `__const`
  literals rebase, 32-bit `timeval`/`time_t`, `chdir` no-op, NSString constants, `ns_cstr` NULL-safe, `+new`,
  `alcOpenDevice(NULL)`, `darwin_vformat` room cap (glibc EOVERFLOW truncated `pack%02d.bar`). Defensive (not proven
  necessary): depth/stencil cleared once in `gles_init`.
- [x] Boot checker exercised: with `rebase.bin` hidden the run prints `[HOSTTEST] sceMsgDialog: Relocation table not
  found:` / `[BOOT] Relocation table not found:` and exits 0.
- [x] Furthest point (final run, 300 s, `hosttest/run.sh run 300`, no crash, exit 124 = timeout): log
  `Applied 493 / 493 external relocations.` -> `Rebase complete: 12808 words relocated.` -> `[BOOT] delegate ok` ->
  game thread `GAME_START` / `InitAppData Done!` / `== OpenGLInit OK` -> `[GL] first frame presented (orientation 4)` ->
  states 67, 68 (save probe, `saves/data.sav` created), 16 (logos, sprites), 2/3 (`Do Load Game`,
  `Level::InitTouchElements`: 13 `[INPUT] element` lines), 8 (`[VIDEO] PlayMovie` -> skipped, game prints
  `Play movie end`), 17/18 (`Total Sound Raw Memory Size: 12533674`, `[AUDIO] alcGetProcAddress(alBufferDataStatic)`),
  BGM `m_title.wav` loaded + `BGM start (loop=1, ok)`, then state 15 looping at ~7-8 fps under qemu
  (`[GL] perf: 7.3 fps`). 0 `unimpl import` lines, 0 `objc_msgSend(...) -> nil` lines in the final log.
- [~] What hosttest cannot prove: vitaGL/GXM output, SceAvcdec decode, real audio output, input feel, speed.

## Console run #1 checklist

Build and stage (desk, already verified 2026-11-04):

```sh
cd "/Volumes/TOSHIBA EXT/PSVITA Develop/Brothers-In-Arms-Hour-of-Heroes-vita"
psvita-toolkit build                      # -> build/brothersinarmshourofheroes.vpk
sh tools/stage_data.sh                    # -> ux0_data/brothersinarmshourofheroes/{game_app,rebase.bin,saves,logs}
                                          #    (music converted to m_*.wav, *.m4a not copied, ~256 MB)
```

Deploy (VitaShell FTP open on the Vita, IP in `.psvita-toolkit.json`):

```sh
psvita-toolkit deploy                     # installs/updates the VPK
python3 -I tools/deploy_data.py           # uploads ux0_data/brothersinarmshourofheroes/ (never touches saves/)
# Needed once on the console: ur0:data/libshacccg.suprx (ShaRKBR33D) and kubridge.skprx in ur0:tai.
```

Run, then pull the log: `python3 -I tools/deploy_data.py --logs` -> `logs/brothersinarmshourofheroes_NNN.log`.

| Subsystem | Log line that proves it | If it is missing / fails |
|---|---|---|
| Loader | `Applied 493 / 493 external relocations.`, `Rebase complete: 128xx words relocated.`, `Thunks used: 206` | Boot dialog names the missing file; else dump -> `psvita-toolkit analyze`, PC-0x98000000 in `symbols.txt` |
| Imports | no `unimpl import:` lines | add the shim to the file `docs/imports.md` points to, re-run `tools/imports_table.py --check` |
| Constructors | `Executing 115 global constructors` then `Jumping to guest entry point` | crash inside a ctor: Release build hides `[MACHO] ctor N`; use the Debug build (toolkit default) to see the index |
| ObjC / delegate | `[BOOT] Calling -[AppController applicationDidFinishLaunching:]` ... `[BOOT] delegate ok` | `objc_msgSend(..., sel) -> nil` lines right before = selector to implement in `objc.c` |
| Game thread | `pthread_create(0x001252A4) -> 0`; game printf ` THREAD START `, `GAME_START`, `InitAppData Done!` | hang here = file not found: grep `fopen(` WARN lines (pack09-16, CTexturesPack08 are normal probes) |
| Input | `[INPUT] CTouchPad bound`, `[INPUT] element ... rect` (13 when a level loads) | no reaction to touch: check the elements' rects vs. where they are drawn |
| GL | `vitaGL: FBO 320x480 -> screen 960x544`, `[GL] first frame presented (orientation 4)` | black screen with frames presented: textures (look for many `glGetError ... swallowed`); upside down/mirrored: swap `uv4`/`uv2` in `gles_present` |
| Audio SFX | `[AL] alcOpenDevice(DirectSound3D)` **without** `using dummy handle`; `[AUDIO] alcGetProcAddress(alBufferDataStatic) -> 0x...` | dummy handle: openal-soft init failed (ports busy?) -> try `SCE_AUDIO_OUT_PORT_TYPE_MAIN` conflicts with BGM thread |
| Audio BGM | `[AUDIO] BGM port N open (44100 Hz stereo, grain 1024)`, `[AUDIO] BGM loaded .../m_title.wav`, `BGM start` | `BGM load ... failed`: m_*.wav not on the console -> re-run stage_data.sh + deploy_data.py |
| Video | `[VIDEO] PlayMovie(.../bia_intro.mp4)`, `video: 480x320 ...`, then `[VIDEO] bia_intro.mp4 finished (played)` | `finished (missing/undecodable: skipped)` = sceAvcdec error logged just above; the game must still reach the menu |
| Saves | `saves/data.sav` and `saves/achievements.bin` exist after the first menu | `fopen(... wb) -> NULL`: `ux0:data/brothersinarmshourofheroes/saves/` permissions |

Likely failures and first action:
1. **Crash in game code** (PC 0x98xxxxxx): an absolute pointer the rebase missed (hosttest found two classes of these).
   Look at the faulting address: if it is in `0x1000..0x211000` (unslid), find the literal with `otool -tV` and extend
   `tools/macho_prep.py`, re-run `stage_data.sh`.
2. **Black screen but frames presented**: texture upload (paletted/PVRTC path in vitaGL) or blit orientation.
3. **Silence**: compare the `[AL]` and `[AUDIO]` lines above; music needs the converted `.wav` files on the console.

## Session 2026-11-04 (desk work, no console)

- Donor choice: Prince of Persia WW (same `AppController`/`EAGLView`/`MoviePlayer`/`ThreadRoutine`/`LoadMovie`
  lifecycle, best import coverage 240/313) for the loader tree; Hero of Sparta II `macho_prep.py` (ARM literal pools +
  switch tables, needed because BIA is ARM-mode non-PIC); Blades of Fury SoundEngine audio idea; Skater Nation present /
  orientation (same `CTouchPad` engine) and `hosttest/`; Zombie Infection/PoP `video_avc.c`.
- Source tree: PoP's loader + new `coreaudio.c` (AudioFileStream/AudioFile WAV, BGM streamer), new `patch.c` (BGM,
  movie, touch-element hooks), new `input.c` (direct CTouchPad injection + `controls.txt`), `objc.c`/`darwin.c`/`gles.c`
  extended for BIA's 313 imports (0 unimplemented), PoP HUD hacks removed, hooks now enter through `ml_align_call`.
- Tools: `tools/imports_table.py` (generates `docs/imports.md`, `--check`), `tools/convert_audio.sh`,
  `tools/make_livearea.py`, `stage_data.sh` stages `game_app/` + converted music + video check; `deploy_data.py --logs`
  now fetches `<slug>_NNN.log`.
- Build: `psvita-toolkit build` -> `[+] Build OK. VPK: build/brothersinarmshourofheroes.vpk` (3.1 MB), zero warnings.
- Hosttest (Docker ARM32 softfp): ~15 runs, 8 loader bugs fixed (list in the Hosttest phase). Furthest point: boot ->
  delegate -> game thread -> logos -> level/HUD init -> intro-video skip path -> SFX bank + BGM start -> game state 15
  for 300 s without a crash; image black under Mesa (unresolved, console check).
- Not done (needs hardware): everything marked `[~]`; no deploy, no git commit (left to the user).

## Console run #1 (log `logs/brothersinarmshourofheroes_001.log`)

- Reached: loader, 115 ctors, delegate, game thread, level HUD (13 elements), SFX (`alcOpenDevice` real), title loop
  at 59.9 fps, no crash. `fbo centre 000000` at the title: still to confirm on screen.
- Fixed: BGM `sceAudioOutOpenPort -> 0x80260008` (INVALID_SAMPLE_FREQ: MAIN port only takes 48000) -> BGM port type;
  hosttest fake now rejects MAIN != 48000. Video `sceVideodecInitLibrary -> 0x80024302` (NO_FREE_PHYSICAL_PAGE) ->
  vitaGL `ram_threshold` 8 -> 32 MB.
- Minor: `BGM start (ok)` is logged even when the BGM thread failed to open its port.

## Console run #2 (log `logs/brothersinarmshourofheroes_002.log`): black screen, FPS counter moving

- Cause (reproduced in hosttest): `tools/macho_prep.py` rebased two `__DATA.__data` words equal to `0x00010000`
  because they "point into `__text`": 0x170908 = m[15] of a fixed-point GL matrix (1.0) and 0x1709d4 = a u16 index
  table (0,1,2,3...). With the slide, m[15] became 0x98010000 -> modelview w = -26623 -> every vertex clipped.
  Fix: in the `__DATA` scan, a value with low 16 bits == 0 pointing into `__text` is rebased only if it is a known
  function. rebase.bin 12964 -> 12962 words (`Rebase complete: 12806`). **rebase.bin must be re-uploaded.**
- Video `0x80024302` persisted with ram_threshold 32 MB: it is PHYCONT that vitaGL drains. Now
  `vglInitWithCustomThreshold(0, 960, 544, 16 MB ram, 0, 16 MB phycont, 0x8C6000)` as Hero of Sparta II.
- Hosttest fake fixes (they hid the result after the rebase fix): GL_FIXED texcoords tracked per client unit; element
  VBOs shadowed on the CPU so `fx_convert` knows the vertex count (it converted 0 vertices -> texcoords NULL ->
  texel (0,0) everywhere). Fake now rejects MAIN audio ports != 48000 Hz. GLTRACE prints w/z of the first vertex.
- Hosttest result: title screen ("TOUCH THE SCREEN TO CONTINUE") correct and fullscreen, `fbo centre a6a297`,
  BGM port open, 300 s without crash.

## Console run #3 (log `_003.log` + `psp2dmp`): menu OK, crash before in-game

- Confirmed on console: image (title + menu), intro video (`finished (played)`, skippable), BGM.
- Crash: prefetch abort at PC 0xc6e58 (unslid). Literal 0xc649c of the switch table in `Level::RunScript`
  (`cmp r3, r2; ldrls pc, [pc, r3, lsl #2]`, r2 = literal 0x1fa) not rebased: `macho_prep.py` read the register
  `cmp` as an immediate (limit 3). Fix: immediate `cmp` only; for `cmp rn, rm` the bound comes from the preceding
  `ldr rm, [pc, #imm]`. +760 entries (RunScript 507, `Level::UpdateObjective` 0xbf1bc 259), 0 removed:
  rebase.bin 12962 -> 13722 words. **Re-upload rebase.bin** (VPK unchanged).
- Still pending: ~16 `glGetError 0x0500` when entering the menu (harmless for now).

## Console run #4-6

- Run 4: same crash as run 3 (old rebase.bin on console: 12962 words). Run 6 with 13722: past `Level::RunScript`,
  mission BGM `m_mission_holland.wav` loads.
- Run 6 crash: data abort in vitaGL `glDrawElements` (draw.c:355), bound index buffer = 0xaed8. `CGroundMesh::RenderFE`
  (0xe65bc) does `ldrh r1,[r4]` -> `glBindBuffer`: the game stores buffer names as u16, vitaGL names are pointers.
  Fix: `gles.c` glGen/Delete/BindBuffers map small names (1..8191) to vitaGL names. Hosttest 120 s OK, title renders.

## Console run #7

- In-game reached (mission 1, ~14.5 fps, 180-290 draws/frame). Crash on grenade pickup: stack overflow (SP below the
  512 KB pthread stack at 0x8f181000) from infinite recursion in `Sprite::PaintFModule` (0x11ca1c), called by
  `Level::DrawGrenadeInterface` -> `Sprite::DrawString` for the grenade count digit.
- Cause: rebase false positives in `__map_char` (0x170994, u16 ASCII->module: 0x50004 = 4,5) shifted the digit module
  ids to 0x98xxxxxx. Same data scan also slid `_g_meshAlphaAnimVehicle/Ground` (fixed point 0x5000/0xc800) and
  `_g_cheatUNLOCK/GOD`.
- Fix (`tools/macho_prep.py`): __DATA words into `__text` are rebased only if they are a known function start; skip u16
  pairs (n, n+1). rebase.bin 13722 -> 13661 (61 dropped, 0 added). Hosttest title OK.

## Console run #8

- No crash: 4 min of mission 1. Two issues reported: every 3D mesh shows the grass texture; progress not saved.
- Save: `data.sav` on console never rewritten (FTP date = first run). Level::Save checkpoints only fill cGame's
  FileBuffer (`*(cGame+0x1c)`, 0xc360 bytes); the file is written by `cGame::DumpSaveToFile`, which iOS runs from
  `-[AppController applicationWillTerminate:]`. The Vita kills the app silently. Fix: `patch_frame()` (called before
  presentRenderbuffer) hashes the buffer every 3 s and calls DumpSaveToFile when it changes. Hosttest: one dump, ret 0.
- Textures: game uses 1 texture unit, no texture matrix, GL_FIXED texcoords from VBOs (same as the ground, which is
  fine). Suspect rejected uploads (GL_INVALID_ENUM at mission load). Added `[TEX]` log of failed
  glTexImage2D/glCompressedTexImage2D (fmt/size/level) for run 9.

## Console run #9 (log 009)

- Save: `[SAVE] data.sav dumped (0)` at checkpoints and on returning to the menu; no crash in ~5 min of mission 1.
- `[TEX]`: 500+ uploads, zero rejected -> the `0x0500` are not texture uploads.
- Screenshot: hay, logs, soldier and trees OK; only foliage (bushes/hedges) is blue/green noise. Packs: foliage
  is PVR `0x11` = RGBA5551 with mipmaps. vitaGL stores `GL_RGBA`+5551 natively (U5U5U5U1_RGBA), ignores the
  game's mip data (`gpu_alloc_mipmaps`) and downscales with the `U1U5U5U5_ABGR` transfer format -> 5-bit fields
  misaligned -> noise once minified. Fix (gles.c `g_glTexImage2D`): 5551 + `GL_RGBA` -> ifmt `GL_RGBA8`
  (vitaGL expands to RGBA8888, mips OK). Hosttest: title unchanged (it doesn't reach the mission).

## Console run #10 (log 010) -- PLAYABLE

- Foliage textures correct after the RGBA5551 -> RGBA8 fix; mission 1 playable start to grenade tutorial, 14.5 fps.
- Tilt: the grenade tutorial asks to tilt the device. `input.c` now feeds sceMotion to CTouchPad::EventSetTilt /
  EventSetTiltAcceleration with the same 0.8/0.2 low-pass and x100 scale as -[AppController accelerometer:didAccelerate:].
  Axis mapping (iOS x = -vita y, y = vita x) is derived from orientation 4, not yet verified on hardware.
