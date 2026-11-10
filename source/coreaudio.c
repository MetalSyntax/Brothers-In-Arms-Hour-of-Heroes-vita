/**
 * @file coreaudio.c
 * @brief AudioToolbox emulation for Brothers In Arms: Hour of Heroes (Apple "SoundEngine" sample code).
 * @details Evidence (decompiled/macho/symbols.txt + disassembly):
 *          - SFX: SoundEngineEffect::AttachFilesToSource feeds whole WAV files (PCM s16 mono/stereo,
 *            16/22.05 kHz, packed in sounds.bar) to AudioFileStreamOpen/ParseBytes. The property
 *            listener SoundDataParser::Parser_PropertyListener only reads 'dfmt' (40-byte ASBD) and
 *            'bcnt' (4 bytes!), the packets listener memcpy()s the PCM into a malloc'd buffer, then
 *            the game uploads it with alcGetProcAddress("alBufferDataStatic") -> OpenAL.
 *          - BGM: BackgroundTrackMgr streams m_*.m4a (AAC-LC) through AudioFile + AudioQueue. AAC has no
 *            cheap decoder here, so tools/convert_audio.sh turns them into m_*.wav (s16 stereo 44.1 kHz)
 *            and patch.c hooks the SoundEngine_*BackgroundMusic* functions onto bgm_* below, which stream
 *            the WAV from disk on a sceAudioOut MAIN port (openal-soft owns the BGM port).
 *          AudioQueue/AudioFile entry points are kept as logged shims for anything that bypasses the hooks.
 */
#include <AL/al.h>
#include <AL/alc.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "coreaudio.h"
#include "darwin.h"
#include "imports.h"
#include "log.h"
#include "macho.h"

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define kAudioFormatLinearPCM FOURCC('l', 'p', 'c', 'm')
#define kPropDataFormat FOURCC('d', 'f', 'm', 't')
#define kPropByteCount FOURCC('b', 'c', 'n', 't')
#define kPropDataOffset FOURCC('d', 'o', 'f', 'f')
#define kPropReady FOURCC('r', 'e', 'd', 'y')
#define kPropPacketCount FOURCC('p', 'c', 'n', 't')
#define kPropMaxPacket FOURCC('p', 's', 'z', 'u') // PacketSizeUpperBound
#define kAudioFileUnspecifiedError FOURCC('w', 'h', 'a', '?')

typedef struct {
    double mSampleRate;
    uint32_t mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame,
        mChannelsPerFrame, mBitsPerChannel, mReserved;
} asbd;

static int ok(void) { return 0; }

// ---------------------------------------------------------------- WAV header parsing (shared)

typedef struct {
    asbd fmt;
    uint32_t data_offset, data_size;
} wav_info;

static void pcm_asbd(asbd *f, uint32_t rate, uint32_t ch, uint32_t bits) {
    *f = (asbd){rate, kAudioFormatLinearPCM, 0xC /* signed int | packed */, ch * bits / 8, 1, ch * bits / 8, ch, bits, 0};
}

// Parses a RIFF/WAVE header held in memory. Returns 1 when 'fmt ' and the 'data' chunk start were found.
static int wav_parse(const uint8_t *p, uint32_t n, wav_info *w) {
    if (n < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
        return 0;
    int have_fmt = 0;
    for (uint32_t off = 12; off + 8 <= n;) {
        uint32_t sz;
        memcpy(&sz, p + off + 4, 4);
        if (!memcmp(p + off, "fmt ", 4) && off + 8 + 16 <= n) {
            uint16_t tag, ch, bits;
            uint32_t rate;
            memcpy(&tag, p + off + 8, 2);
            memcpy(&ch, p + off + 10, 2);
            memcpy(&rate, p + off + 12, 4);
            memcpy(&bits, p + off + 22, 2);
            if (tag != 1)
                l_warn("[AUDIO] WAV format tag %u is not PCM", tag);
            pcm_asbd(&w->fmt, rate, ch, bits);
            have_fmt = 1;
        } else if (!memcmp(p + off, "data", 4)) {
            w->data_offset = off + 8;
            w->data_size = sz;
            return have_fmt;
        }
        off += 8 + sz + (sz & 1);
    }
    return 0;
}

static void put_prop(void *out, uint32_t *io_size, const void *val, uint32_t val_size) {
    uint32_t n = io_size && *io_size < val_size ? *io_size : val_size;
    memcpy(out, val, n);
    if (io_size)
        *io_size = n;
}

// ---------------------------------------------------------------- AudioFileStream (SFX path)

typedef struct {
    void *client, *prop_cb, *pkt_cb;
    uint8_t *hdr;      // header bytes accumulated until the 'data' chunk is located
    uint32_t hdr_len;
    int ready;
    wav_info wav;
    uint32_t delivered; // PCM bytes already handed to the packets callback
} file_stream;

static int fs_Open(void *client, void *prop_cb, void *pkt_cb, uint32_t type_hint, file_stream **out) {
    file_stream *s = calloc(1, sizeof(*s));
    s->client = client, s->prop_cb = prop_cb, s->pkt_cb = pkt_cb;
    *out = s;
    return 0;
}

static void fs_deliver(file_stream *s, const uint8_t *pcm, uint32_t n) {
    uint32_t left = s->wav.data_size - s->delivered;
    if (n > left)
        n = left;
    uint32_t bpf = s->wav.fmt.mBytesPerFrame ? s->wav.fmt.mBytesPerFrame : 2;
    n -= n % bpf;
    if (!n)
        return;
    // AudioFileStream_PacketsProc(client, numBytes, numPackets, data, packetDescs): LPCM -> no descs.
    call_guest6(s->pkt_cb, (uint32_t)s->client, n, n / bpf, (uint32_t)pcm, 0, 0);
    s->delivered += n;
}

static int fs_ParseBytes(file_stream *s, uint32_t size, const void *data, uint32_t flags) {
    if (!s || !data)
        return kAudioFileUnspecifiedError;
    if (s->ready) {
        fs_deliver(s, data, size);
        return 0;
    }
    s->hdr = realloc(s->hdr, s->hdr_len + size);
    memcpy(s->hdr + s->hdr_len, data, size);
    s->hdr_len += size;
    if (!wav_parse(s->hdr, s->hdr_len, &s->wav))
        return s->hdr_len > 64 * 1024 ? kAudioFileUnspecifiedError : 0; // need more header bytes
    s->ready = 1;
    static const uint32_t props[] = {kPropDataFormat, kPropDataOffset, kPropByteCount, kPropReady};
    for (unsigned i = 0; i < sizeof(props) / sizeof(*props); i++) {
        uint32_t fl = 0;
        call_guest(s->prop_cb, (uint32_t)s->client, (uint32_t)s, props[i], (uint32_t)&fl);
    }
    if (s->hdr_len > s->wav.data_offset)
        fs_deliver(s, s->hdr + s->wav.data_offset, s->hdr_len - s->wav.data_offset);
    free(s->hdr);
    s->hdr = NULL;
    return 0;
}

static int fs_GetProperty(file_stream *s, uint32_t id, uint32_t *io_size, void *out) {
    if (!s || !s->ready || !out)
        return kAudioFileUnspecifiedError;
    uint64_t v64;
    uint32_t v32 = 1;
    switch (id) {
    case kPropDataFormat:
        put_prop(out, io_size, &s->wav.fmt, sizeof(asbd));
        return 0;
    case kPropByteCount: // UInt64 per Apple; the game asks for 4 bytes (little-endian low word)
        v64 = s->wav.data_size;
        put_prop(out, io_size, &v64, 8);
        return 0;
    case kPropDataOffset:
        v64 = s->wav.data_offset;
        put_prop(out, io_size, &v64, 8);
        return 0;
    case kPropReady:
        put_prop(out, io_size, &v32, 4);
        return 0;
    }
    l_warn("[AUDIO] AudioFileStreamGetProperty('%c%c%c%c') unsupported", id >> 24, (id >> 16) & 0xFF, (id >> 8) & 0xFF, id & 0xFF);
    return kAudioFileUnspecifiedError;
}

static int fs_Close(file_stream *s) {
    if (s) {
        free(s->hdr);
        free(s);
    }
    return 0;
}

// ---------------------------------------------------------------- AudioFile (WAV on disk, LPCM packets)

typedef struct {
    FILE *f;
    wav_info wav;
} audio_file;

// .m4a/.caf requests are served from the offline-converted .wav next to them (tools/convert_audio.sh).
static FILE *open_wav(const char *guest_path, wav_info *w, char *real, size_t real_len) {
    darwin_path(guest_path, real, real_len);
    char *dot = strrchr(real, '.');
    if (dot && (!strcasecmp(dot, ".m4a") || !strcasecmp(dot, ".caf") || !strcasecmp(dot, ".mp3")))
        strcpy(dot, ".wav"); // same length or shorter: fits
    FILE *f = fopen(real, "rb");
    if (!f)
        return NULL;
    uint8_t hdr[512];
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    if (!wav_parse(hdr, n, w)) {
        fclose(f);
        return NULL;
    }
    return f;
}

static const char *url_path(void *url) {
    // CFURLCreateFromFileSystemRepresentation (objc.c) returns an NSString-shaped object: cstr at +8.
    return url ? *(const char **)((uint8_t *)url + 8) : NULL;
}

static int af_OpenURL(void *url, int perms, int hint, audio_file **out) {
    char real[512] = "";
    audio_file *a = calloc(1, sizeof(*a));
    a->f = url_path(url) ? open_wav(url_path(url), &a->wav, real, sizeof(real)) : NULL;
    if (!a->f) {
        l_warn("[AUDIO] AudioFileOpenURL(%s) failed (missing converted .wav?)", url_path(url) ? url_path(url) : "(null)");
        free(a);
        return kAudioFileUnspecifiedError;
    }
    *out = a;
    return 0;
}

static int af_Close(audio_file *a) {
    if (a) {
        fclose(a->f);
        free(a);
    }
    return 0;
}

static int af_GetPropertyInfo(audio_file *a, uint32_t id, uint32_t *size, uint32_t *writable) {
    if (writable)
        *writable = 0;
    if (!a)
        return kAudioFileUnspecifiedError;
    uint32_t n = id == kPropDataFormat ? sizeof(asbd) : (id == kPropByteCount || id == kPropPacketCount) ? 8 : id == kPropMaxPacket ? 4 : 0;
    if (!n)
        return kAudioFileUnspecifiedError; // no magic cookie / channel layout for LPCM
    if (size)
        *size = n;
    return 0;
}

static int af_GetProperty(audio_file *a, uint32_t id, uint32_t *io_size, void *out) {
    if (!a || !out)
        return kAudioFileUnspecifiedError;
    uint64_t v64;
    uint32_t v32;
    switch (id) {
    case kPropDataFormat:
        put_prop(out, io_size, &a->wav.fmt, sizeof(asbd));
        return 0;
    case kPropByteCount:
        v64 = a->wav.data_size;
        put_prop(out, io_size, &v64, 8);
        return 0;
    case kPropPacketCount:
        v64 = a->wav.data_size / a->wav.fmt.mBytesPerPacket;
        put_prop(out, io_size, &v64, 8);
        return 0;
    case kPropMaxPacket:
        v32 = a->wav.fmt.mBytesPerPacket;
        put_prop(out, io_size, &v32, 4);
        return 0;
    }
    return kAudioFileUnspecifiedError;
}

// AudioFileReadPackets(file, useCache, &outNumBytes, packetDescs, (SInt64)startPacket, &ioNumPackets, outBuffer):
// r0-r3 hold the first four args, the SInt64 sits 8-byte aligned on the stack (two words).
static int af_ReadPackets(audio_file *a, int cache, uint32_t *out_bytes, void *descs, uint32_t start_lo, uint32_t start_hi,
                          uint32_t *io_packets, void *buf) {
    if (!a || !io_packets || !buf)
        return kAudioFileUnspecifiedError;
    uint32_t bpp = a->wav.fmt.mBytesPerPacket;
    uint64_t start = (uint64_t)start_hi << 32 | start_lo;
    uint64_t total = a->wav.data_size / bpp;
    uint32_t n = start >= total ? 0 : (uint32_t)(total - start < *io_packets ? total - start : *io_packets);
    fseek(a->f, a->wav.data_offset + start * bpp, SEEK_SET);
    n = fread(buf, bpp, n, a->f);
    *io_packets = n;
    if (out_bytes)
        *out_bytes = n * bpp;
    return 0;
}

// ---------------------------------------------------------------- AudioQueue (not used: BGM is hooked)

static int aq_unused(void) {
    static int logged;
    if (!logged++)
        l_warn("[AUDIO] AudioQueue called directly (BGM hooks bypassed?): returning an error");
    return kAudioFileUnspecifiedError;
}

// ---------------------------------------------------------------- BGM streamer (SoundEngine hooks)

#define BGM_GRAIN 1024
#define BGM_RATE 44100

static pthread_mutex_t bgm_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *bgm_file;
static wav_info bgm_wav;
static uint32_t bgm_pos;
static volatile int bgm_playing, bgm_paused, bgm_loop = 1;
static volatile float bgm_volume = 1.0f;
static SceUID bgm_thread = -1;

// Reads up to BGM_GRAIN frames of the current track into raw (handles loop/end). Caller holds bgm_lock.
static int bgm_fill(int16_t *raw, int *ch_out) {
    int frames = 0, ch = bgm_wav.fmt.mChannelsPerFrame == 1 ? 1 : 2;
    uint32_t bpf = ch * 2;
    *ch_out = ch;
    while (frames < BGM_GRAIN) {
        uint32_t left = (bgm_wav.data_size - bgm_pos) / bpf;
        if (!left) {
            if (!bgm_loop) {
                bgm_playing = 0;
                break;
            }
            bgm_pos = 0;
            fseek(bgm_file, bgm_wav.data_offset, SEEK_SET);
            continue;
        }
        uint32_t want = BGM_GRAIN - frames < (int)left ? BGM_GRAIN - frames : left;
        uint32_t got = fread(raw + frames * ch, bpf, want, bgm_file);
        bgm_pos = got < want ? bgm_wav.data_size : bgm_pos + got * bpf; // short read = truncated file: end
        frames += got;
    }
    return frames;
}

static int bgm_thread_fn(SceSize args, void *argp) {
    // MAIN ports only accept 48000 Hz (console run #1: 0x80260008 INVALID_SAMPLE_FREQ); BGM ports take 44100
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, BGM_GRAIN, BGM_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        l_error("[AUDIO] BGM sceAudioOutOpenPort -> 0x%08X", port);
        return 0;
    }
    l_info("[AUDIO] BGM port %d open (%d Hz stereo, grain %d)", port, BGM_RATE, BGM_GRAIN);
    static int16_t raw[BGM_GRAIN * 2], out[BGM_GRAIN * 2];
    for (;;) {
        int frames = 0, ch = 2;
        pthread_mutex_lock(&bgm_lock);
        if (bgm_file && bgm_playing && !bgm_paused)
            frames = bgm_fill(raw, &ch);
        float vol = bgm_volume;
        pthread_mutex_unlock(&bgm_lock);
        for (int i = 0; i < BGM_GRAIN; i++) {
            int l = 0, r = 0;
            if (i < frames) {
                l = raw[i * ch] * vol;
                r = ch == 2 ? raw[i * 2 + 1] * vol : l;
            }
            out[i * 2] = l, out[i * 2 + 1] = r;
        }
        sceAudioOutOutput(port, out); // blocks one grain: paces the thread even when silent
    }
    return 0;
}

static void bgm_ensure_thread(void) {
    if (bgm_thread >= 0)
        return;
    bgm_thread = sceKernelCreateThread("bgm", bgm_thread_fn, 0x10000100, 64 * 1024, 0, 0, NULL);
    if (bgm_thread >= 0)
        sceKernelStartThread(bgm_thread, 0, NULL);
    else
        l_error("[AUDIO] BGM thread create -> 0x%08X", bgm_thread);
}

int bgm_load(const char *path) {
    char real[512] = "";
    wav_info w;
    FILE *f = path ? open_wav(path, &w, real, sizeof(real)) : NULL;
    if (!f) {
        l_warn("[AUDIO] BGM load '%s' failed: run tools/stage_data.sh (needs %s)", path ? path : "(null)", real);
        return -1;
    }
    if (w.fmt.mBitsPerChannel != 16 || (int)w.fmt.mSampleRate != BGM_RATE)
        l_warn("[AUDIO] BGM %s is %d Hz/%u bit: expected %d Hz s16 (re-run tools/convert_audio.sh)", real,
               (int)w.fmt.mSampleRate, (unsigned)w.fmt.mBitsPerChannel, BGM_RATE);
    bgm_ensure_thread();
    pthread_mutex_lock(&bgm_lock);
    if (bgm_file)
        fclose(bgm_file);
    bgm_file = f;
    bgm_wav = w;
    bgm_pos = 0;
    fseek(f, w.data_offset, SEEK_SET);
    bgm_playing = bgm_paused = 0;
    pthread_mutex_unlock(&bgm_lock);
    l_info("[AUDIO] BGM loaded %s (%u Hz, %u ch, %u bytes)", real, (unsigned)w.fmt.mSampleRate,
           (unsigned)w.fmt.mChannelsPerFrame, (unsigned)w.data_size);
    return 0;
}

int bgm_start(int loop) {
    pthread_mutex_lock(&bgm_lock);
    bgm_loop = loop;
    bgm_playing = bgm_file != NULL;
    bgm_paused = 0;
    pthread_mutex_unlock(&bgm_lock);
    l_info("[AUDIO] BGM start (loop=%d, %s)", loop, bgm_file ? "ok" : "no track loaded");
    return 0;
}

int bgm_stop(int at_end) {
    pthread_mutex_lock(&bgm_lock);
    if (at_end)
        bgm_loop = 0; // finish the current pass, then stop
    else
        bgm_playing = 0;
    pthread_mutex_unlock(&bgm_lock);
    return 0;
}

int bgm_unload(void) {
    pthread_mutex_lock(&bgm_lock);
    bgm_playing = 0;
    if (bgm_file)
        fclose(bgm_file);
    bgm_file = NULL;
    pthread_mutex_unlock(&bgm_lock);
    return 0;
}

void bgm_pause(int paused) { bgm_paused = paused; }
int bgm_is_playing(void) { return bgm_playing && !bgm_paused; }
void bgm_set_volume(float v) { bgm_volume = v < 0 ? 0 : v > 1 ? 1 : v; }

// ---------------------------------------------------------------- AudioSession / OpenAL glue

// iOS OpenAL reports AL_NO_ERROR before a context is current; openal-soft reports AL_INVALID_OPERATION.
// SoundEngine_Initialize checks alGetError() right after alcOpenDevice (Blades of Fury, same engine).
static ALenum al_GetErrorWrap(void) { return alcGetCurrentContext() ? alGetError() : AL_NO_ERROR; }

// Apple extension: the app keeps ownership of the data. openal-soft copies it, which is equivalent.
static void al_BufferDataStatic(ALuint buf, ALenum fmt, const ALvoid *data, ALsizei size, ALsizei freq) {
    alBufferData(buf, fmt, data, size, freq);
}

static void *al_GetProcAddress(ALCdevice *dev, const ALCchar *name) {
    char n[80];
    snprintf(n, sizeof(n), "_%s", name ? name : "");
    void *p = (void *)macho_import_thunk(n);
    l_info("[AUDIO] alcGetProcAddress(%s) -> %p", name ? name : "(null)", p);
    return p;
}

#define F(name, fn) {"_" #name, (uintptr_t)(fn), 0}

const import_entry coreaudio_imports[] = {
    F(AudioSessionInitialize, ok), F(AudioSessionSetActive, ok), F(AudioSessionSetProperty, ok),
    F(AudioFileStreamOpen, fs_Open), F(AudioFileStreamParseBytes, fs_ParseBytes),
    F(AudioFileStreamGetProperty, fs_GetProperty), F(AudioFileStreamClose, fs_Close),
    F(AudioFileOpenURL, af_OpenURL), F(AudioFileClose, af_Close), F(AudioFileGetPropertyInfo, af_GetPropertyInfo),
    F(AudioFileGetProperty, af_GetProperty), F(AudioFileReadPackets, af_ReadPackets),
    F(AudioQueueNewOutput, aq_unused), F(AudioQueueAllocateBuffer, aq_unused), F(AudioQueueEnqueueBuffer, aq_unused),
    F(AudioQueuePrime, aq_unused), F(AudioQueueStart, aq_unused), F(AudioQueueGetProperty, aq_unused),
    F(AudioQueueSetProperty, aq_unused), F(AudioQueueAddPropertyListener, aq_unused),
    F(AudioQueueSetParameter, ok), F(AudioQueuePause, ok), F(AudioQueueStop, ok), F(AudioQueueDispose, ok),
    F(AudioQueueFreeBuffer, ok),
    F(alGetError, al_GetErrorWrap), F(alcGetProcAddress, al_GetProcAddress), F(alBufferDataStatic, al_BufferDataStatic),
};
const int coreaudio_imports_count = sizeof(coreaudio_imports) / sizeof(coreaudio_imports[0]);
