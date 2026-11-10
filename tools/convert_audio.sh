#!/bin/sh
# Converts the iOS background music (AAC-LC .m4a, decoded by AudioQueue on iOS) into the format the
# loader streams (source/coreaudio.c bgm_*): RIFF WAV, PCM s16le, stereo, 44100 Hz, same base name.
# SFX need nothing: they are already PCM WAV inside sounds.bar. Videos need nothing (H.264 + AAC, SceAvcdec).
#
# Usage: tools/convert_audio.sh <dir with m_*.m4a> [<output dir>]   (output defaults to the input dir)
# Needs ffmpeg (brew install ffmpeg); falls back to macOS afconvert.
set -e
IN="$1"; OUT="${2:-$1}"
[ -d "$IN" ] || { echo "usage: $0 <dir> [outdir]" >&2; exit 1; }
mkdir -p "$OUT"
n=0
for f in "$IN"/*.m4a; do
    case "$(basename "$f")" in ._*) continue ;; esac   # exFAT AppleDouble files
    [ -f "$f" ] || continue
    wav="$OUT/$(basename "${f%.m4a}").wav"
    if [ -f "$wav" ] && [ "$wav" -nt "$f" ]; then n=$((n + 1)); continue; fi
    if command -v ffmpeg >/dev/null; then
        ffmpeg -nostdin -loglevel error -y -i "$f" -vn -ac 2 -ar 44100 -c:a pcm_s16le -f wav "$wav"
    else
        afconvert -f WAVE -d LEI16@44100 -c 2 "$f" "$wav"
    fi
    n=$((n + 1))
done
echo "[+] $n music tracks -> $OUT (WAV s16 stereo 44.1 kHz)"
