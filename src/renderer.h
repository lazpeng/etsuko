/**
 * renderer.h - Drawing primitives that the UI is built upon
 */

#ifndef ETSUKO_RENDERER_H
#define ETSUKO_RENDERER_H

#include <stdint.h>

typedef struct etsuko_Texture_t {
    unsigned int id;
    int32_t width, height;
    float border_radius;
} etsuko_Texture_t;

typedef struct etsuko_Color_t {
    uint8_t r, g, b, a;
} etsuko_Color_t;

typedef struct etsuko_Bounds_t {
    double x, y, w, h;
} etsuko_Bounds_t;

typedef struct etsuko_RenderTarget_t {
    etsuko_Texture_t *texture;
    unsigned int fbo;
    struct etsuko_RenderTarget_t *prev_target;
    int saved_viewport[4];
    float saved_projection[16];
} etsuko_RenderTarget_t;

typedef enum etsuko_FontType_t { FONT_UI = 0, FONT_LYRICS = 1 } etsuko_FontType_t;

typedef enum etsuko_BlendMode_t { BLEND_MODE_BLEND = 0, BLEND_MODE_NONE } etsuko_BlendMode_t;

void render_init();
void render_finish();
void render_on_window_changed();
void render_clear();
void render_present();
const etsuko_Bounds_t *render_get_viewport();
double render_get_pixel_scale();
void render_set_window_title(const char *title);
void render_set_bg_color(etsuko_Color_t color);
void render_set_bg_gradient(etsuko_Color_t top_color, etsuko_Color_t bottom_color);
void render_set_blend_mode(etsuko_BlendMode_t mode);
etsuko_BlendMode_t render_get_blend_mode();
etsuko_Color_t render_color_parse(uint32_t color);
etsuko_Color_t render_color_darken(etsuko_Color_t color);
void render_load_font(const char *path, etsuko_FontType_t type);
void render_measure_text_size(const char *text, int32_t pt, int32_t *w, int32_t *h, etsuko_FontType_t kind);
int32_t render_measure_pt_from_em(double em);

etsuko_Texture_t *render_make_text(const char *text, int32_t pt_size, bool bold, const etsuko_Color_t *color,
                                   etsuko_FontType_t font_type);
etsuko_Texture_t *render_make_image(const char *file_path, double border_radius_em);
etsuko_Texture_t *render_make_dummy_image(double border_radius_em);
void render_destroy_texture(etsuko_Texture_t *texture);
const etsuko_RenderTarget_t *render_make_texture_target(int32_t w, int32_t h);
void render_restore_texture_target();

void render_draw_rounded_rect(const etsuko_Bounds_t *bounds, const etsuko_Color_t *color, float border_radius);
void render_draw_texture(const etsuko_Texture_t *texture, const etsuko_Bounds_t *at, int32_t alpha_mod);

#endif // ETSUKO_RENDERER_H
