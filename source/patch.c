/**
 * @file patch.c
 * @brief Game-specific hooks for Brothers In Arms: Hour of Heroes (BIA 1.3.1, armv6).
 * @details Every hooked symbol exists in decompiled/macho/symbols.txt (non-stripped binary):
 *          - SoundEngine_*BackgroundMusic*: Apple SoundEngine BGM (AudioQueue + AAC) -> coreaudio.c WAV streamer.
 *          - LoadMovie/PlayMovie/IsMoviePlaying/IsMoviePaused/StopMovie/SuspendMovie/ResumeMovie (C wrappers
 *            around -[MoviePlayer ...]): cGame::update3D/doLoadGame call PlayMovie on the game thread, then
 *            poll IsMoviePlaying/IsMoviePaused every frame and call StopMovie when both are 0. We play the
 *            whole video synchronously inside PlayMovie (video_avc.c) and report "not playing" afterwards.
 *          - CTouchElement::SetScreenCoordinates: reimplemented 1:1 (4 stores) plus a log line, so the console
 *            log lists every on-screen control rectangle for controls.txt calibration.
 */
#include <kubridge.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>

#include "coreaudio.h"
#include "gles.h"
#include "hle.h"
#include "imports.h"
#include "log.h"
#include "macho.h"
#include "patch.h"
#include "video.h"

// ---------------------------------------------------------------- autosave

// iOS writes data.sav from -[AppController applicationWillTerminate:] (0x15b1c4); Level::Save checkpoints only
// fill cGame's in-memory FileBuffer (data ptr at cGame+0x1c, 0xc360 bytes). The Vita kills the app without
// notice, so progress was lost (run 8). Dump the buffer when it changes, on the game thread between frames.
#define SAVE_SIZE 0xc360
static uint32_t fnv1a(const uint8_t *p, int n) {
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

void patch_frame(void) {
    static void **pgame;
    static uintptr_t dump;
    static uint64_t last_us;
    static uint32_t last_hash;
    if (!pgame) {
        pgame = (void **)macho_sym("_g_pGame");
        dump = macho_sym("__ZN5cGame14DumpSaveToFileEv");
        if (!pgame || !dump)
            return;
    }
    uint64_t now = sceKernelGetProcessTimeWide();
    if (now - last_us < 3000000)
        return;
    last_us = now;
    uint8_t *game = *pgame;
    const uint8_t *buf = game ? *(const uint8_t **)(game + 0x1c) : NULL;
    if (!buf)
        return;
    uint32_t h = fnv1a(buf, SAVE_SIZE);
    if (h == last_hash)
        return;
    int first = last_hash == 0;
    last_hash = h;
    if (first) // the save as loaded at boot
        return;
    int r = (int)call_guest6((void *)dump, (uint32_t)game, 0, 0, 0, 0, 0);
    l_info("[SAVE] data.sav dumped (%d)", r);
    last_hash = fnv1a(buf, SAVE_SIZE); // DumpSaveToFile rewrites the header (CRC): don't re-trigger
}

// ---------------------------------------------------------------- BGM

// OSStatus SoundEngine_LoadBackgroundMusicTrack(const char *path, Boolean addToQueue, Boolean loadAtOnce)
static int hook_LoadBackgroundMusicTrack(const char *path, int add_to_queue, int load_at_once) {
    return bgm_load(path) ? -1 : 0;
}
// CSoundWrap::MusicStart(bool loop) -> SoundEngine_StartBackgroundMusic(loop) (0x12b444)
static int hook_StartBackgroundMusic(int loop) { return bgm_start(loop & 0xff); }
static int hook_StopBackgroundMusic(int stop_at_end) { return bgm_stop(stop_at_end & 0xff); }
static int hook_UnloadBackgroundMusicTrack(void) { return bgm_unload(); }
static int hook_PauseBackgroundMusic(void) { bgm_pause(1); return 0; }
static int hook_ResumeBackgroundMusic(void) { bgm_pause(0); return 0; }
static int hook_IsMusicPlaying(void) { return bgm_is_playing(); }
// Float32 argument: softfp ABI -> arrives in r0 (the whole loader is built -mfloat-abi=softfp).
static int hook_SetBackgroundMusicVolume(float v) {
    bgm_set_volume(v);
    return 0;
}

// ---------------------------------------------------------------- video

static char s_movie[256];

static void hook_LoadMovie(const char *name) {
    snprintf(s_movie, sizeof(s_movie), "%s", name ? name : "");
    l_info("[VIDEO] LoadMovie(\"%s\")", s_movie);
}

static void hook_PlayMovie(void) {
    if (!s_movie[0]) {
        l_warn("[VIDEO] PlayMovie without LoadMovie: skipped");
        return;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s%s", BUNDLE_PATH, s_movie);
    bgm_pause(1); // iOS MPMoviePlayerController ducks the app audio
    l_info("[VIDEO] PlayMovie(%s)", path);
    int ok = video_play_avc(path);
    l_info("[VIDEO] %s finished (%s)", s_movie, ok ? "played" : "missing/undecodable: skipped");
    gles_reapply_arrays();
    bgm_pause(0);
}

static int hook_IsMoviePlaying(void) { return 0; }
static int hook_IsMoviePaused(void) { return 0; }
static void hook_MovieNoop(void) {}

// ---------------------------------------------------------------- touch element log

static void hook_SetScreenCoordinates(uint8_t *el, int x, int y, int w, int h) {
    *(int *)(el + 0x18) = x, *(int *)(el + 0x1c) = y, *(int *)(el + 0x20) = w, *(int *)(el + 0x24) = h;
    static int logged;
    if (logged++ < 64)
        l_info("[INPUT] element %p type %d rect %d,%d %dx%d", el, *(int *)(el + 0x8), x, y, w, h);
}

const import_entry patch_imports[] = {
    {"_unused_patch_entry", 0, 0}, // patch.c exposes no imports; hooks are installed below
};
const int patch_imports_count = 0;

void patch_init(void) {
    // armv6 VFP code must never run with FPSCR.LEN/STRIDE set (Cortex-A9 traps vector mode). The binary has
    // no "vmsr fpscr" (grep of the disassembly), so only make sure our own state starts clean.
    uint32_t fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
    if (fpscr & 0x00370000) {
        fpscr &= ~0x00370000;
        __asm__ volatile("vmsr fpscr, %0" ::"r"(fpscr));
    }

    macho_hook("_SoundEngine_LoadBackgroundMusicTrack", hook_LoadBackgroundMusicTrack);
    macho_hook("_SoundEngine_StartBackgroundMusic", hook_StartBackgroundMusic);
    macho_hook("_SoundEngine_StopBackgroundMusic", hook_StopBackgroundMusic);
    macho_hook("_SoundEngine_UnloadBackgroundMusicTrack", hook_UnloadBackgroundMusicTrack);
    macho_hook("_SoundEngine_PauseBackgroundMusic", hook_PauseBackgroundMusic);
    macho_hook("_SoundEngine_ResumeBackgroundMusic", hook_ResumeBackgroundMusic);
    macho_hook("_SoundEngine_IsMusicPlaying", hook_IsMusicPlaying);
    macho_hook("_SoundEngine_SetBackgroundMusicVolume", hook_SetBackgroundMusicVolume);

    macho_hook("__Z9LoadMoviePKc", hook_LoadMovie);
    macho_hook("_PlayMovie", hook_PlayMovie);
    macho_hook("_IsMoviePlaying", hook_IsMoviePlaying);
    macho_hook("_IsMoviePaused", hook_IsMoviePaused);
    macho_hook("_StopMovie", hook_MovieNoop);
    macho_hook("_SuspendMovie", hook_MovieNoop);
    macho_hook("_ResumeMovie", hook_MovieNoop);

    macho_hook("__ZN13CTouchElement20SetScreenCoordinatesEiiii", hook_SetScreenCoordinates);
    l_info("[PATCH] BGM, movie and touch-element hooks installed");
}
