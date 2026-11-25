/**
 * @file gles.c
 * @brief OpenGL ES 1.1 pipeline translation over vitaGL.
 * @details The game renders into a virtual 320x480 portrait renderbuffer created on iOS by EAGLView
 *          (with internal projection rotation to landscape). Here that renderbuffer is an FBO with a texture
 *          of the same size: viewport, scissor, and glCopyTexImage2D remain in iOS coordinate space untouched.
 *          During presentation (gles_present), the FBO is drawn rotated and scaled onto the native 960x544 screen.
 *
 *          All imported gl* functions take only integer/pointer arguments (*x variants): no SoftFP issues.
 *          GL_FIXED vertex arrays are passed to vitaGL as-is (it converts them in the shader).
 */
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>

#include "gles.h"
#include "hle.h"
#include "imports.h"
#include "log.h"
#include "options.h"
#include "objc.h"
#include "macho.h"

#define FB_W 320
#define FB_H 480
#define SCR_W 960
#define SCR_H 544
// Full screen stretch (as in Hero of Sparta II) or original aspect (816x544 centered): options menu (options.c),
// gles_dest_rect; input.c maps touches through the same rectangle.

#define GL_FRAMEBUFFER_COMPLETE_OES 0x8CD5
#define GL_RENDERBUFFER_WIDTH_OES 0x8D42
#define GL_RENDERBUFFER_HEIGHT_OES 0x8D43

static GLuint fbo, fbo_tex;

// Cached state overwritten during blit that glGet cannot fully query:
typedef struct {
    GLint size;
    GLenum type;
    GLsizei stride;
    const void *ptr;
} array_ptr;
static array_ptr vtx, tc[2];
static int client_unit; // glClientActiveTexture - GL_TEXTURE0
static GLint env_mode = GL_MODULATE;
static GLfixed color[4] = {0x10000, 0x10000, 0x10000, 0x10000};

// ---------------------------------------------------------------- vertex arrays
// vitaGL takes GL_FIXED pointers natively (shader-side conversion). The old CPU float conversion
// only ran in glDrawArrays: glDrawElements (the 2D UI lib: RenderRegions, FillRect...) drew with a
// stale pointer, so menu buttons were invisible (log 017). State is mirrored for gles_present.

static void apply_vertex(void) {
    if (vtx.size)
        glVertexPointer(vtx.size, vtx.type, vtx.stride, vtx.ptr);
}

static void apply_texcoord(int unit) {
    array_ptr *a = &tc[unit];
    if (!a->size)
        return;
    glClientActiveTexture(GL_TEXTURE0 + unit);
    glTexCoordPointer(a->size, a->type, a->stride, a->ptr);
    glClientActiveTexture(GL_TEXTURE0 + client_unit);
}

static void g_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) {
    vtx = (array_ptr){size, type, stride, ptr};
    apply_vertex();
}

static void g_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) {
    tc[client_unit] = (array_ptr){size, type, stride, ptr};
    apply_texcoord(client_unit);
}

static void g_glClientActiveTexture(GLenum unit) {
    client_unit = (unit - GL_TEXTURE0) & 1;
    glClientActiveTexture(unit);
}

static uint32_t gles_draw_calls;

// After a cutscene (video_avc.c draws with its own arrays): give vitaGL the guest's pointers back.
void gles_reapply_arrays(void) {
    apply_vertex();
    apply_texcoord(0);
    apply_texcoord(1);
}

uint32_t gles_get_draw_calls(void) {
    return gles_draw_calls;
}

// On-screen controls hidden with L+R (options.c decides from the draw's rectangle on screen). Client arrays only:
// with a buffer object bound (3D meshes) the data is not mirrored here.
static int hud_filter(GLenum mode, const void *idx, GLenum itype, int first, int count, const uint16_t **out) {
    if (!opt_hud_hidden || !vtx.size)
        return -1;
    GLint ab = 0, eb = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &ab);
    if (idx)
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &eb);
    return options_hud_filter(mode, ab || eb ? NULL : vtx.ptr, vtx.type, vtx.size, vtx.stride, idx, itype, first,
                              count, out); // NULL: solo cuenta si el frame tiene 3D
}

static void g_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    gles_draw_calls++;
    const uint16_t *hud_i;
    int hud_n = hud_filter(mode, NULL, 0, first, count, &hud_i);
    (hud_n > 0 ? options_hud_draw(hud_i, hud_n) : hud_n == 0 ? (options_hud_mask(1), glDrawArrays(mode, first, count), options_hud_mask(0)) : glDrawArrays(mode, first, count));
}

static void g_glGetPointerv(GLenum pname, void **out) {
    if (pname == GL_VERTEX_ARRAY_POINTER)
        *out = (void *)vtx.ptr;
    else if (pname == GL_TEXTURE_COORD_ARRAY_POINTER)
        *out = (void *)tc[client_unit].ptr;
    else
        glGetPointerv(pname, out);
}

// ---------------------------------------------------------------- *x state

static void g_glColor4x(GLfixed r, GLfixed g, GLfixed b, GLfixed a) {
    color[0] = r, color[1] = g, color[2] = b, color[3] = a;
    glColor4x(r, g, b, a);
}

static void g_glTexEnvi(GLenum target, GLenum pname, GLint v) {
    if (pname == GL_TEXTURE_ENV_MODE)
        env_mode = v;
    glTexEnvi(target, pname, v);
}

static void g_glTexEnvxv(GLenum target, GLenum pname, const GLfixed *v) {
    if (pname == GL_TEXTURE_ENV_MODE) // enum value, not fixed point
        return g_glTexEnvi(target, pname, v[0]);
    glTexEnvxv(target, pname, (GLfixed *)v);
}

// Texture parameters used by the engine are enums (filters, wrap modes): pass unscaled.
static void g_glTexParameterx(GLenum target, GLenum pname, GLfixed v) { glTexParameteri(target, pname, v); }

static void g_glGetFixedv(GLenum pname, GLfixed *out) {
    float f[16];
    int n = 1;
    if (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX || pname == GL_TEXTURE_MATRIX)
        n = 16;
    else if (pname == GL_CURRENT_COLOR) {
        memcpy(out, color, sizeof(color));
        return;
    }
    glGetFloatv(pname, f);
    for (int i = 0; i < n; i++)
        out[i] = (GLfixed)(f[i] * 65536.0f);
}

static void g_glGetLightxv(GLenum light, GLenum pname, GLfixed *out) { memset(out, 0, 4 * sizeof(GLfixed)); }

// ---------------------------------------------------------------- EAGLView framebuffer emulation

static GLuint next_name = 1;
static void g_glGenNames(GLsizei n, GLuint *ids) {
    for (int i = 0; i < n; i++)
        ids[i] = next_name++;
}
static void g_glBindFramebufferOES(GLenum target, GLuint fb) { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
static void g_glNoop(void) {}
static GLenum g_glCheckFramebufferStatusOES(GLenum target) { return GL_FRAMEBUFFER_COMPLETE_OES; }
static void g_glGetRenderbufferParameterivOES(GLenum target, GLenum pname, GLint *v) {
    *v = pname == GL_RENDERBUFFER_WIDTH_OES ? FB_W : pname == GL_RENDERBUFFER_HEIGHT_OES ? FB_H : 0;
}

// ---------------------------------------------------------------- init / present

void gles_init(void) {
    vglUseTripleBuffering(GL_FALSE);
    // vitaGL takes every pool but these thresholds. sceVideodecInitLibrary needs PHYCONT pages: vglInitExtended
    // leaves none -> 0x80024302 NO_FREE_PHYSICAL_PAGE (runs 1-2, even with 32 MB RAM free). As Hero of Sparta II.
    options_load(); // MSAA is fixed here (vitaGL applies it to the screen and every FBO)
    vglInitWithCustomThreshold(0, SCR_W, SCR_H, 16 * 1024 * 1024, 0, 16 * 1024 * 1024,
                               0x8C6000 /* vitaGL default cdlg threshold */, options_msaa_mode());
    glGenTextures(1, &fbo_tex);
    glBindTexture(GL_TEXTURE_2D, fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FB_W, FB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex, 0);
    // Depth + stencil: gameswf (menu UI) clips with stencil masks (GL_EQUAL on the mask depth).
    // Without a stencil buffer every masked shape failed the test -> no buttons (log 018).
    GLuint rb;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, FB_W, FB_H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
    glBindTexture(GL_TEXTURE_2D, 0);
    glViewport(0, 0, FB_W, FB_H);
    glClearColor(0, 0, 0, 1);
    // Depth/stencil start defined (1.0 / 0): the 2D menus draw with GL_DEPTH_TEST on without ever
    // clearing depth (hosttest: an uninitialised depth buffer hid every quad -> black screen).
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); // RGB565 semantics, see g_glColorMask
    l_info("vitaGL: FBO %ux%u -> screen %ux%u", FB_W, FB_H, SCR_W, SCR_H);
}

// Destination rectangle on screen (input.c maps touches back through it): full screen or 480x320 aspect.
void gles_dest_rect(int *x, int *y, int *w, int *h) {
    int dw = opt_stretch ? SCR_W : SCR_H * FB_H / FB_W; // the FBO is seen rotated: FB_H wide
    *x = (SCR_W - dw) / 2, *y = 0, *w = dw, *h = SCR_H;
}

// ---------------------------------------------------------------- scaling to the screen (options.c filter)
// sharp bilinear: the FBO is enlarged x k with nearest (k = integer factor >= scale) into an intermediate
// FBO, then reduced with bilinear to the screen: crisp pixels without uneven steps.
int gles_rot180; // CTouchPad orientation 2 in the last gles_present (input.c maps HUD points with it)
static GLuint pre_fbo, pre_tex;
static int pre_w, pre_h;
static GLint scr_filter = GL_LINEAR;

static void set_filter(GLuint t, GLint f) {
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
}

// Draws the game's FBO on the screen (fixed-function state already prepared by gles_present).
static void draw_screen(void) {
    int dx, dy, dw, dh;
    gles_dest_rect(&dx, &dy, &dw, &dh);
    int k = opt_filter == FILTER_SHARP ? (dh + FB_W - 1) / FB_W : 1; // seen height = FB_W
    GLuint src = fbo_tex;
    GLint want = opt_filter == FILTER_PIXEL || k > 1 ? GL_NEAREST : GL_LINEAR;
    if (want != scr_filter)
        set_filter(fbo_tex, want), scr_filter = want;
    static const float unit[] = {0, 0, 1, 0, 0, 1, 1, 1};
    if (k > 1) {
        if (!pre_tex) {
            pre_w = FB_W * k, pre_h = FB_H * k;
            glGenTextures(1, &pre_tex), glGenFramebuffers(1, &pre_fbo);
            glBindTexture(GL_TEXTURE_2D, pre_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pre_w, pre_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            set_filter(pre_tex, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, pre_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pre_tex, 0);
            l_info("[GL] sharp bilinear: %dx%d x%d", FB_W, FB_H, k);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, pre_fbo);
        glViewport(0, 0, pre_w, pre_h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof(0, 1, 0, 1, -1, 1); // same orientation as fbo_tex: the rotation UVs below still apply
        glBindTexture(GL_TEXTURE_2D, fbo_tex);
        glVertexPointer(2, GL_FLOAT, 0, unit);
        glTexCoordPointer(2, GL_FLOAT, 0, unit);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glLoadIdentity();
        glOrthof(0, SCR_W, SCR_H, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, SCR_W, SCR_H);
        src = pre_tex;
    }
    glClearColor(0, 0, 0, 1); // side bars: black, not the game's clear color
    glClear(GL_COLOR_BUFFER_BIT);
    // Screen corners -> vertical FBO texture: orientation 4 = top edge of the FBO on the left edge of the
    // screen, 2 = rotated 180 (see gles_present).
    static const float uv4[] = {1, 1, 1, 0, 0, 1, 0, 0}, uv2[] = {0, 0, 0, 1, 1, 0, 1, 1};
    const float xy[] = {dx, dy, dx + dw, dy, dx, dy + dh, dx + dw, dy + dh};
    glBindTexture(GL_TEXTURE_2D, src);
    glVertexPointer(2, GL_FLOAT, 0, xy);
    glTexCoordPointer(2, GL_FLOAT, 0, gles_rot180 ? uv2 : uv4);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}


// Blits the 320x480 FBO to the screen. Every fixed-function state that could alter the blit (fog,
// lighting, culling, stencil, a second texture unit, the texture matrix, write masks) is saved,
// neutralized and restored (from Skater Nation, same Gameloft engine: the game leaves fog/cull on).
void gles_present(void) {
    static const GLenum caps[] = {GL_BLEND,      GL_ALPHA_TEST, GL_SCISSOR_TEST, GL_DEPTH_TEST,
                                  GL_CULL_FACE,  GL_LIGHTING,   GL_FOG,          GL_STENCIL_TEST,
                                  GL_CLIP_PLANE0, GL_TEXTURE_2D};
#define NCAPS (sizeof(caps) / sizeof(caps[0]))
    GLboolean on[NCAPS], tca, va, ca, na, tex1_on, cmask[4], dmask;
    GLint tex, mode, active, vp[4], bound_fb, array_buf;
    GLfloat clear[4];
    for (unsigned i = 0; i < NCAPS; i++)
        on[i] = glIsEnabled(caps[i]);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE1);
    tex1_on = glIsEnabled(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    tca = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
    va = glIsEnabled(GL_VERTEX_ARRAY);
    ca = glIsEnabled(GL_COLOR_ARRAY);
    na = glIsEnabled(GL_NORMAL_ARRAY);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glGetIntegerv(GL_MATRIX_MODE, &mode);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound_fb);
    glGetBooleanv(GL_COLOR_WRITEMASK, cmask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &dmask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buf); // CMesh::RenderVbo keeps VBOs bound

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0); // the blit quad uses client arrays
    glViewport(0, 0, SCR_W, SCR_H);
    for (unsigned i = 0; i < NCAPS - 1; i++)
        glDisable(caps[i]);
    glEnable(GL_TEXTURE_2D);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBindTexture(GL_TEXTURE_2D, fbo_tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrthof(0, SCR_W, SCR_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    // Screen corners (top-left, top-right, bottom-left, bottom-right) -> vertical FBO texture.
    // The game rotates its own rendering by CTouchPad::s_currentDeviceOrientation (initial value 4 in
    // __DATA; TransformCoordinatesByOrientation: 4 -> x'=y, y'=320-x, 2 -> the same turned 180 degrees),
    // like Skater Nation (same Gameloft CTouchPad, its orientation 3 == BIA's 4): 4 = top edge of the FBO
    // on the left edge of the screen, 2 = rotated 180.
    static const int *orient;
    if (!orient)
        orient = (const int *)macho_sym("__ZN9CTouchPad26s_currentDeviceOrientationE");
    gles_rot180 = orient && *orient == 2;
    draw_screen();
    if (options_pending())
        options_run(draw_screen);
    static int presented;
    static uint32_t last_draws;
    static uint64_t t_win;
    if (!presented++)
        l_info("[GL] first frame presented (orientation %d)", orient ? *orient : -1);
    if (presented % 300 == 0) { // perf line: fps + guest draw calls per frame
        uint64_t now = sceKernelGetProcessTimeWide();
        uint8_t px[4] = {0};
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); // centre pixel of the game image: black screen triage
        glReadPixels(FB_W / 2, FB_H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (t_win)
            l_info("[GL] perf: %.1f fps, %u draws/frame, fbo centre %02x%02x%02x", 300e6 / (now - t_win),
                   (gles_draw_calls - last_draws) / 300, px[0], px[1], px[2]);
        t_win = now, last_draws = gles_draw_calls;
    }
    options_frame();
    vglSwapBuffers(ime_active()); // GL_TRUE while system dialog is active

    // Restore guest OpenGL state.
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(mode);
    glBindFramebuffer(GL_FRAMEBUFFER, bound_fb);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glColorMask(cmask[0], cmask[1], cmask[2], GL_FALSE); // RGB565 semantics, see g_glColorMask
    glDepthMask(dmask);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env_mode);
    for (unsigned i = 0; i < NCAPS; i++)
        on[i] ? glEnable(caps[i]) : glDisable(caps[i]);
    if (tex1_on) {
        glActiveTexture(GL_TEXTURE1);
        glEnable(GL_TEXTURE_2D);
    }
    if (!va)
        glDisableClientState(GL_VERTEX_ARRAY);
    if (!tca)
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (ca)
        glEnableClientState(GL_COLOR_ARRAY);
    if (na)
        glEnableClientState(GL_NORMAL_ARRAY);
    glColor4x(color[0], color[1], color[2], color[3]);
    glBindBuffer(GL_ARRAY_BUFFER, array_buf);
    gles_reapply_arrays();
    glClientActiveTexture(GL_TEXTURE0 + client_unit);
    glActiveTexture(active);
#undef NCAPS
}

// iOS draws into a kEAGLColorFormatRGB565 renderbuffer: no alpha, so GL_DST_ALPHA reads as 1.
// RenderState::Send blends with GL_DST_ALPHA / GL_ONE_MINUS_DST_ALPHA; with the RGBA8 FBO the
// character's texture alpha stayed in the buffer and later passes showed it see-through (log 027).
// Keep FBO alpha at 1: never write alpha, always clear it to 1.
static void g_glClearColorx(GLfixed r, GLfixed g, GLfixed b, GLfixed a) {
    glClearColor((float)r / 65536.0f, (float)g / 65536.0f, (float)b / 65536.0f, 1.0f);
}

static void g_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { glClearColor(r, g, b, 1.0f); }

static void g_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { glColorMask(r, g, b, GL_FALSE); }

static void g_glTexEnvx(GLenum target, GLenum pname, GLfixed param) {
    if (pname == GL_TEXTURE_ENV_MODE)
        glTexEnvi(target, pname, param);
    else
        glTexEnvf(target, pname, (float)param / 65536.0f);
}

static void g_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    gles_draw_calls++;
    const uint16_t *hud_i;
    int hud_n = hud_filter(mode, indices, type, 0, count, &hud_i);
    (hud_n > 0 ? options_hud_draw(hud_i, hud_n) : hud_n == 0 ? (options_hud_mask(1), glDrawElements(mode, count, type, indices), options_hud_mask(0)) : glDrawElements(mode, count, type, indices));
}

// glFogx: GL_FOG_MODE takes an enum, the other fog parameters are 16.16 fixed point.
static void g_glFogx(GLenum pname, GLfixed v) {
    glFogf(pname, pname == GL_FOG_MODE ? (GLfloat)v : v / 65536.0f); // GLES 1.1 has no glFogi
}

static void g_glFlush(void) {} // vitaGL flushes on swap; a real glFlush mid-frame stalls (Skater Nation)

static const GLubyte *g_glGetString(GLenum name) {
    // cLib3DOGL probes extensions (PVRTC, point sprites): answer like an iPhone 3G MBX/SGX driver.
    switch (name) {
    case GL_VENDOR: return (const GLubyte *)"Imagination Technologies";
    case GL_RENDERER: return (const GLubyte *)"PowerVR MBX";
    case GL_VERSION: return (const GLubyte *)"OpenGL ES-CM 1.1";
    case GL_EXTENSIONS:
        return (const GLubyte *)"GL_IMG_texture_compression_pvrtc GL_OES_point_sprite GL_OES_point_size_array "
                                "GL_OES_framebuffer_object GL_OES_matrix_palette GL_IMG_read_format";
    }
    return (const GLubyte *)"";
}

// MacIndexBuffer::Init treats any error after glGenBuffers as "no VBO" and skips glBufferData,
// then binds the empty buffer anyway -> NULL index read in vitaGL. Errors vitaGL raises that iOS
// doesn't must not leak into the game: log them and report GL_NO_ERROR.
static GLenum g_glGetError(void) {
    GLenum e = glGetError();
    static int logged;
    if (e != GL_NO_ERROR && logged < 16) {
        logged++;
        l_warn("glGetError: vitaGL 0x%04X swallowed", e);
    }
    return GL_NO_ERROR;
}

static void g_glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params) {
    GLint iv[4] = {0};
    glGetTexEnviv(target, pname, iv);
    params[0] = (GLfloat)iv[0];
}

// vitaGL buffer names are vbo pointers (0x8xxxxxxx); CGroundMesh::RenderFE keeps them as u16
// (ldrh before glBindBuffer) -> truncated name 0xaed8 crashed in glDrawElements (run 6).
// Hand the game small names, mapped to vitaGL's.
#define MAX_BUFS 8192
static GLuint buf_map[MAX_BUFS]; // [small name] -> vitaGL name; slot 0 unused

static void g_glGenBuffers(GLsizei n, GLuint *out) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint real = 0, k;
        glGenBuffers(1, &real);
        for (k = 1; k < MAX_BUFS && buf_map[k]; k++)
            ;
        if (k == MAX_BUFS) {
            l_error("glGenBuffers: name table full");
            glDeleteBuffers(1, &real);
            out[i] = 0;
            continue;
        }
        buf_map[k] = real;
        out[i] = k;
    }
}

static void g_glDeleteBuffers(GLsizei n, const GLuint *names) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint k = names[i];
        if (k && k < MAX_BUFS && buf_map[k]) {
            glDeleteBuffers(1, &buf_map[k]);
            buf_map[k] = 0;
        }
    }
}

static void g_glBindBuffer(GLenum target, GLuint k) { glBindBuffer(target, k < MAX_BUFS ? buf_map[k] : 0); }

// Run 8: in-game every mesh showed the grass texture, with 13+ GL_INVALID_ENUM at mission load.
// Log uploads vitaGL rejects (a failed texture keeps drawing whatever GXM had bound).
static void tex_check(const char *fn, GLint level, GLenum fmt, GLsizei w, GLsizei h, GLenum type, GLsizei size) {
    static int ok, logged;
    GLenum e = glGetError();
    if (e == GL_NO_ERROR) {
        if (++ok % 100 == 0)
            l_info("[TEX] %d uploads ok", ok);
        return;
    }
    if (logged++ < 48)
        l_warn("[TEX] %s err 0x%04X level %d fmt 0x%04X %dx%d type 0x%04X size %d", fn, e, level, fmt, w, h, type,
               size);
}

static void g_glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt,
                           GLenum type, const void *data) {
    // Run 9: RGBA5551 with ifmt GL_RGBA is stored natively (U5U5U5U1_RGBA) and vitaGL rebuilds the mips with the
    // U1U5U5U5_ABGR transfer format -> bit fields mixed -> foliage drawn as blue/green noise. GL_RGBA8 makes
    // vitaGL expand to RGBA8888, whose mips downscale correctly.
    if (type == GL_UNSIGNED_SHORT_5_5_5_1 && ifmt == GL_RGBA)
        ifmt = GL_RGBA8;
    glTexImage2D(target, level, ifmt, w, h, border, fmt, type, data);
    tex_check("glTexImage2D", level, fmt, w, h, type, 0);
}

static void g_glCompressedTexImage2D(GLenum target, GLint level, GLenum fmt, GLsizei w, GLsizei h, GLint border,
                                     GLsizei size, const void *data) {
    glCompressedTexImage2D(target, level, fmt, w, h, border, size, data);
    tex_check("glCompressedTexImage2D", level, fmt, w, h, 0, size);
}

// ---------------------------------------------------------------- import table

#define F(name, fn) {"_" #name, (uintptr_t)(fn)}
#define D(name) {"_" #name, (uintptr_t)(name)}

const import_entry gles_imports[] = {
    D(glActiveTexture), D(glBindTexture), D(glBlendFunc), D(glClear), D(glCopyTexImage2D),
    D(glCopyTexSubImage2D), D(glDeleteTextures), D(glDepthMask), D(glDisable), D(glDisableClientState),
    D(glEnable), D(glEnableClientState), D(glGenTextures), D(glGetIntegerv), D(glIsEnabled),
    D(glLoadIdentity), D(glLoadMatrixx), D(glMatrixMode), D(glOrthox), D(glPopMatrix), D(glPushMatrix),
    D(glRotatex), D(glScalex), D(glScissor), D(glShadeModel), F(glTexImage2D, g_glTexImage2D), D(glTexParameteri),
    D(glTexSubImage2D), D(glTranslatex), D(glViewport),
    D(glAlphaFunc), F(glBindBuffer, g_glBindBuffer), D(glBufferData), D(glBufferSubData),
    F(glClearColor, g_glClearColor), D(glClearDepthf), D(glClearStencil), D(glColor4ub),
    F(glColorMask, g_glColorMask), D(glColorPointer), F(glCompressedTexImage2D, g_glCompressedTexImage2D), F(glDeleteBuffers, g_glDeleteBuffers),
    D(glDepthFunc), D(glFogf), D(glFogfv), D(glFrontFace), D(glFrustumf),
    F(glGenBuffers, g_glGenBuffers), F(glGetError, g_glGetError), D(glHint), D(glLineWidth), D(glLoadMatrixf),
    D(glMultMatrixf), D(glNormalPointer), D(glPixelStorei), D(glPointSize),
    D(glPolygonOffset), D(glReadPixels), D(glRotatef), D(glScalef),
    D(glStencilFunc), D(glStencilMask), D(glStencilOp), D(glTexEnvf),
    D(glTexEnvfv), D(glTranslatef),
    F(glClearColorx, g_glClearColorx), F(glTexEnvx, g_glTexEnvx),
    F(glDrawElements, g_glDrawElements), F(glGetTexEnvfv, g_glGetTexEnvfv),
    F(glColor4x, g_glColor4x), F(glTexEnvi, g_glTexEnvi), F(glTexEnvxv, g_glTexEnvxv),
    F(glTexParameterx, g_glTexParameterx), F(glGetFixedv, g_glGetFixedv), F(glGetLightxv, g_glGetLightxv),
    F(glVertexPointer, g_glVertexPointer), F(glTexCoordPointer, g_glTexCoordPointer),
    F(glClientActiveTexture, g_glClientActiveTexture), F(glDrawArrays, g_glDrawArrays),
    F(glGetPointerv, g_glGetPointerv),
    D(glCullFace), D(glTexParameterf), D(glGetFloatv), D(glOrthof), F(glFogx, g_glFogx), F(glFlush, g_glFlush),
    F(glGetString, g_glGetString),
    F(glGenFramebuffersOES, g_glGenNames), F(glGenRenderbuffersOES, g_glGenNames),
    F(glBindFramebufferOES, g_glBindFramebufferOES), F(glBindRenderbufferOES, g_glNoop),
    F(glDeleteFramebuffersOES, g_glNoop), F(glDeleteRenderbuffersOES, g_glNoop),
    F(glRenderbufferStorageOES, g_glNoop), F(glFramebufferRenderbufferOES, g_glNoop),
    F(glCheckFramebufferStatusOES, g_glCheckFramebufferStatusOES),
    F(glGetRenderbufferParameterivOES, g_glGetRenderbufferParameterivOES),
};
const int gles_imports_count = sizeof(gles_imports) / sizeof(gles_imports[0]);
