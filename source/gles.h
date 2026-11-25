/**
 * @file gles.h
 * @brief OpenGL ES 1.1 / 2.0 graphics pipeline over vitaGL with SoftFP calling convention.
 */
#pragma once
#include <stdint.h>

/**
 * @brief Initializes the OpenGL ES pipeline and off-screen virtual FBO.
 */
void gles_init(void);

/**
 * @brief Presents current frame: blits and scales iOS FBO to PS Vita 960x544 screen.
 */
void gles_present(void);

/**
 * @brief Returns the total number of draw calls dispatched this session.
 */
uint32_t gles_get_draw_calls(void);

/** @brief Re-sends the guest's vertex/texcoord pointers to vitaGL (after a cutscene). */
void gles_reapply_arrays(void);

/** @brief Screen rectangle the game image is drawn to (full screen or original aspect, options menu). */
void gles_dest_rect(int *x, int *y, int *w, int *h);
extern int gles_rot180; /**< 1 when the last frame was drawn for CTouchPad orientation 2. */
