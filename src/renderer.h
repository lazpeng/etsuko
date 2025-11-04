/**
 * renderer.h - Drawing primitives that the UI is built upon
 */

#ifndef ETSUKO_RENDERER_H
#define ETSUKO_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct SDL_Texture SDL_Texture;
typedef SDL_Texture *etsuko_Texture_t;

typedef struct etsuko_Color_t {
    uint8_t r, g, b, a;
} etsuko_Color_t;

typedef struct etsuko_Bounds_t {
    float x, y, w, h;
} etsuko_Bounds_t;

typedef enum etsuko_FontType_t { FONT_UI = 0, FONT_LYRICS = 1 } etsuko_FontType_t;

void render_init(void);
void render_finish(void);
void render_on_window_changed(void);
void render_clear(void);
void render_present(void);
const etsuko_Bounds_t *render_get_viewport(void);

void render_load_font(const char *path, etsuko_FontType_t type);
void render_set_window_title(const char *title);
void render_measure_text_size(const char *text, int32_t pt, int32_t *w, int32_t *h, etsuko_FontType_t kind);
int32_t render_measure_pt_from_em(float em);
void render_measure_texture(etsuko_Texture_t texture, int32_t *w, int32_t *h);
etsuko_Color_t render_color_parse(uint32_t color);
void render_set_bg_color(etsuko_Color_t color);

etsuko_Texture_t render_make_texture_target(int32_t w, int32_t h);
void render_restore_texture_target(void);
void render_destroy_texture(etsuko_Texture_t texture);
etsuko_Texture_t render_make_text(const char *text, int32_t pt_size, bool bold, const etsuko_Color_t *color, etsuko_FontType_t font_type);
etsuko_Texture_t render_make_image(const char *file_path, int corner_radius);

void render_draw_rounded_rect(const etsuko_Bounds_t *bounds, const etsuko_Color_t *color);
void render_draw_texture(etsuko_Texture_t texture, const etsuko_Bounds_t *at, int32_t alpha_mod);
void render_draw_texture_no_blend(etsuko_Texture_t texture, const etsuko_Bounds_t *at, int32_t alpha_mod);

#endif // ETSUKO_RENDERER_H
