# Brothers In Arms Hour of Heroes (iOS Mach-O) → PS Vita

Port de Brothers In Arms Hour of Heroes (iOS Mach-O) a PS Vita con loader de Mach-O propio.
Leer `PORTING_PLAN.md` antes de modificar el loader.

## Estructura

- `source/macho.c`: Carga, parseo Mach-O, mapeo de segmentos, rebase y resolución de imports.
- `source/darwin.c`: Capa de compatibilidad libc/libgcc/pthread/semáforos de Darwin.
- `source/objc.c` (ObjC/CF/UIKit por nombre de selector), `gles.c` (GLES1 sobre vitaGL, FBO 320x480 girado),
  `coreaudio.c` (AudioFileStream/AudioFile WAV + streamer de BGM), `openal.c`, `patch.c` (hooks BGM/vídeo/HUD),
  `input.c` (táctil + botones → `CTouchPad`, `controls.txt`), `video_avc.c` (H.264 + AAC).
- `tools/stage_data.sh` (prepara `ux0_data/`, convierte música con `tools/convert_audio.sh`),
  `tools/imports_table.py --check` (genera `docs/imports.md`), `hosttest/run.sh build|run` (loader en Docker ARM32).
- `game_app` → symlink a la carpeta `.app` original del IPA.
- `ux0_data/brothersinarmshourofheroes/`: Datos extraídos y assets del juego en la consola.
- `PORTING_PLAN.md`: Plan de port y estado actual de fases.
- `port_progress.md`: Registro de progreso.

## Reglas

- ABI iOS ARM: softfp en ARMv6/v7 (floats en registros enteros), r9 temporal (preservar en callbacks).
- Nada del IPA original en el repositorio git (.gitignore anti-DMCA).
- Un bug por corrida de consola física, guiado por logs. Antes de cada corrida: `hosttest/run.sh run` (ver checklist
  "Console run #1" en `port_progress.md`).
