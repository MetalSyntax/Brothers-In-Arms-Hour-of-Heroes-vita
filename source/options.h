#pragma once
// Menú de opciones del port (SELECT+START) y extras: editor de botones, controles en pantalla con L+R,
// filtro de escalado, MSAA, pantalla completa, contador y límite de FPS. Se guarda en DATA_PATH options.txt.
// Mismo archivo en todos los ports iOS de la familia (base: menú de video de Ferrari-GT-Evolution-vita).
#include <stdint.h>

enum { FILTER_BILINEAR, FILTER_SHARP, FILTER_PIXEL };
extern int opt_filter;      // escalado del drawable a la pantalla
extern int opt_msaa;        // 0 = off, 2 = 2x, 4 = 4x (se aplica al reiniciar: vitaGL lo fija en vglInit)
extern int opt_msaa_active; // el valor con que arrancó vitaGL
extern int opt_stretch;     // 1 = pantalla completa (960x544), 0 = aspecto original con barras
extern int opt_show_fps;    // contador arriba a la izquierda
extern int opt_fps_cap;     // 0 = sin límite (el ritmo del juego), 30 = máximo 30 fps
extern int opt_hud_hidden;  // controles táctiles del juego ocultos (L+R); siguen respondiendo

void options_load(void);    // antes de vglInitExtended
int options_msaa_mode(void); // SceGxmMultisampleMode para vglInitExtended
void options_poll(uint32_t buttons); // input.c, en cada poll: SELECT+START abre el menú, L+R alterna los controles
int options_pending(void);
int options_active(void);   // menú abierto (los watchdogs no cuentan ese tiempo)
void options_run(void (*draw_game)(void)); // gles_present: bucle modal con el juego en pausa detrás
void options_frame(void);   // gles_present, justo antes del swap (estado de pantalla 960x544): FPS
// gles.c, en cada draw (antes del glDraw* nativo; vtx = NULL si los datos están en un VBO sin copia):
// -1 = dibujar igual, 0 = dibujar con opacidad 0 % (options_hud_mask(1) antes, (0) después), n > 0 = la tanda con
// options_hud_draw(*out, n): los quads visibles normal y los ocultos con opacidad 0 %.
int options_hud_filter(unsigned mode, const void *vtx, unsigned type, int size, int stride, const void *idx,
                       unsigned itype, int first, int count, const uint16_t **out);
void options_hud_draw(const uint16_t *idx, int n);
void options_hud_mask(int on);
// input.c: botones que ve el juego (L y R retenidos un instante para que L+R no dispare sus acciones).
uint32_t options_filter_buttons(uint32_t buttons);

// Lo pone input.c: acciones de controls.txt (botón de la Vita -> toque en pantalla).
int input_bind_count(void);
const char *input_bind_label(int i);
uint32_t input_bind_mask(int i);
void input_bind_set(int i, uint32_t mask);
void input_save_controls(void);
// Puntos tocados por botones/stick, normalizados 0..1 sobre el drawable del juego (y hacia abajo): x, y y media zona
// en x, y (0, 0 para un botón; la zona del pad táctil para el stick). Devuelve cuántos.
int input_hud_points(float (*pts)[4], int max);
