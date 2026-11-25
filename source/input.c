/**
 * @file input.c
 * @brief Vita front touch + physical buttons -> the game's CTouchPad (5 touch slots).
 * @details Evidence: -[EAGLView touchesChangedWithEvent:] (0x15c618) converts each UITouch with
 *          CTouchPad::TransformCoordinatesByOrientation and calls CTouchPad::EventTouchDown/Move/Up
 *          (this = *CTouchPad::s_touchPadWrite, slot 0..4 from -[EAGLView getTouchIndex:], x, y) in the
 *          480x320 landscape space. We call the same three functions directly with landscape coordinates,
 *          so no UITouch/NSSet emulation is needed. Runs on the main thread like iOS touch delivery.
 *
 *          Slots: 0-1 front-touch fingers, 2 left stick (virtual joystick drag), 3 right stick (look drag),
 *          4 face/shoulder buttons (one held at a time, synthesized taps on HUD elements).
 *          Button positions come from Level::InitTouchElements (SetScreenCoordinates constants, see
 *          PORTING_PLAN Phase 7) and can be remapped in ux0:data/brothersinarmshourofheroes/controls.txt.
 */
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gles.h"
#include "hle.h"
#include "input.h"
#include "log.h"
#include "macho.h"
#include "options.h"

#define GAME_W 480
#define GAME_H 320
#define SLOT_FINGER0 0
#define SLOT_LSTICK 2
#define SLOT_RSTICK 3
#define SLOT_BUTTON 4

typedef struct {
    const char *name;
    uint32_t mask;
    int x, y; // tap point in game space; x < 0 = unmapped
} button_map;

// Defaults from Level::InitTouchElements rectangles (x,y,w,h -> centre). Table order = priority on slot 4.
static button_map buttons[] = {
    {"R", SCE_CTRL_RTRIGGER, 413, 261},        // right on-screen stick (fire), InitJoystick 413,261 r=47
    {"CROSS", SCE_CTRL_CROSS, 375, 290},       // element 345,260 60x60
    {"SQUARE", SCE_CTRL_SQUARE, 445, 290},     // element 410,260 70x60
    {"TRIANGLE", SCE_CTRL_TRIANGLE, 435, 275}, // element 405,245 60x60
    {"CIRCLE", SCE_CTRL_CIRCLE, 445, 165},     // element 405,135 80x60 (hosttest log)
    {"L", SCE_CTRL_LTRIGGER, 30, 145},         // element 0,115 60x60
    {"START", SCE_CTRL_START, 453, 27},        // element 408,-18 91x91 (top-right corner)
    {"SELECT", SCE_CTRL_SELECT, 174, 16},      // element 149,0 50x33
    {"UP", SCE_CTRL_UP, -1, -1},               // D-pad unmapped (-1) = drives the left stick
    {"DOWN", SCE_CTRL_DOWN, -1, -1},
    {"LEFT", SCE_CTRL_LEFT, -1, -1},
    {"RIGHT", SCE_CTRL_RIGHT, -1, -1},
};
#define NBUTTONS (int)(sizeof(buttons) / sizeof(buttons[0]))
enum { B_UP = 8, B_DOWN, B_LEFT, B_RIGHT };

static struct {
    int lx, ly, lr;     // left virtual joystick centre/radius (InitJoystick 75,245 r=33)
    int rx, ry, rspeed; // look drag start point and pixels/poll at full deflection
    int deadzone;       // analog deadzone (0..127)
} cfg = {75, 245, 33, 240, 140, 6, 10};

static uintptr_t fn_down, fn_move, fn_up, pad_ptr, fn_tilt, fn_tilt_acc;

// Tilt (grenade aiming, "Inclina el iPhone"): -[AppController accelerometer:didAccelerate:] (0x15aeb8) low-passes
// UIAcceleration (f = f*0.8 + a*0.2, per axis) and calls CTouchPad::EventSetTilt(f*100) and
// EventSetTiltAcceleration(raw*100 - f*100). Same here from sceMotion. iOS axes are portrait-device axes; the game
// runs in UIDeviceOrientationLandscapeRight (home button left, device top -> screen right), so
// iOS x = screen down = -vita y, iOS y = screen right = vita x, z unchanged (both read -1 g lying face up).
// ponytail: filter steps once per polled frame (iOS: 30 Hz); at ~15 fps it settles twice as slow.
static void tilt_poll(uint32_t pad) {
    static float f[3];
    SceMotionState m;
    if (!fn_tilt || sceMotionGetState(&m) < 0)
        return;
    float a[3] = {-m.acceleration.y, m.acceleration.x, m.acceleration.z};
    int fi[3], hi[3];
    for (int i = 0; i < 3; i++) {
        f[i] = f[i] * 0.8f + a[i] * 0.2f;
        fi[i] = (int)(f[i] * 100.0f);
        hi[i] = (int)(a[i] * 100.0f) - fi[i];
    }
    call_guest((void *)fn_tilt, pad, fi[0], fi[1], fi[2]);
    if (fn_tilt_acc)
        call_guest((void *)fn_tilt_acc, pad, hi[0], hi[1], hi[2]);
}

typedef struct {
    int down, x, y;
} slot_state;
static slot_state slots[5];

static void slot_set(int slot, int down, int x, int y) {
    uint32_t pad = *(uint32_t *)pad_ptr;
    if (!pad)
        return; // CTouchPad not created yet (before GameStart)
    x = x < 0 ? 0 : x >= GAME_W ? GAME_W - 1 : x;
    y = y < 0 ? 0 : y >= GAME_H ? GAME_H - 1 : y;
    slot_state *s = &slots[slot];
    if (down && !s->down)
        call_guest((void *)fn_down, pad, slot, x, y);
    else if (down && (s->x != x || s->y != y))
        call_guest((void *)fn_move, pad, slot, x, y);
    else if (!down && s->down)
        call_guest((void *)fn_up, pad, slot, s->x, s->y);
    if (down || s->down)
        s->x = x, s->y = y;
    s->down = down;
}

static int axis(int v) {
    v -= 128;
    return abs(v) < cfg.deadzone ? 0 : v > 127 ? 127 : v;
}

// ---------------------------------------------------------------- controls.txt

#define CONTROLS_PATH DATA_PATH "controls.txt"

static void write_default_controls(void) {
    FILE *f = fopen(CONTROLS_PATH, "w");
    if (!f)
        return;
    fprintf(f, "# Brothers In Arms: Hour of Heroes - Vita controls (delete this file to restore defaults)\n"
               "# BUTTON = x,y   tap point in the 480x320 game screen (x = -1: unmapped)\n"
               "# LSTICK = x,y,radius   virtual move joystick; RSTICK = x,y,speed   look drag\n"
               "# The log lists every on-screen element as '[INPUT] element ... rect' to help remapping.\n");
    for (int i = 0; i < NBUTTONS; i++)
        fprintf(f, "%s = %d,%d\n", buttons[i].name, buttons[i].x, buttons[i].y);
    fprintf(f, "LSTICK = %d,%d,%d\nRSTICK = %d,%d,%d\nDEADZONE = %d\n", cfg.lx, cfg.ly, cfg.lr, cfg.rx, cfg.ry,
            cfg.rspeed, cfg.deadzone);
    fclose(f);
}

static void load_controls(void) {
    FILE *f = fopen(CONTROLS_PATH, "r");
    if (!f) {
        write_default_controls();
        l_info("[INPUT] %s missing: defaults written", CONTROLS_PATH);
        return;
    }
    char line[128], key[32];
    int a, b, c, n = 0;
    while (fgets(line, sizeof(line), f)) {
        int args = line[0] == '#' ? 0 : sscanf(line, " %31[A-Z] = %d , %d , %d", key, &a, &b, &c);
        if (args < 2)
            continue;
        n++;
        if (!strcmp(key, "DEADZONE"))
            cfg.deadzone = a;
        else if (!strcmp(key, "LSTICK") && args == 4)
            cfg.lx = a, cfg.ly = b, cfg.lr = c;
        else if (!strcmp(key, "RSTICK") && args == 4)
            cfg.rx = a, cfg.ry = b, cfg.rspeed = c;
        else
            for (int i = 0; i < NBUTTONS; i++)
                if (!strcmp(key, buttons[i].name) && args >= 3)
                    buttons[i].x = a, buttons[i].y = b;
    }
    fclose(f);
    l_info("[INPUT] %s: %d entries (deadzone %d)", CONTROLS_PATH, n, cfg.deadzone);
}

// ---------------------------------------------------------------- options.c: button editor and hidden HUD
// One menu row per distinct tap point of the table (an "action"); its buttons are the ones tapping there.
static struct {
    int x, y;
    char label[32];
} acts[NBUTTONS];
static int nacts;

static void build_actions(void) {
    static const struct {
        int x, y;
        const char *label;
    } known[] = {{413, 261, "Fire (right stick)"}, {453, 27, "Top-right button"}, {174, 16, "Top bar button"}};
    nacts = 0;
    for (int i = 0; i < NBUTTONS; i++) {
        int k = 0;
        while (k < nacts && (acts[k].x != buttons[i].x || acts[k].y != buttons[i].y))
            k++;
        if (buttons[i].x < 0 || k < nacts)
            continue;
        acts[nacts].x = buttons[i].x, acts[nacts].y = buttons[i].y;
        snprintf(acts[nacts].label, sizeof(acts[nacts].label), "HUD button %d,%d", buttons[i].x, buttons[i].y);
        for (unsigned j = 0; j < sizeof(known) / sizeof(known[0]); j++)
            if (known[j].x == buttons[i].x && known[j].y == buttons[i].y)
                snprintf(acts[nacts].label, sizeof(acts[nacts].label), "%s", known[j].label);
        nacts++;
    }
}

int input_bind_count(void) { return nacts; }
const char *input_bind_label(int i) { return acts[i].label; }
uint32_t input_bind_mask(int i) {
    uint32_t m = 0;
    for (int b = 0; b < NBUTTONS; b++)
        if (buttons[b].x == acts[i].x && buttons[b].y == acts[i].y)
            m |= buttons[b].mask;
    return m;
}
void input_bind_set(int i, uint32_t mask) { // unmapped D-pad directions drive the left stick again
    for (int b = 0; b < NBUTTONS; b++)
        if (mask & buttons[b].mask)
            buttons[b].x = acts[i].x, buttons[b].y = acts[i].y;
        else if (buttons[b].x == acts[i].x && buttons[b].y == acts[i].y)
            buttons[b].x = buttons[b].y = -1;
}
void input_save_controls(void) {
    write_default_controls(); // writes the table as it is now
    l_info("[INPUT] %s saved from the options menu", CONTROLS_PATH);
}

// Tap points + move stick (landscape 480x320) -> the portrait FBO, normalized (see uv4/uv2 in gles.c).
int input_hud_points(float (*pts)[4], int max) {
    int n = 0;
    for (int i = 0; i <= nacts && n < max; i++, n++) {
        float sx = (i < nacts ? acts[i].x : cfg.lx) / (float)GAME_W, sy = (i < nacts ? acts[i].y : cfg.ly) / (float)GAME_H;
        pts[n][0] = gles_rot180 ? sy : 1 - sy;
        pts[n][1] = gles_rot180 ? 1 - sx : sx;
        // move stick: zone ±1.6 radii (axes swap with the portrait FBO); HUD buttons: point rule
        pts[n][2] = i < nacts ? 0 : 1.6f * cfg.lr / GAME_H, pts[n][3] = i < nacts ? 0 : 1.6f * cfg.lr / GAME_W;
    }
    return n;
}

// ---------------------------------------------------------------- init / poll

void input_init(void) {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceMotionStartSampling();
    load_controls();
    build_actions();
}

void input_bind(void) {
    fn_down = macho_sym("__ZN9CTouchPad14EventTouchDownEiii");
    fn_move = macho_sym("__ZN9CTouchPad14EventTouchMoveEiii");
    fn_up = macho_sym("__ZN9CTouchPad12EventTouchUpEiii");
    pad_ptr = macho_sym("__ZN9CTouchPad15s_touchPadWriteE");
    fn_tilt = macho_sym("__ZN9CTouchPad12EventSetTiltEiii");
    fn_tilt_acc = macho_sym("__ZN9CTouchPad24EventSetTiltAccelerationEiii");
    if (!fn_down || !fn_move || !fn_up || !pad_ptr) {
        l_error("[INPUT] CTouchPad symbols missing (down=%p move=%p up=%p pad=%p): no input", (void *)fn_down,
                (void *)fn_move, (void *)fn_up, (void *)pad_ptr);
        fn_down = 0;
    } else {
        l_info("[INPUT] CTouchPad bound (s_touchPadWrite at %p)", (void *)pad_ptr);
    }
}

void input_poll(void) {
    if (!fn_down)
        return;
    if (*(uint32_t *)pad_ptr)
        tilt_poll(*(uint32_t *)pad_ptr);
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    options_poll(pad.buttons); // SELECT+START: options menu; L+R: on-screen controls
    pad.buttons = options_filter_buttons(pad.buttons); // L/R held back briefly: L+R must not reach the game
    SceTouchData t;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) < 1)
        t.reportNum = 0;

    // Front touch (1920x1088 units) -> the screen rectangle of the 480x320 image (full screen or aspect, gles.c).
    int dx, dy, dw, dh;
    gles_dest_rect(&dx, &dy, &dw, &dh);
    for (int i = 0; i < 2; i++) {
        int down = i < (int)t.reportNum;
        slot_set(SLOT_FINGER0 + i, down, down ? (t.report[i].x / 2 - dx) * GAME_W / dw : 0,
                 down ? (t.report[i].y / 2 - dy) * GAME_H / dh : 0);
    }

    // Left stick (+ unmapped D-pad) -> drag inside the move joystick.
    int lx = axis(pad.lx), ly = axis(pad.ly);
    if (buttons[B_UP].x < 0 && (pad.buttons & SCE_CTRL_UP)) ly = -127;
    if (buttons[B_DOWN].x < 0 && (pad.buttons & SCE_CTRL_DOWN)) ly = 127;
    if (buttons[B_LEFT].x < 0 && (pad.buttons & SCE_CTRL_LEFT)) lx = -127;
    if (buttons[B_RIGHT].x < 0 && (pad.buttons & SCE_CTRL_RIGHT)) lx = 127;
    slot_set(SLOT_LSTICK, lx || ly, cfg.lx + lx * cfg.lr / 127, cfg.ly + ly * cfg.lr / 127);

    // Right stick -> continuous look drag; re-grab at the start point when the finger would leave its box.
    int rx = axis(pad.rx), ry = axis(pad.ry);
    if (rx || ry) {
        slot_state *s = &slots[SLOT_RSTICK];
        int x = (s->down ? s->x : cfg.rx) + rx * cfg.rspeed / 127, y = (s->down ? s->y : cfg.ry) + ry * cfg.rspeed / 127;
        if (abs(x - cfg.rx) > 100 || abs(y - cfg.ry) > 80) {
            slot_set(SLOT_RSTICK, 0, 0, 0);
            x = cfg.rx, y = cfg.ry;
        }
        slot_set(SLOT_RSTICK, 1, x, y);
    } else {
        slot_set(SLOT_RSTICK, 0, 0, 0);
    }

    // Buttons -> tap on slot 4; the first held mapped button in table order wins.
    int p = -1;
    for (int i = 0; i < NBUTTONS && p < 0; i++)
        if ((pad.buttons & buttons[i].mask) && buttons[i].x >= 0)
            p = i;
    slot_state *b = &slots[SLOT_BUTTON];
    if (p >= 0 && b->down && (b->x != buttons[p].x || b->y != buttons[p].y))
        slot_set(SLOT_BUTTON, 0, 0, 0); // switching buttons: lift first so the game sees a new tap
    slot_set(SLOT_BUTTON, p >= 0, p >= 0 ? buttons[p].x : 0, p >= 0 ? buttons[p].y : 0);
}
