/**
 * renderer.h - Drawing primitives that the UI is built upon
 */

#ifndef ETSUKO_RENDERER_H
#define ETSUKO_RENDERER_H

#include "constants.h"

#include <stdint.h>
#include <unicode/utf8.h>

// The max number of sub regions that can be specified when drawing portions of a texture
#define MAX_DRAW_SUB_REGIONS (4)

/**
 * Represents a texture uploaded to the GPU using OpenGL, with some cached information about it
 */
typedef struct Texture_t {
    // OpenGL texture id
    unsigned int id;
    // Dimensions in pixels
    int32_t width, height;
    // Optional border radius used when rendering the texture
    float border_radius;
    // Internal vertex array object and buffer object pre-configured and exclusive to this texture
    unsigned int vbo, vao;
    // Cached values used when configuring the above VAO and VBO so we can track when those need to be reconfigured
    int32_t buf_x, buf_y, buf_w, buf_h;
} Texture_t;

/**
 * Basic definition of a color
 */
typedef struct Color_t {
    // Color component values
    uint8_t r, g, b, a;
} Color_t;

/**
 * Basic definition of a bounding box with both (relative) positioning and dimensions.
 * Position is usually relative to some parent when used externally, and meant to be absolute
 * when calling functions on the renderer with it (relative to the screen).
 * Using any dimension values other than the texture's own width and height will cause the texture to
 * be stretched instead of being cut/partially drawn (see draw regions for that effect instead)
 */
typedef struct Bounds_t {
    // Position and dimensions
    double x, y, w, h;
    /** 
     * Optional scale modification, with 0(the default)being the regular size, same as the dimensions,
     * and any value lesser or greater than 0 (with the lower bound of -1, which would be equal
     * to zeroing the dimensions of the texture) will apply a scale mod to the final rendered texture.
     * Example: 0.75 scale_mod = 1.75 of the normal scale
     * Example: -0.25 scale_mod = 0.75 of the normal scale
     */
    double scale_mod;
} Bounds_t;

/**
 * A shadow is basically the same as a texture but with the optional info of being generated separately
 * based on a given texture (usually upon creation) and that can hold additional info such as an offset
 * relative to the parent texture and its own bounds.
 * It's expected of the caller to supply the correct Bounds when drawing the shadow like a regular texture
 * (and drawing it before the texture itself so the effect looks correct), applying the offset manually.
 */
typedef struct Shadow_t {
    // Texture of the shadow itself
    OWNING Texture_t *texture;
    // Bounds of the generated texture. It's likely larger than the texture itself
    Bounds_t bounds;
    // Offset relative to the parent texture. Must be applied manually before rendering the shadow
    int32_t offset;
} Shadow_t;

/**
 * A texture render target used to generate a single texture from (possibly) multiple sources or
 * a combination of multiple operations.
 * It holds a texture with the result of all the draw operations that must be freed after it's been used,
 * or assigned to a struct that will own it (and later free it), like a Drawable
 */
typedef struct RenderTarget_t {
    // Resulting texture. Must be freed separately from the target
    WEAK Texture_t *texture;
    // Points to the previous render target or NULL if none existed when this render target was created
    WEAK struct RenderTarget_t *prev_target;
    // Framebuffer object associated with this render target
    unsigned int fbo;
    // Configured viewport
    int viewport[4];
    // Projection matrix
    float projection[16];
} RenderTarget_t;

/**
 * Defines different types of fullscreen backgrounds that can be used and will be applied automatically
 * on the start of every frame after clearing the framebuffer.
 */
typedef enum BackgroundType_t {
    // Solid color
    BACKGROUND_NONE = 0,
    // Static gradient between the two specified colors
    BACKGROUND_GRADIENT,
    // A moving, cloud like gradient shader
    BACKGROUND_DYNAMIC_GRADIENT,
    // A shader that cycles between multiple pre-defined colors
    BACKGROUND_RANDOM_GRADIENT,
    // A shader that imitates Apple Music's background effect
    BACKGROUND_AM_LIKE_GRADIENT,
    // Variant of the same shader for the AM_LIKE effect but modified to generate clouds
    BACKGROUND_CLOUD_GRADIENT,
} BackgroundType_t;

/*
 * Defines types of font to use when creating text textures and measuring glyphs
 */
typedef enum FontType_t {
    // Usually a thinner, more ui-like font, although this is not enforced
    FONT_UI = 0,
    // The font used to display the lyrics. Preferrably a bolder, fatter font
    FONT_LYRICS = 1
} FontType_t;

/*
 * Blend modes used by the renderer when drawing textures to the screen
 */
typedef enum BlendMode_t {
    BLEND_MODE_BLEND = 0,
    BLEND_MODE_ADD,
    BLEND_MODE_NONE,
    BLEND_MODE_ERASE
} BlendMode_t;

/*
 * Represents the bounds of a single character inside a bigger string of characters with the specified font
 */
typedef struct CharBounds_t {
    // Distance between the current character and the previous one (usually to the left), if there's any
    double kerning;
    // Number of pixels to advance in the x axis
    double advance;
    // Width of the character itself, including the space between itself and the previous character
    double width;
    // Maximum height of a character in the given font, starting from the y coordinate used as a baseline
    // (characters can descend below the baseline and this value, AFAIK, does not account for this specifically)
    double font_height;
} CharBounds_t;

/*
 * Allows for specifying which parts of the texture that should be drawn, with values
 * in percentages of the total size.
 * Example: 0, 0.5, 0, 0.5 will display the bottom left quarter of the texture (50% of each axis,
 * y coordinates, in this case, go from 0 (bottom) to 1 (top))
 */
typedef struct DrawRegionOpt_t {
    float x0_perc, x1_perc;
    float y0_perc, y1_perc;
} DrawRegionOpt_t;

/*
 * Set of draw regions that can be applied to a single texture at the same time. This allows for multi-line text
 * in a single texture to highlight each line separately, given that the percentages are calculated correctly
 * using the character bounds for each line and accounting for any padding and alignment
 */
typedef struct DrawRegionOptSet_t {
    DrawRegionOpt_t regions[MAX_DRAW_SUB_REGIONS];
    int32_t num_regions;
} DrawRegionOptSet_t;

/*
 * Options that can be specified when drawing a texture using the renderer
 */
typedef struct DrawTextureOpts_t {
    // Alpha mod to be applied to the texture. Defaults to 255 which is opaque.
    // This value is absolute and not relative to any kind of saved alpha of the texture.
    int32_t alpha_mod;
    // A color mod to be applied to the texture. This value is relative to the texture's original
    // colors and is applied as a whole, so that 1.0 will render the original texture, 0.5 will render
    // it 50% darker, and 0 will display it solid black
    float color_mod;
    // Optional set of draw regions to limit the visibility of parts of the texture
    WEAK const DrawRegionOptSet_t *draw_regions;
} DrawTextureOpts_t;

void render_init(void);
void render_finish(void);
void render_on_window_changed(void);
void render_clear(void);
void render_present(void);
const Bounds_t *render_get_viewport(void);
double render_get_pixel_scale(void);
void render_set_window_title(const char *title);
void render_set_bg_color(Color_t color);
void render_set_bg_gradient(Color_t top_color, Color_t bottom_color, BackgroundType_t type);
void render_sample_bg_colors_from_image(const unsigned char *bytes, int length);
void render_set_blend_mode(BlendMode_t mode);
BlendMode_t render_get_blend_mode(void);
Color_t render_color_parse(uint32_t color);
Color_t render_color_darken(Color_t color);
void render_load_font(const unsigned char *data, int data_size, FontType_t type);
void render_measure_text_size(const char *text, int32_t pixels, int32_t *w, int32_t *h, FontType_t kind);
int32_t render_measure_pt_from_em(double em);
int32_t render_measure_pixels_from_em(double em);
void render_measure_char_bounds(UChar32 c, UChar32 prev_c, int32_t pixels, CharBounds_t *out_bounds, FontType_t font);
Texture_t *render_make_null(void);
Texture_t *render_make_text(const char *text, int32_t pixels_size, const Color_t *color, FontType_t font_type);
Texture_t *render_make_image(const unsigned char *bytes, int length, double border_radius_em);
Texture_t *render_make_dummy_image(double border_radius_em);
void render_destroy_shadow(Shadow_t *shadow);
Shadow_t *render_make_shadow(Texture_t *texture, const Bounds_t *src_bounds, float blur_radius, int32_t offset);
void render_destroy_texture(Texture_t *texture);
const RenderTarget_t *render_make_texture_target(int32_t width, int32_t height);
Texture_t *render_blur_texture(const Texture_t *source, float blur_radius);
Texture_t *render_blur_texture_replace(Texture_t *source, float blur_radius);
Texture_t *render_restore_texture_target(void);
void render_draw_rounded_rect(const Texture_t *null_tex, const Bounds_t *bounds, const Color_t *color, float border_radius);
void render_draw_texture(Texture_t *texture, const Bounds_t *at, const DrawTextureOpts_t *opts);

#endif // ETSUKO_RENDERER_H
