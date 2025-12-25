/**
 * renderer.h - Drawing primitives that the UI is built upon
 */

#ifndef ETSUKO_RENDERER_H
#define ETSUKO_RENDERER_H

#include "constants.h"

#include <stdint.h>

// The max number of sub regions that can be specified when drawing portions of a texture
#define MAX_DRAW_SUB_REGIONS (4)
// The max number of sub regions that can be scaled at the same time
#define MAX_SCALE_SUB_REGIONS (20)

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
     * Optional scale modification, with 0 (the default) being the regular size, same as the dimensions,
     * and any value lesser or greater than 0 (with the lower bound of -1, which would be equal
     * to zeroing the dimensions of the texture) will apply a scale mod to the final rendered texture.
     * Example: 0.75 scale_mod = 1.75 of the normal scale
     * Example: -0.25 scale_mod = 0.75 of the normal scale
     * In other words: final scale = 1.0 + scale_mod
     */
    double scale_mod;
} Bounds_t;

/**
 * A shadow is basically the same as a texture but with the optional info of being generated separately
 * based on a given texture (usually upon creation) and that can hold additional info such as an offset
 * relative to the parent texture and its own bounds.
 * It's expected of the caller to supply the correct Bounds when drawing the shadow like a regular texture
 * (and drawing it before the texture itself so the effect looks correct) and applying the offset manually.
 */
typedef struct Shadow_t {
    // Texture of the shadow itself
    OWNING Texture_t *texture;
    // Bounds of the generated texture. It's likely larger than the texture itself
    Bounds_t bounds;
    // Offset relative to the parent texture. Must be applied manually before rendering the shadow.
    // Applies to both the x and y axis at the same time.
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
    BACKGROUND_SANDS_GRADIENT,
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
 * Note that this only applies to this particular instance as it's passed directly to OpenGL. All other instances
 * of coordinates expect 0 to be top and H (screen height) to be bottom.
 * Actually this doesn't really matter as long as it represents a valid interval in the y axis, whether it's 0-0.5 or 0.5-0 I believe
 * it behaves the same, but it's just something to keep in mind.
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

/**
 * Set of regions to apply scale in a geometry level, that is, we apply a transformation to the quad for the texture in
 * the geometry shader before drawing it to the screen, leaving the rest unchanged.
 * The relative_scale param behaves in the same way as the Bounds_t scale_mod, that is, 0 being the default and any value other than 0
 * being a mod to the original value, e.g. a relative_scale of 0.5 is the same as 1.5 of the original scale, in other words, bigger than the default.
 * A negative value yields a smaller final scale.
 * TODO Fix this doc
 */
typedef struct ScaleRegionOpt_t {
    float x0_perc, x1_perc;
    float y0_perc, y1_perc;
    float from_scale, to_scale;
    float relative_scale;
} ScaleRegionOpt_t;

/**
 * Contains a set of ScaleRegionOpt_t that can be applied _at the same time_.
 */
typedef struct ScaleRegionOptSet_t {
    ScaleRegionOpt_t regions[MAX_SCALE_SUB_REGIONS];
    int32_t num_regions;
} ScaleRegionOptSet_t;

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
    // Optional set of regions to scale inside the final texture
    WEAK const ScaleRegionOptSet_t *scale_regions;
} DrawTextureOpts_t;

/**
 * Initializes the rendering backend with the creation of a window (in case of a desktop build), an OpenGL context and initialization
 * of various settings and parameters.
 * This function will abort on error so it's not possible to programmatically check if it succeeded or not, other than that the
 * program flow continues past the point of calling it.
 * After it returns, it's safe to call any other associated functions on it.
 * It's not recommended (and this applies to the renderer in general, not just this one function) to call any of the render_* functions
 * on threads other than the main one. No synchronization is in place and some of these calls modify internal render state (blend mode,
 * render targets, etc) that will cause weird behavior if called concurrently.
 */
void render_init(void);
/**
 * Cleans up all resources taken up by the renderer. It's only expected to be ran once in the program lifetime, as the renderer is not
 * meant to be destroyed and reconstructed on each scene/screen change.
 */
void render_finish(void);
/**
 * Calculates the usable area and updates internal reference values and structures used to compute and draw textures.
 * Should be called whenever a change happened to the screen size, DPI or whatever.
 */
void render_on_window_changed(void);
/**
 * Clears the framebuffer for drawing, also applying whatever background option is set with the provided colors.
 * Backgrounds can be disabled with BACKGROUND_NONE and setting the color black as the background.
 * Supposed to be called at the start of every frame.
 */
void render_clear(void);
/**
 * Swaps framebuffers replacing the image being shown in the screen with the one that has been drawn to so far, essentially
 * presenting the image to the screen.
 * Supposed to be called at the end of every frame.
 */
void render_present(void);
/**
 * Gets the bounds (usually the x and y will always be zero, so it's mostly the dimensions) of the screen, also considered to be
 * the root container in the UI module.
 */
const Bounds_t *render_get_viewport(void);
/**
 * Returns the pixel scale of the reported screen size relative to the actual pixel framebuffer we can draw to.
 * This is mostly because on mac OS, mouse events are reported in "whole" pixels while the framebuffer itself, in retina displays,
 * usually count every pixel as 2, so any calculations regarding if the mouse cursor is at a certain place on the screen is always off.
 */
double render_get_pixel_scale(void);
/**
 * Sets the window title (in a desktop build), or the tab title when targetting web/wasm.
 */
void render_set_window_title(const char *title);
/**
 * Sets a single color for background effects. For effects that use more than one color, every other one is going to be assigned
 * black (0,0,0), so for example a static gradient will go from this color to black.
 */
void render_set_bg_color(Color_t color);
/**
 * Assigns two colors and a background type option. For effects that use more than two colors (AM-like), the other colors are assigned black.
 */
void render_set_bg_gradient(Color_t top_color, Color_t bottom_color, BackgroundType_t type);
/**
 * Sample 5 colors from the given raw image data to be used in background effects.
 * Supported image types: JPEG, PNG
 * For effects that use less than 5 colors (static gradient, dynamic gradient, solid color), the first N colors will be used from this sample
 */
void render_sample_bg_colors_from_image(const unsigned char *bytes, int length);
/**
 * Set the blend mode to be used when calling render_draw_texture
 */
void render_set_blend_mode(BlendMode_t mode);
/**
 * Retrieves the current blend mode
 */
BlendMode_t render_get_blend_mode(void);
/**
 * Parses a color from a 32-bit unsigned int in ARGB format
 */
Color_t render_color_parse(uint32_t color);
/**
 * Darkens a given color by a given amount in the range of 0 to 1, e.g. 0.2 darkens a color by 20%, or the equivalent of multiplying
 * all color channels by 0.8
 */
Color_t render_color_darken(Color_t color, double amount);
/**
 * Loads and stores the given font (truetype) and assigns it to the specified type.
 * In case another font of the same type has already been loaded, it is freed and replaced
 */
void render_load_font(const unsigned char *data, int data_size, FontType_t type);
/**
 * Measures text in the given size and font type, optionally saving the width and height of the overall text.
 */
void render_measure_text_size(const char *text, int32_t pixels, MAYBE_NULL int32_t *w, MAYBE_NULL int32_t *h, FontType_t kind);
/**
 * Returns a pt size for the given em value, which is essentially (and this might be wrong, but it IS what the function does)
 * the value of render_measure_pixels_from_em but with scaling based on the ratio of the current screen DPI and the base DPI
 */
int32_t render_measure_pt_from_em(double em);
/**
 * Returns a pixel size for the given em value
 */
int32_t render_measure_pixels_from_em(double em);
/**
 * Calculates bounds (mostly dimensions) for the given character, optionally in relation to a previous character,
 * in a given font type and size
 */
void render_measure_char_bounds(int32_t c, int32_t prev_c, int32_t pixels, CharBounds_t *out_bounds, FontType_t font);
/**
 * Creates a null texture which is a no-op when attempting to draw it. Useful when constructing a dynamic drawable that cannot
 * be cached to a texture (or it's more expensive to do so) and thus must be drawn in some other method (probably below)
 */
Texture_t *render_make_null(void);
/**
 * Creates a texture of the given text string using the provided font, font size and color.
 * The resulting texture is exactly the size it needs to contain the final bitmap and no centering, alignment or wrapping is performed
 * in the renderer.
 */
Texture_t *render_make_text(const char *text, int32_t pixels_size, const Color_t *color, FontType_t font_type);
/**
 * Creates a texture from raw image data, optionally assigning a border radius to the texture directly (but it's not a feature exclusive
 * to images).
 * Supported image formats: JPEG, PNG
 */
Texture_t *render_make_image(const unsigned char *bytes, int length, double border_radius_em);
/**
 * Creates a texture with a checkboard pattern with fixed size and optional border radius.
 * Kinda useless.
 */
Texture_t *render_make_dummy_image(double border_radius_em);
/**
 * Creates a shadow texture based on the provided texture, which by adding the additional padding so the blur looks better will always
 * be larger than the original texture.
 * The shadow is first created by making a completely black version of the same texture, applying blur to it with some alpha blending
 * and then erasing the part of the original texture that would/will sit on top of the shadow. This is calculated using the provided offset,
 * so if the shadow is later drawn at a different offset, it'll probably show transparent parts.
 * It's probably useful if we have an option to do this erase part or not based on a parameter.
 * The texture itself is not modified but render_draw_texture needs a non-const pointer to the texture because it may need to reconfigure
 * the cached attributes inside the texture (see Texture_t and render_draw_texture for more information).
 */
Shadow_t *render_make_shadow(Texture_t *texture, const Bounds_t *src_bounds, float blur_radius, int32_t offset);
/**
 * Frees all resources associated with a shadow. Does not affect the original texture
 */
void render_destroy_shadow(Shadow_t *shadow);
/**
 * Frees all resources associated with a texture. Does not destroy any shadows created from it, must be freed separately.
 */
void render_destroy_texture(Texture_t *texture);
/**
 * Creates a texture render target with the given width and height.
 * The provided dimensions will be the final size of the resulting texture.
 * Any draw operations called between this function call and render_restore_texture_target will be drawn to *A* render target instead of
 * the framebuffer.
 * Texture render targets can be accumulated and the most recent call to this function will set the active target to be drawn to.
 */
const RenderTarget_t *render_make_texture_target(int32_t width, int32_t height);
/**
 * Restores the currently active render target, or aborts with an error if no target is active (i.e., we're currently drawing to the framebuffer.
 * it's arguably better to return an error code, but if this situation ever happens it's something that needs to be resolved in the application code
 * so it's better to be pretty explicit about this one case).
 * If a render target was already active when another one was created, it is saved and restored when this function is called, so that render targets can
 * stack on top of each other.
 * This function also returns the texture associated with the created framebuffer, which must be freed separately.
 */
Texture_t *render_restore_texture_target(void);
/**
 * Creates a new texture with blur applied to the given texture, using the blur_radius parameter.
 */
Texture_t *render_blur_texture(const Texture_t *source, float blur_radius);
/**
 * Creates a new texture with blur applied to the given texture, using the blur_radius parameter.
 * The old texture is destroyed so that this virtually "replaces" a texture with is blurred variant without the caller needing to explicitly do so.
 */
Texture_t *render_blur_texture_replace(Texture_t *source, float blur_radius);
/**
 * Draws a rounded rect to the screen, using the bounds parameter as both location and dimension parameters, using the given color and border radius.
 * A null texture (created using render_make_null) is needed because a vertex array object and buffer object are pre-computed for the given dimensions
 * for performance and the null texture, even if it doesn't actually point to a texture uploaded to the GPU, helps keep track of this information.
 * This draw function is subject to the currently active render target and behaves the same as render_draw_texture in that regard.
 */
void render_draw_rounded_rect(const Texture_t *null_tex, const Bounds_t *bounds, const Color_t *color, float border_radius);
/**
 * Draws a texture to the currently active render target (which can be a texture render target, or the framebuffer itself), using the provided options.
 * Bounds specifies the location and size the texture is to be drawn to/as, and upon change in dimensions the pre-computed vertex array object and
 * buffer object can be reconfigured.
 * The texture itself is not changed, but because of the procedure described above, cached values for the dimensions as well as the vertex objects
 * themselves may suffer changes.
 * All options are non-destructive and only affect how the texture is drawn to the target, not changing the original data in the texture uploaded to
 * GPU memory.
 */
void render_draw_texture(Texture_t *texture, const Bounds_t *at, const DrawTextureOpts_t *opts);

#endif // ETSUKO_RENDERER_H
