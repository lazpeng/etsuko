#include "renderer.h"

#include "constants.h"
#include "container_utils.h"
#include "error.h"

#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>

typedef struct etsuko_Renderer_t {
    SDL_Window *window;
    SDL_Renderer *renderer;
    etsuko_Bounds_t viewport;
    TTF_Font *ui_font, *lyrics_font;
    double h_dpi, v_dpi;
    etsuko_Color_t bg_color;
    etsuko_Color_t draw_color;
    etsuko_Texture_t root_texture;
} etsuko_Renderer_t;

static etsuko_Renderer_t *g_renderer = NULL;

void render_init(void) {
    if ( g_renderer != NULL ) {
        render_finish();
        g_renderer = NULL;
    }

    g_renderer = calloc(1, sizeof(*g_renderer));
    if ( g_renderer == NULL ) {
        error_abort("Failed to allocate renderer");
    }

    // Initialize window and renderer
    const int pos = SDL_WINDOWPOS_CENTERED;
    const int flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
    g_renderer->window = SDL_CreateWindow(DEFAULT_TITLE, pos, pos, DEFAULT_WIDTH, DEFAULT_HEIGHT, flags);
    if ( g_renderer->window == NULL ) {
        error_abort("Failed to create window");
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
    SDL_SetHint(SDL_HINT_RENDER_OPENGL_SHADERS, "1");
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "1");

    const int renderer_flags = SDL_RENDERER_ACCELERATED;
    g_renderer->renderer = SDL_CreateRenderer(g_renderer->window, -1, renderer_flags);
    if ( g_renderer->renderer == NULL ) {
        error_abort("Failed to create renderer");
    }

    g_renderer->bg_color = (etsuko_Color_t){0, 0, 0, 255};
    g_renderer->draw_color = (etsuko_Color_t){255, 255, 255, 255};

#ifdef __EMSCRIPTEN__
    double cssWidth, cssHeight;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);

    const int32_t width = (int32_t)cssWidth;
    const int32_t height = (int32_t)cssHeight;
    SDL_SetWindowSize(g_renderer->window, width, height);
#endif

    render_on_window_changed();
}

void render_finish(void) {
    // Unload fonts
    if ( g_renderer->ui_font != NULL )
        TTF_CloseFont(g_renderer->ui_font);
    if ( g_renderer->lyrics_font != NULL )
        TTF_CloseFont(g_renderer->lyrics_font);
    // Unload sdl things
    SDL_DestroyRenderer(g_renderer->renderer);
    SDL_DestroyWindow(g_renderer->window);
    // Cleanup
    free(g_renderer);
    g_renderer = NULL;
}

void render_on_window_changed(void) {
    int32_t outW, outH;
    SDL_GetRendererOutputSize(g_renderer->renderer, &outW, &outH);
    SDL_RenderSetLogicalSize(g_renderer->renderer, outW, outH);

    float hdpi_temp, v_dpi_temp;
    if ( SDL_GetDisplayDPI(0, NULL, &hdpi_temp, &v_dpi_temp) != 0 ) {
        puts(SDL_GetError());
        error_abort("Failed to get DPI");
    }
    g_renderer->h_dpi = hdpi_temp;
    g_renderer->v_dpi = v_dpi_temp;

    g_renderer->viewport = (etsuko_Bounds_t){.x = 0, .y = 0, .w = (float)outW, .h = (float)outH};
}

void render_clear(void) {
    SDL_SetRenderDrawColor(g_renderer->renderer, g_renderer->bg_color.r, g_renderer->bg_color.g, g_renderer->bg_color.b, 255);
    SDL_RenderClear(g_renderer->renderer);
    SDL_SetRenderDrawColor(g_renderer->renderer, g_renderer->draw_color.r, g_renderer->draw_color.g, g_renderer->draw_color.b, 255);
}

void render_present(void) { SDL_RenderPresent(g_renderer->renderer); }
const etsuko_Bounds_t *render_get_viewport(void) { return &g_renderer->viewport; }

void render_load_font(const char *path, const etsuko_FontType_t type) {
    TTF_Font *font = TTF_OpenFontDPI(path, DEFAULT_PT, (int32_t)g_renderer->h_dpi, (int32_t)g_renderer->h_dpi);

    if ( font == NULL ) {
        puts(TTF_GetError());
        error_abort("Could not load font");
    }

    TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
    TTF_SetFontKerning(font, 1);

    if ( type == FONT_UI ) {
        g_renderer->ui_font = font;
    } else if ( type == FONT_LYRICS ) {
        g_renderer->lyrics_font = font;
    } else {
        error_abort("Invalid font kind");
    }
}

void render_set_window_title(const char *title) { SDL_SetWindowTitle(g_renderer->window, title); }

void render_measure_text_size(const char *text, const int32_t pt, int32_t *w, int32_t *h, const etsuko_FontType_t kind) {
    TTF_Font *font = kind == FONT_UI ? g_renderer->ui_font : g_renderer->lyrics_font;
    if ( TTF_SetFontSizeDPI(font, pt, (int32_t)g_renderer->h_dpi, (int32_t)g_renderer->v_dpi) != 0 ) {
        error_abort("Failed to set font size/DPI");
    }

    if ( TTF_SizeUTF8(font, text, w, h) != 0 ) {
        error_abort("Failed to measure line size");
    }
}

int32_t render_measure_pt_from_em(const float em) {
    const double scale = g_renderer->viewport.w / DEFAULT_WIDTH;
    const double rem = fmax(12.0, round(DEFAULT_PT * scale));
    const double pixels = em * rem;
    const int32_t pt_size = (int32_t)lround(pixels * 72.0 / g_renderer->h_dpi);
    return pt_size;
}

etsuko_Texture_t render_make_texture_target(int32_t w, int32_t h) {
    if ( g_renderer->root_texture != NULL ) {
        error_abort("Drawing to two textures simultaneously is not supported yet");
    }

    const int format = SDL_PIXELFORMAT_RGBA8888;
    const int access = SDL_TEXTUREACCESS_TARGET;
    SDL_Texture *texture = SDL_CreateTexture(g_renderer->renderer, format, access, w, h);
    if ( texture == NULL ) {
        error_abort("Failed to create texture for drawing text");
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    // Clear texture
    g_renderer->root_texture = SDL_GetRenderTarget(g_renderer->renderer);

    SDL_SetRenderDrawColor(g_renderer->renderer, 0, 0, 0, 0);
    SDL_SetRenderTarget(g_renderer->renderer, texture);
    SDL_SetRenderDrawBlendMode(g_renderer->renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderClear(g_renderer->renderer);

    return texture;
}

void render_restore_texture_target(void) {
    SDL_SetRenderTarget(g_renderer->renderer, g_renderer->root_texture);
    g_renderer->root_texture = NULL;
}

void render_destroy_texture(etsuko_Texture_t texture) { SDL_DestroyTexture(texture); }

void render_measure_texture(etsuko_Texture_t texture, int32_t *w, int32_t *h) { SDL_QueryTexture(texture, NULL, NULL, w, h); }

etsuko_Color_t render_color_parse(const uint32_t color) {
    const uint8_t a = color >> 24;
    const uint8_t r = color >> 16;
    const uint8_t g = color >> 8;
    const uint8_t b = color & 0xFF;
    return (etsuko_Color_t){.r = r, .g = g, .b = b, .a = a};
}

void render_set_bg_color(const etsuko_Color_t color) { g_renderer->bg_color = color; }

etsuko_Texture_t render_make_text(const char *text, const int32_t pt_size, const bool bold, const etsuko_Color_t *color,
                                  const etsuko_FontType_t font_type) {
    TTF_Font *font = font_type == FONT_UI ? g_renderer->ui_font : g_renderer->lyrics_font;
    if ( font == NULL ) {
        error_abort("Font not loaded");
    }
    if ( strnlen(text, MAX_TEXT_SIZE) == 0 ) {
        error_abort("Text is empty");
    }

    if ( TTF_SetFontSizeDPI(font, pt_size, (int32_t)g_renderer->h_dpi, (int32_t)g_renderer->v_dpi) != 0 ) {
        puts(TTF_GetError());
        error_abort("Failed to set font size/DPI");
    }
    const int style = bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL;
    TTF_SetFontStyle(font, style);

    const SDL_Color sdl_color = (SDL_Color){color->r, color->g, color->b, color->a};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, sdl_color);
    if ( surface == NULL ) {
        puts(TTF_GetError());
        error_abort("Failed to render to surface");
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(g_renderer->renderer, surface);
    SDL_FreeSurface(surface);
    if ( texture == NULL ) {
        puts(SDL_GetError());
        error_abort("Failed to create texture");
    }

    return texture;
}

static SDL_Surface *round_corners_surface(SDL_Surface *surf, const int radius) {
    if ( !surf )
        return NULL;

    SDL_Surface *rounded = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
    if ( !rounded ) {
        SDL_Log("ConvertSurfaceFormat failed: %s", SDL_GetError());
        return NULL;
    }
    SDL_LockSurface(rounded);

    Uint32 *pixels = rounded->pixels;
    const int pitch = rounded->pitch / 4;
    const int w = rounded->w;
    const int h = rounded->h;

    // Top-left corner
    for ( int y = 0; y < radius; y++ ) {
        for ( int x = 0; x < radius; x++ ) {
            // Distance from the corner center (at radius-0.5, radius-0.5) to the
            // pixel center (at x+0.5, y+0.5)
            const double dx = (radius - 0.5) - (x + 0.5);
            const double dy = (radius - 0.5) - (y + 0.5);
            const double dist = sqrt(dx * dx + dy * dy);

            // Calculate alpha based on distance
            long double alpha = 1.0;
            if ( dist > radius - 1.0 ) {
                alpha = fmaxl(0.0f, radius - dist);
            }

            const Uint32 pixel = pixels[y * pitch + x];
            const Uint32 current_alpha = (pixel >> 24) & 0xFF;
            const Uint32 new_alpha = (Uint8)(current_alpha * alpha);
            pixels[y * pitch + x] = (pixel & 0x00FFFFFF) | (new_alpha << 24);
        }
    }

    // Top-right corner
    for ( int y = 0; y < radius; y++ ) {
        for ( int x = w - radius; x < w; x++ ) {
            const double dx = (x + 0.5) - (w - radius + 0.5);
            const double dy = (radius - 0.5) - (y + 0.5);
            const double dist = sqrt(dx * dx + dy * dy);

            long double alpha = 1.0;
            if ( dist > radius - 1.0 ) {
                alpha = fmaxl(0.0, radius - dist);
            }

            const Uint32 pixel = pixels[y * pitch + x];
            const Uint32 current_alpha = (pixel >> 24) & 0xFF;
            const Uint32 new_alpha = (Uint8)(current_alpha * alpha);
            pixels[y * pitch + x] = (pixel & 0x00FFFFFF) | (new_alpha << 24);
        }
    }

    // Bottom-left corner
    for ( int y = h - radius; y < h; y++ ) {
        for ( int x = 0; x < radius; x++ ) {
            const double dx = (radius - 0.5) - (x + 0.5);
            const double dy = (y + 0.5) - (h - radius + 0.5);
            const double dist = sqrt(dx * dx + dy * dy);

            long double alpha = 1.0;
            if ( dist > radius - 1.0 ) {
                alpha = fmaxl(0.0, radius - dist);
            }

            const Uint32 pixel = pixels[y * pitch + x];
            const Uint32 current_alpha = (pixel >> 24) & 0xFF;
            const Uint32 new_alpha = (Uint8)(current_alpha * alpha);
            pixels[y * pitch + x] = (pixel & 0x00FFFFFF) | (new_alpha << 24);
        }
    }

    // Bottom-right corner
    for ( int y = h - radius; y < h; y++ ) {
        for ( int x = w - radius; x < w; x++ ) {
            const double dx = (x + 0.5) - (w - radius + 0.5);
            const double dy = (y + 0.5) - (h - radius + 0.5);
            const double dist = sqrt(dx * dx + dy * dy);

            long double alpha = 1.0;
            if ( dist > radius - 1.0 ) {
                alpha = fmaxl(0.0, radius - dist);
            }

            const Uint32 pixel = pixels[y * pitch + x];
            const Uint32 current_alpha = (pixel >> 24) & 0xFF;
            const Uint32 new_alpha = (Uint8)(current_alpha * alpha);
            pixels[y * pitch + x] = (pixel & 0x00FFFFFF) | (new_alpha << 24);
        }
    }

    SDL_UnlockSurface(rounded);

    return rounded;
}

etsuko_Texture_t render_make_image(const char *file_path, const int corner_radius) {
    SDL_Surface *loaded = IMG_Load(file_path);
    if ( loaded == NULL ) {
        printf("IMG_GetError: %s\n", IMG_GetError());
        error_abort("Failed to load image");
    }
    if ( corner_radius > 0 ) {
        SDL_Surface *prev = loaded;
        loaded = round_corners_surface(loaded, corner_radius);
        if ( loaded == NULL ) {
            error_abort("Failed to round corners of image");
        }
        SDL_FreeSurface(prev);
    }
    SDL_Surface *converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ABGR8888, 0);
    if ( converted == NULL ) {
        error_abort("Failed to convert image surface to appropriate pixel format");
    }
    SDL_FreeSurface(loaded);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(g_renderer->renderer, converted);
    if ( texture == NULL ) {
        error_abort("Failed to render texture from image surface");
    }

    int32_t src_w, src_h;
    SDL_QueryTexture(texture, NULL, NULL, &src_w, &src_h);
    const float w = (float)src_w, h = (float)src_h;

    SDL_Texture *final_texture = render_make_texture_target(src_w, src_h);

    const SDL_FRect destination_rect = {.x = 0, .y = 0, .w = w, .h = h};
    SDL_RenderCopyF(g_renderer->renderer, texture, NULL, &destination_rect);
    SDL_DestroyTexture(texture);

    render_restore_texture_target();

    return final_texture;
}

void render_draw_rounded_rect(const etsuko_Bounds_t *bounds, const etsuko_Color_t *color) {
    const double radius = bounds->h / 3.0;

    if ( bounds->w > 0 ) {
        // TODO: Find a way to calculate this number or, even better, not need this at all
        if ( bounds->w < 7 ) {
            SDL_SetRenderDrawColor(g_renderer->renderer, color->r, color->g, color->b, color->a);
            const SDL_FRect rect = {.x = bounds->x, .y = bounds->y, .w = bounds->w, .h = bounds->h};
            SDL_RenderFillRectF(g_renderer->renderer, &rect);
            return;
        }

        const Sint16 x1 = (Sint16)bounds->x;
        const Sint16 x2 = (Sint16)(bounds->x + bounds->w - 1);
        const Sint16 y1 = (Sint16)bounds->y;
        const Sint16 y2 = (Sint16)(bounds->y + bounds->h - 1);
        const Sint16 rad = (Sint16)radius;
        roundedBoxRGBA(g_renderer->renderer, x1, y1, x2, y2, rad, color->r, color->g, color->b, color->a);
    }
}

void render_draw_texture(etsuko_Texture_t texture, const etsuko_Bounds_t *at, const int32_t alpha_mod) {
    const SDL_FRect rect = {.x = at->x, .y = at->y, .w = at->w, .h = at->h};
    if ( rect.y < 0 || rect.y > g_renderer->viewport.h || rect.x < 0 || rect.x > g_renderer->viewport.w ) {
        return;
    }

    SDL_SetTextureAlphaMod(texture, alpha_mod);
    SDL_RenderCopyF(g_renderer->renderer, texture, NULL, &rect);
}

void render_draw_texture_no_blend(etsuko_Texture_t texture, const etsuko_Bounds_t *at, const int32_t alpha_mod) {
    const SDL_FRect rect = {.x = at->x, .y = at->y, .w = at->w, .h = at->h};
    // TODO: We're certainly drawing to a target texture, so check against that instead of against the viewport
    if ( rect.y < 0 || rect.y > g_renderer->viewport.h || rect.x < 0 || rect.x > g_renderer->viewport.w ) {
        return;
    }

    SDL_BlendMode blend;
    SDL_GetTextureBlendMode(texture, &blend);

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaMod(texture, alpha_mod);
    SDL_RenderCopyF(g_renderer->renderer, texture, NULL, &rect);

    // Restore blend mode
    SDL_SetTextureBlendMode(texture, blend);
}
