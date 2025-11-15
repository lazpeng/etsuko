/**
 * renderer.h - Drawing primitives that the UI is built upon
 */

#ifndef ETSUKO_RENDERER_H
#define ETSUKO_RENDERER_H

#include <stdint.h>

typedef struct Texture_t {
    unsigned int id;
    int32_t width, height;
    float border_radius;
} Texture_t;

typedef struct Color_t {
    uint8_t r, g, b, a;
} Color_t;

typedef struct Bounds_t {
    double x, y, w, h;
    double scale_mod;
} Bounds_t;

typedef struct RenderTarget_t {
    Texture_t *texture;
    struct RenderTarget_t *prev_target;
    // Should not be used outside renderer
    unsigned int fbo;
    int viewport[4];
    float projection[16];
} RenderTarget_t;

typedef enum BackgroundType_t {
    BACKGROUND_NONE = 0,
    BACKGROUND_GRADIENT,
    BACKGROUND_DYNAMIC_GRADIENT,
    BACKGROUND_RANDOM_GRADIENT
} BackgroundType_t;

typedef enum FontType_t { FONT_UI = 0, FONT_LYRICS = 1 } FontType_t;

typedef enum BlendMode_t { BLEND_MODE_BLEND = 0, BLEND_MODE_ADD, BLEND_MODE_NONE } BlendMode_t;

void render_init();
void render_finish();
void render_on_window_changed();
void render_clear();
void render_present();
const Bounds_t *render_get_viewport();
double render_get_pixel_scale();
void render_set_window_title(const char *title);
void render_set_bg_color(Color_t color);
void render_set_bg_gradient(Color_t top_color, Color_t bottom_color, BackgroundType_t type);
void render_set_blend_mode(BlendMode_t mode);
BlendMode_t render_get_blend_mode();
Color_t render_color_parse(uint32_t color);
Color_t render_color_darken(Color_t color);
void render_load_font(const char *path, FontType_t type);
void render_measure_text_size(const char *text, int32_t pt, int32_t *w, int32_t *h, FontType_t kind);
int32_t render_measure_pt_from_em(double em);

Texture_t *render_make_text(const char *text, int32_t pt_size, bool bold, const Color_t *color, FontType_t font_type);
Texture_t *render_make_image(const char *file_path, double border_radius_em);
Texture_t *render_make_dummy_image(double border_radius_em);
Texture_t *render_make_shadow(const Texture_t *texture, float blur_radius, float fade_distance, int32_t padding);
void render_destroy_texture(Texture_t *texture);
const RenderTarget_t *render_make_texture_target(int32_t width, int32_t height);
Texture_t *render_blur_texture(const Texture_t *source, float blur_radius, float fade_distance);
Texture_t *render_blur_texture_replace(Texture_t *source, float blur_radius, float fade_distance);
void render_restore_texture_target();

void render_draw_rounded_rect(const Bounds_t *bounds, const Color_t *color, float border_radius);
void render_draw_texture(const Texture_t *texture, const Bounds_t *at, int32_t alpha_mod, float color_mod);

#endif // ETSUKO_RENDERER_H
