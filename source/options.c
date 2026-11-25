// Menú de opciones (SELECT+START), al estilo del de Ferrari-GT-Evolution-vita / Zenonia2-vita: panel con la fuente
// 8x8 de PSPSDK dibujado en una textura translúcida sobre el juego, que queda en pausa (gles_present no vuelve hasta
// cerrar). Filas: una por acción de controls.txt (X: apretar el botón nuevo, triángulo: sin botón; si el botón ya lo
// usa otra acción, se intercambian), controles en pantalla (L+R), tamaño, filtro, MSAA, FPS, límite de FPS, rotación.
// Se guarda en DATA_PATH options.txt (los botones en controls.txt), editable a mano:
//     filter=sharp      (bilinear | sharp | pixel; pixel solo si PORT_2D)
//     msaa=0            (0 | 2 | 4; se aplica al reiniciar)
//     stretch=0         (1 = pantalla completa)
//     show_fps=0  fps_cap=0 (0 | 30)  hud_hidden=0  rotation=1 (si el port gira la imagen)
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <string.h>
#include <vitaGL.h>

#include "hle.h"
#include "log.h"
#include "options.h"

#ifndef PORT_2D
#define PORT_2D 0 // port_config.h: 1 = juego 2D, habilita el filtro "pixel" (vecino más cercano)
#endif
#ifndef PORT_STRETCH
#define PORT_STRETCH 0
#endif
#ifdef PORT_ROTATION
extern int port_rotation;
#define HAS_ROT 1
#else
static int port_rotation;
#define HAS_ROT 0
#endif
#define OPTIONS_PATH DATA_PATH "options.txt"

extern const unsigned char font8x8[];

int opt_filter = FILTER_SHARP, opt_msaa = 0, opt_msaa_active = 0, opt_stretch = PORT_STRETCH;
int opt_show_fps = 0, opt_fps_cap = 0, opt_hud_hidden = 0;
static volatile int open_req, active;

static const char *filter_names[] = {"bilinear", "sharp", "pixel"};
static const char *filter_labels[] = {"BILINEAR (SMOOTH)", "SHARP BILINEAR", "PIXEL (NEAREST)"};
static const char *rot_labels[] = {"NONE", "LANDSCAPE RIGHT", "LANDSCAPE LEFT"};

void options_load(void) {
    FILE *f = fopen(OPTIONS_PATH, "r");
    if (f) {
        char line[64], s[16];
        int v;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "filter=%15s", s) == 1) {
                for (int i = 0; i < 3; i++)
                    if (!strcmp(s, filter_names[i]))
                        opt_filter = i;
            } else if (sscanf(line, "msaa=%d", &v) == 1 && (v == 0 || v == 2 || v == 4))
                opt_msaa = v;
            else if (sscanf(line, "rotation=%d", &v) == 1 && v >= 0 && v <= 2)
                port_rotation = HAS_ROT ? v : port_rotation;
            else if (sscanf(line, "stretch=%d", &v) == 1)
                opt_stretch = !!v;
            else if (sscanf(line, "show_fps=%d", &v) == 1)
                opt_show_fps = !!v;
            else if (sscanf(line, "fps_cap=%d", &v) == 1)
                opt_fps_cap = v == 30 ? 30 : 0;
            else if (sscanf(line, "hud_hidden=%d", &v) == 1)
                opt_hud_hidden = !!v;
        }
        fclose(f);
    }
    if (opt_filter == FILTER_PIXEL && !PORT_2D)
        opt_filter = FILTER_SHARP;
    opt_msaa_active = opt_msaa;
    l_info("options.txt: filtro %s, MSAA %dx, pantalla completa %d, fps %d (límite %d), controles ocultos %d",
           filter_names[opt_filter], opt_msaa, opt_stretch, opt_show_fps, opt_fps_cap, opt_hud_hidden);
}

static void options_save(void) {
    FILE *f = fopen(OPTIONS_PATH, "w");
    if (!f)
        return;
    fprintf(f, "filter=%s\nmsaa=%d\nstretch=%d\nshow_fps=%d\nfps_cap=%d\nhud_hidden=%d\n", filter_names[opt_filter],
            opt_msaa, opt_stretch, opt_show_fps, opt_fps_cap, opt_hud_hidden);
    if (HAS_ROT)
        fprintf(f, "rotation=%d\n", port_rotation);
    fclose(f);
}

int options_msaa_mode(void) {
    return opt_msaa == 4 ? SCE_GXM_MULTISAMPLE_4X : opt_msaa == 2 ? SCE_GXM_MULTISAMPLE_2X : SCE_GXM_MULTISAMPLE_NONE;
}

int options_pending(void) { return open_req; }
int options_active(void) { return active; }

void options_poll(uint32_t b) {
    static uint32_t prev;
    const uint32_t menu = SCE_CTRL_SELECT | SCE_CTRL_START, lr = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
    if ((b & menu) == menu && (prev & menu) != menu)
        open_req = 1;
    if ((b & lr) == lr && (prev & lr) != lr) {
        opt_hud_hidden = !opt_hud_hidden;
        options_save();
        l_info("controles en pantalla %s (L+R)", opt_hud_hidden ? "ocultos" : "visibles");
    }
    prev = b;
}

// L+R alterna los controles en pantalla; L y R suelen ser acciones del juego (freno, giro, sacudón...). Un L o R
// recién apretado solo se entrega al juego si en LR_WAIT_US no llega el otro; con los dos juntos, ninguno hasta
// soltar ambos. ponytail: 80 ms de retardo en L/R solos; bajarlo si en consola se nota en el freno/acelerador.
#define LR_WAIT_US 80000
uint32_t options_filter_buttons(uint32_t b) {
    static uint64_t since;
    static int combo;
    const uint32_t lr = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
    if ((b & lr) == lr)
        combo = 1;
    else if (!(b & lr))
        combo = 0, since = 0;
    if (combo)
        return b & ~lr;
    if (b & lr) {
        uint64_t now = sceKernelGetProcessTimeWide();
        if (!since)
            since = now;
        if (now - since < LR_WAIT_US)
            return b & ~lr;
    }
    return b;
}

// ---------------------------------------------------------------- texto en texturas (fuente 8x8)

#define C_BG 0xD0201008 // ABGR (GL_RGBA/UNSIGNED_BYTE en little endian); alfa = translúcido
#define C_SEL 0xFF603818
#define C_TEXT 0xFFE0E0E0
#define C_DIM 0xFF909090
#define C_HEAD 0xFF40C0FF
#define C_EDIT 0xFF40FFFF

static void fill(uint32_t *px, int w, int y, int h, uint32_t c) {
    for (int i = y * w; i < (y + h) * w; i++)
        px[i] = c;
}
static void text(uint32_t *px, int w, int x, int y, uint32_t c, const char *s) {
    for (; *s && x + 8 <= w; s++, x += 8)
        for (int r = 0; r < 8; r++) {
            unsigned char bits = font8x8[(unsigned char)*s * 8 + r];
            for (int col = 0; col < 8; col++)
                if (bits & (0x80 >> col))
                    px[(y + r) * w + x + col] = c;
        }
}
// Textura w x h dibujada en (x, y, sw, sh) de la pantalla; la proyección del present es 0..960 x 0..544, y hacia abajo.
static void blit(GLuint *tex, const uint32_t *px, int w, int h, float x, float y, float sw, float sh) {
    if (!*tex) {
        glGenTextures(1, tex);
        glBindTexture(GL_TEXTURE_2D, *tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    const float xy[] = {x, y, x + sw, y, x, y + sh, x + sw, y + sh};
    static const float uv[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glVertexPointer(2, GL_FLOAT, 0, xy);
    glTexCoordPointer(2, GL_FLOAT, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------- botones de la Vita

static const struct {
    const char *name;
    uint32_t mask;
} pads[] = {
    {"CROSS", SCE_CTRL_CROSS},  {"CIRCLE", SCE_CTRL_CIRCLE}, {"SQUARE", SCE_CTRL_SQUARE}, {"TRIANGLE", SCE_CTRL_TRIANGLE},
    {"L", SCE_CTRL_LTRIGGER},   {"R", SCE_CTRL_RTRIGGER},    {"UP", SCE_CTRL_UP},         {"DOWN", SCE_CTRL_DOWN},
    {"LEFT", SCE_CTRL_LEFT},    {"RIGHT", SCE_CTRL_RIGHT},   {"SELECT", SCE_CTRL_SELECT}, {"START", SCE_CTRL_START},
};
#define NPADS (int)(sizeof(pads) / sizeof(pads[0]))
static const char *mask_str(uint32_t m) {
    static char out[96];
    out[0] = 0;
    for (int i = 0; i < NPADS; i++)
        if (m & pads[i].mask) {
            if (out[0])
                strcat(out, "+");
            strcat(out, pads[i].name);
        }
    return out[0] ? out : "(NONE)";
}

static uint32_t pad(void) {
    SceCtrlData c;
    sceCtrlPeekBufferPositive(0, &c, 1);
    return c.buttons;
}

// ---------------------------------------------------------------- panel

enum { R_HUD, R_SIZE, R_FILTER, R_MSAA, R_FPS, R_CAP, R_ROT, R_CLOSE, R_FIXED };
#define PW 480
#define PH 272
static uint32_t px[PW * PH];
static GLuint panel_tex;
static int cursor, capture, binds_dirty;

#ifndef PORT_NO_FILTER
#define PORT_NO_FILTER 0 // 1 = el drawable es más grande que la pantalla: "sharp" sería igual a bilineal
#endif
static int row_shown(int o) { return (o != R_ROT || HAS_ROT) && (o != R_FILTER || !PORT_NO_FILTER); }
static int nrows(void) {
    int n = input_bind_count();
    for (int o = 0; o < R_FIXED; o++)
        n += row_shown(o);
    return n;
}
static int fixed_row(int r) { // fila -> R_*, o -1 si es una acción (las filas que no aplican no se muestran)
    int o = r - input_bind_count();
    if (o < 0)
        return -1;
    for (int k = 0; k < R_FIXED; k++)
        if (row_shown(k) && o-- == 0)
            return k;
    return R_CLOSE;
}

static void render(void) {
    static const char *labels[R_FIXED] = {"On-screen controls (L+R)", "Screen size", "Screen filter",
                                          "Anti-aliasing (MSAA)", "FPS counter", "FPS limit", "Rotation",
                                          "Save & close"};
    int rows = nrows(), nb = input_bind_count();
    char lab[64], val[64];
    fill(px, PW, 0, PH, C_BG);
    text(px, PW, 16, 8, C_HEAD, "OPTIONS");
    for (int r = 0; r < rows; r++) {
        int rh = rows > 16 ? 12 : 13, y = 24 + r * rh, o = fixed_row(r); // filas hasta y = 246 (ayuda abajo)
        if (r == cursor)
            fill(px, PW, y - 3, rh, C_SEL);
        uint32_t c = C_TEXT;
        val[0] = 0;
        if (o < 0) {
            const char *l = input_bind_label(r);
            snprintf(lab, sizeof(lab), "%.24s", l && l[0] ? l : "Touch");
            if (r == cursor && capture)
                strcpy(val, "PRESS A BUTTON..."), c = C_EDIT;
            else
                snprintf(val, sizeof(val), "%s", mask_str(input_bind_mask(r)));
        } else {
            strcpy(lab, labels[o]);
            if (o == R_HUD)
                strcpy(val, opt_hud_hidden ? "HIDDEN" : "SHOWN");
            else if (o == R_SIZE)
                strcpy(val, opt_stretch ? "FULL SCREEN" : "ORIGINAL ASPECT");
            else if (o == R_FILTER)
                strcpy(val, filter_labels[opt_filter]);
            else if (o == R_MSAA)
                snprintf(val, sizeof(val), "%s%s", opt_msaa == 4 ? "4X" : opt_msaa == 2 ? "2X" : "OFF",
                         opt_msaa != opt_msaa_active ? " (RESTART)" : "");
            else if (o == R_FPS)
                strcpy(val, opt_show_fps ? "ON" : "OFF");
            else if (o == R_CAP)
                strcpy(val, opt_fps_cap ? "30 FPS" : "OFF (GAME)");
            else if (o == R_ROT)
                strcpy(val, rot_labels[port_rotation]);
        }
        text(px, PW, 16, y, o == R_CLOSE ? C_HEAD : C_TEXT, lab);
        text(px, PW, 232, y, c, val);
    }
    const char *help = cursor < nb ? (capture ? "Press the new button   START: cancel"
                                              : "X: change button   TRIANGLE: no button   O: close")
                                   : "LEFT/RIGHT or X: change   O: save & close";
    text(px, PW, 16, PH - 26, C_DIM, help);
    text(px, PW, 16, PH - 13, C_DIM, opt_msaa != opt_msaa_active ? "MSAA applies after restarting the game"
                                                                  : "L+R in game: show/hide on-screen controls");
}

static void change(int o, int d) {
    if (o == R_HUD)
        opt_hud_hidden = !opt_hud_hidden;
    else if (o == R_SIZE)
        opt_stretch = !opt_stretch;
    else if (o == R_FILTER) {
        int n = PORT_2D ? 3 : 2;
        opt_filter = (opt_filter + d + n) % n;
    } else if (o == R_MSAA) {
        static const int v[] = {0, 2, 4};
        opt_msaa = v[(opt_msaa / 2 + d + 3) % 3];
    } else if (o == R_FPS)
        opt_show_fps = !opt_show_fps;
    else if (o == R_CAP)
        opt_fps_cap = opt_fps_cap ? 0 : 30;
    else if (o == R_ROT)
        port_rotation = (port_rotation + d + 3) % 3;
}

// Botón nuevo para la acción i; si otra acción ya lo usa, recibe el botón viejo de esta (intercambio).
static void assign(int i, uint32_t m) {
    uint32_t old = input_bind_mask(i);
    for (int j = 0; j < input_bind_count(); j++)
        if (j != i && (input_bind_mask(j) & m))
            input_bind_set(j, (input_bind_mask(j) & ~m) | old);
    input_bind_set(i, m);
    binds_dirty = 1;
}

void options_run(void (*draw_game)(void)) {
    open_req = 0, active = 1, capture = 0, binds_dirty = 0;
    uint32_t prev = pad(); // SELECT+START siguen apretados
    l_info("menú de opciones abierto");
    for (;;) {
        uint32_t b = pad(), p = b & ~prev;
        prev = b;
        int rows = nrows(), o = fixed_row(cursor);
        if (capture) {
            if (p & SCE_CTRL_START)
                capture = 0;
            else
                for (int i = 0; i < NPADS; i++)
                    if (p & pads[i].mask) {
                        assign(cursor, pads[i].mask);
                        capture = 0;
                        break;
                    }
        } else if (p & SCE_CTRL_CIRCLE || p & SCE_CTRL_START || (p & SCE_CTRL_CROSS && o == R_CLOSE))
            break;
        else if (p & SCE_CTRL_UP)
            cursor = (cursor + rows - 1) % rows;
        else if (p & SCE_CTRL_DOWN)
            cursor = (cursor + 1) % rows;
        else if (o < 0 && p & SCE_CTRL_CROSS)
            capture = 1;
        else if (o < 0 && p & SCE_CTRL_TRIANGLE)
            input_bind_set(cursor, 0), binds_dirty = 1;
        else if (o >= 0 && p & (SCE_CTRL_RIGHT | SCE_CTRL_CROSS))
            change(o, 1);
        else if (o >= 0 && p & SCE_CTRL_LEFT)
            change(o, -1);
        draw_game(); // filtro/tamaño/rotación nuevos en vivo
        render();
        blit(&panel_tex, px, PW, PH, 0, 0, 960, 544);
        vglSwapBuffers(GL_FALSE);
    }
    options_save();
    if (binds_dirty)
        input_save_controls();
    l_info("opciones: filtro %s, MSAA %dx (activo %dx), pantalla completa %d, fps %d (límite %d), controles ocultos %d",
           filter_names[opt_filter], opt_msaa, opt_msaa_active, opt_stretch, opt_show_fps, opt_fps_cap, opt_hud_hidden);
    // no devolver el botón de cierre al juego (input.c lo leería como toque)
    while (pad()) {
        draw_game();
        vglSwapBuffers(GL_FALSE);
    }
    active = 0;
    draw_game(); // lo que presenta el swap de gles_present al volver
}

// ---------------------------------------------------------------- FPS (contador y límite)

static int frame3d, last3d; // options_hud_filter: el frame tuvo dibujos en perspectiva (partida, no un menú 2D)

void options_frame(void) {
    static uint64_t t0, last;
    last3d = frame3d, frame3d = 0;
    static int frames, shown;
    static GLuint fps_tex;
    static uint32_t fps_px[64 * 8];
    uint64_t now = sceKernelGetProcessTimeWide();
    frames++;
    if (!t0)
        t0 = now;
    else if (now - t0 >= 1000000) {
        shown = (int)(frames * 1e6 / (now - t0) + 0.5);
        frames = 0, t0 = now;
    }
    if (opt_show_fps) {
        char s[16];
        snprintf(s, sizeof(s), "%d FPS", shown);
        fill(fps_px, 64, 0, 8, 0xA0000000);
        text(fps_px, 64, 0, 0, 0xFF40FF40, s);
        blit(&fps_tex, fps_px, 64, 8, 4, 4, 128, 16);
    }
    if (opt_fps_cap) {
        uint64_t frame_us = 1000000 / opt_fps_cap;
        if (last && now - last < frame_us)
            sceKernelDelayThread(frame_us - (now - last));
        last = sceKernelGetProcessTimeWide();
    }
}

// ---------------------------------------------------------------- controles en pantalla ocultos (L+R)
// Ocultar = opacidad 0 %: el dibujo se hace igual pero sin escribir color (glColorMask apagado), así profundidad y
// stencil quedan como el juego los espera. Sin saber qué textura es el HUD de cada juego: va invisible un rectángulo 2D (proyección ortográfica) de menos de
// HUD_MAX del drawable por lado que contiene un punto que toca un botón (controls.txt), o cualquier pieza 2D que cabe
// entera en la zona de un pad táctil (STICK: cruceta = base + flechas que no tocan el centro). Si el juego manda
// varios rectángulos en una tanda (GL_TRIANGLES de a 6 índices = un quad), se decide quad por quad y se dibuja el resto
// con índices propios. Solo en frames que también dibujaron 3D: en un menú 2D los botones en esos puntos son del menú.
// Los toques siguen funcionando (la entrada no depende del dibujo).
// ponytail: tiras (TRIANGLE_STRIP) de varios quads se deciden enteras; calibrar con el log "HUD" en consola.
#define HUD_MAX 0.3f
#define HUD_MAX_IDX 3072
static int hud_logs;
static uint16_t hud_idx[HUD_MAX_IDX], hud_hid[HUD_MAX_IDX];
static int hud_nhid;

static float comp(const uint8_t *e, unsigned type, int c) {
    switch (type) {
    case GL_FLOAT:
        return ((const float *)e)[c];
    case GL_FIXED:
        return ((const int32_t *)e)[c] / 65536.0f;
    case GL_SHORT:
        return ((const int16_t *)e)[c];
    default:
        return ((const int8_t *)e)[c];
    }
}

static struct {
    GLfloat pm[16], mv[16];
    const uint8_t *vtx;
    unsigned type;
    int size, stride;
    float pts[16][4]; // x, y, media zona x, y (0 = botón: regla del punto)
    int npts;
} hc;

static int vertex_index(const void *idx, unsigned itype, int first, int k) {
    return !idx ? first + k
         : itype == GL_UNSIGNED_BYTE ? ((const uint8_t *)idx)[k]
         : itype == GL_UNSIGNED_SHORT ? ((const uint16_t *)idx)[k] : (int)((const uint32_t *)idx)[k];
}

// 1 si los vértices [k0, k0+n) forman un rectángulo chico sobre un punto de botón.
static int hud_hit(const void *idx, unsigned itype, int first, int k0, int n, float *box) {
    const GLfloat *pm = hc.pm, *mv = hc.mv;
    float x0 = 2, y0 = 2, x1 = -1, y1 = -1;
    for (int k = k0; k < k0 + n; k++) {
        const uint8_t *e = hc.vtx + vertex_index(idx, itype, first, k) * hc.stride;
        float vx = comp(e, hc.type, 0), vy = comp(e, hc.type, 1), vz = hc.size > 2 ? comp(e, hc.type, 2) : 0;
        float ex = mv[0] * vx + mv[4] * vy + mv[8] * vz + mv[12], ey = mv[1] * vx + mv[5] * vy + mv[9] * vz + mv[13];
        float ez = mv[2] * vx + mv[6] * vy + mv[10] * vz + mv[14], ew = mv[3] * vx + mv[7] * vy + mv[11] * vz + mv[15];
        float cx = pm[0] * ex + pm[4] * ey + pm[8] * ez + pm[12] * ew;
        float cy = pm[1] * ex + pm[5] * ey + pm[9] * ez + pm[13] * ew, cw = pm[15] * ew;
        if (cw == 0)
            return 0;
        float nx = (cx / cw + 1) / 2, ny = (1 - cy / cw) / 2;
        x0 = nx < x0 ? nx : x0, x1 = nx > x1 ? nx : x1, y0 = ny < y0 ? ny : y0, y1 = ny > y1 ? ny : y1;
        if (x1 - x0 > 0.6f || y1 - y0 > 0.6f) // más grande que cualquier zona: no es un control
            return 0;
    }
    box[0] = x0, box[1] = y0, box[2] = x1, box[3] = y1;
    int small = x1 - x0 <= HUD_MAX && y1 - y0 <= HUD_MAX, near = 0;
    for (int i = 0; i < hc.npts; i++) {
        const float *p = hc.pts[i];
        if (p[2] > 0 && x0 >= p[0] - p[2] && x1 <= p[0] + p[2] && y0 >= p[1] - p[3] && y1 <= p[1] + p[3])
            return 1; // dentro de la zona del pad
        if (small && p[0] >= x0 - 0.01f && p[0] <= x1 + 0.01f && p[1] >= y0 - 0.01f && p[1] <= y1 + 0.01f)
            return 1;
        float cx = (x0 + x1) / 2 - p[0], cy = (y0 + y1) / 2 - p[1];
        near |= cx * cx + cy * cy < 0.15f * 0.15f;
    }
    static int near_logs;
    if (near && near_logs < 30) // ponytail: diagnóstico de lo que queda visible junto a un control; quitar al calibrar
        near_logs++, l_info("HUD visible cerca de un control: (%.2f,%.2f)-(%.2f,%.2f), %d vértices", x0, y0, x1, y1, n);
    return 0;
}

int options_hud_filter(unsigned mode, const void *vtx, unsigned type, int size, int stride, const void *idx,
                       unsigned itype, int first, int count, const uint16_t **out) {
    if (!opt_hud_hidden || count <= 0)
        return -1;
    glGetFloatv(GL_PROJECTION_MATRIX, hc.pm);
    if (hc.pm[3] != 0 || hc.pm[7] != 0 || hc.pm[11] != 0) { // perspectiva: 3D, no es HUD
        frame3d = 1;
        return -1;
    }
    if (!last3d || !vtx || size < 2 || count > HUD_MAX_IDX)
        return -1;
    glGetFloatv(GL_MODELVIEW_MATRIX, hc.mv);
    int esz = type == GL_FLOAT || type == GL_FIXED ? 4 : type == GL_SHORT ? 2 : 1;
    hc.vtx = vtx, hc.type = type, hc.size = size, hc.stride = stride ? stride : size * esz;
    hc.npts = input_hud_points(hc.pts, 16);
    float box[4];
    if (mode != GL_TRIANGLES || count % 6 || count == 6) { // un solo rectángulo (o forma desconocida): entero
        int skip = hud_hit(idx, itype, first, 0, count, box);
        if (skip && hud_logs < 40)
            hud_logs++, l_info("HUD oculto: draw (%.2f,%.2f)-(%.2f,%.2f)", box[0], box[1], box[2], box[3]);
        return skip ? 0 : -1;
    }
    int n = 0, hidden = 0;
    for (int q = 0; q < count; q += 6) {
        int hit = hud_hit(idx, itype, first, q, 6, box);
        if (hit && hud_logs < 40)
            hud_logs++, l_info("HUD oculto: quad (%.2f,%.2f)-(%.2f,%.2f) de una tanda de %d", box[0], box[1], box[2],
                               box[3], count / 6);
        for (int k = q; k < q + 6; k++) {
            int v = vertex_index(idx, itype, first, k);
            if (v > 0xffff)
                return -1;
            if (hit)
                hud_hid[hidden++] = v;
            else
                hud_idx[n++] = v;
        }
    }
    if (!hidden)
        return -1;
    hud_nhid = hidden;
    *out = hud_idx;
    return n; // 0 = toda la tanda invisible
}

// Opacidad 0 % para el glDraw* que sigue (on = 1) y vuelta a la máscara del juego (on = 0).
void options_hud_mask(int on) {
    static GLboolean m[4];
    if (on) {
        glGetBooleanv(GL_COLOR_WRITEMASK, m);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    } else {
        glColorMask(m[0], m[1], m[2], m[3]);
    }
}

// Tanda partida: los quads visibles normal y los ocultos con opacidad 0 % (índices de cliente).
void options_hud_draw(const uint16_t *idx, int n) {
    GLint eb = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &eb);
    if (eb)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_SHORT, idx);
    options_hud_mask(1);
    glDrawElements(GL_TRIANGLES, hud_nhid, GL_UNSIGNED_SHORT, hud_hid);
    options_hud_mask(0);
    if (eb)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eb);
}
