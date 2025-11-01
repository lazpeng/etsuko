#include "renderer.h"

#include <math.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "error.h"
#include "str_utils.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "constants.h"
#include "container_utils.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *ui_font, *lyrics_font;
    double h_dpi, v_dpi;
    etsuko_Bounds_t viewport;
    uint32_t bg_color;
    etsuko_Container_t root_container;
    SDL_Texture *root_texture;
} etsuko_Renderer_t;

static etsuko_Renderer_t *g_renderer;

int renderer_init(void) {
    if ( g_renderer != NULL ) {
        renderer_finish();
        g_renderer = NULL;
    }

    g_renderer = calloc(1, sizeof(etsuko_Renderer_t));
    if ( g_renderer == NULL ) {
        error_abort("Failed to allocate renderer");
    }

    // Initialize window and renderer
    const int pos = SDL_WINDOWPOS_CENTERED;
    const int flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
    g_renderer->window = SDL_CreateWindow(DEFAULT_TITLE, pos, pos, DEFAULT_WIDTH, DEFAULT_HEIGHT, flags);
    if ( g_renderer->window == NULL ) {
        return -2;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
    SDL_SetHint(SDL_HINT_RENDER_OPENGL_SHADERS, "1");
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "1");

    const int renderer_flags = SDL_RENDERER_ACCELERATED;
    g_renderer->renderer = SDL_CreateRenderer(g_renderer->window, -1, renderer_flags);
    if ( g_renderer->renderer == NULL ) {
        return -3;
    }

    g_renderer->root_container.child_containers = vec_init();
    g_renderer->root_container.child_drawables = vec_init();
    g_renderer->root_container.enabled = true;

#ifdef __EMSCRIPTEN__
    double cssWidth, cssHeight;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);

    const int32_t width = (int32_t)cssWidth;
    const int32_t height = (int32_t)cssHeight;
    SDL_SetWindowSize(g_renderer->window, width, height);
#endif

    renderer_on_window_changed();

    return 0;
}

void renderer_load_font(const etsuko_FontType_t type, const char *path) {
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

static void animation_translation_data_destroy(etsuko_Animation_EaseTranslationData_t *data) { free(data); }
static void animation_fade_data_destroy(etsuko_Animation_FadeInOutData_t *data) { free(data); }

static void animation_destroy(etsuko_Animation_t *animation) {
    if ( animation->custom_data != NULL ) {
        if ( animation->type == ANIM_EASE_TRANSLATION ) {
            animation_translation_data_destroy(animation->custom_data);
        } else if ( animation->type == ANIM_FADE_IN_OUT ) {
            animation_fade_data_destroy(animation->custom_data);
        } else {
            error_abort("Unrecognized animation type for animation_destroy");
        }
    }

    free(animation);
}

static void container_update_animations(const etsuko_Container_t *container, const double delta_time) {
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        const etsuko_Drawable_t *drawable = container->child_drawables->data[i];

        for ( size_t j = 0; j < drawable->animations->size; j++ ) {
            etsuko_Animation_t *anim = drawable->animations->data[j];

            if ( anim->elapsed < anim->duration ) {
                anim->elapsed += delta_time;
            }
        }
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        const etsuko_Container_t *child = container->child_containers->data[i];
        container_update_animations(child, delta_time);
    }
}

static void update_animations(const double delta_time) { container_update_animations(&g_renderer->root_container, delta_time); }

void renderer_begin_loop(const double delta_time) {
    const uint8_t r = g_renderer->bg_color >> 16;
    const uint8_t g = g_renderer->bg_color >> 8;
    const uint8_t b = g_renderer->bg_color & 0xFF;
    SDL_SetRenderDrawColor(g_renderer->renderer, r, g, b, 255);
    SDL_RenderClear(g_renderer->renderer);
    SDL_SetRenderDrawColor(g_renderer->renderer, 255, 255, 255, 255);

    update_animations(delta_time);
}

static void draw_dynamic_progressbar(const etsuko_Drawable_t *drawable, const etsuko_Bounds_t *base_bounds) {
    const etsuko_Drawable_ProgressBarData_t *data = (etsuko_Drawable_ProgressBarData_t *)drawable->custom_data;
    const double x = base_bounds->x + drawable->bounds.x;
    const double y = base_bounds->y + drawable->bounds.y;
    const double w = drawable->bounds.w;
    const double h = drawable->bounds.h;
    const double radius = h / 3.0;

    const etsuko_Color_t bg = data->bg_color;
    Sint16 x1 = (Sint16)x, x2 = (Sint16)(x + w - 1), y1 = (Sint16)y, y2 = (Sint16)(y + h - 1), rad = (Sint16)radius;
    roundedBoxRGBA(g_renderer->renderer, x1, y1, x2, y2, rad, bg.r, bg.g, bg.b, bg.a);

    const etsuko_Color_t fg = data->fg_color;
    const Sint16 progress_w = (Sint16)(w * data->progress);

    if ( progress_w > 0 ) {
        if ( data->progress < 0.0095 ) {
            SDL_SetRenderDrawColor(g_renderer->renderer, fg.r, fg.g, fg.b, fg.a);
            const SDL_FRect rect = {
                .x = (float)x, .y = (float)y, .w = (float)(drawable->bounds.w * data->progress), .h = (float)drawable->bounds.h};
            SDL_RenderFillRectF(g_renderer->renderer, &rect);
        } else {
            x1 = (Sint16)x;
            x2 = (Sint16)(x + progress_w - 1);
            y1 = (Sint16)y;
            y2 = (Sint16)(y + h - 1);
            rad = (Sint16)radius;
            roundedBoxRGBA(g_renderer->renderer, x1, y1, x2, y2, rad, fg.r, fg.g, fg.b, fg.a);
        }
    }
}

static void measure_layout(const etsuko_Layout_t *layout, const etsuko_Container_t *parent, etsuko_Bounds_t *out_bounds) {
    double w = layout->width, h = layout->height;
    if ( layout->width > 0 ) {
        if ( layout->flags & LAYOUT_PROPORTIONAL_W ) {
            w = parent->bounds.w * w;
        }
        if ( layout->flags & LAYOUT_SPECIAL_KEEP_ASPECT_RATIO ) {
            if ( h != 0 ) {
                error_abort("Cannot keep aspect ratio when both width and height are set");
            }
            const double aspect_ratio = out_bounds->w / out_bounds->h;
            h = w / aspect_ratio;
            out_bounds->h = h;
        }

        out_bounds->w = w;
    }

    if ( layout->height > 0 ) {

        if ( layout->flags & LAYOUT_PROPORTIONAL_H ) {
            h = parent->bounds.h * h;
        }
        if ( layout->flags & LAYOUT_SPECIAL_KEEP_ASPECT_RATIO ) {
            const double aspect_ratio = out_bounds->w / out_bounds->h;
            w = h * aspect_ratio;
            out_bounds->w = w;
        }

        out_bounds->h = h;
    }

    if ( layout->relative_to_size != NULL ) {
        if ( layout->relative_to_size->parent != parent ) {
            error_abort("Relative layout's parent is not the same as the container");
        }

        if ( (layout->flags & LAYOUT_RELATIVE_TO_SIZE) == 0 ) {
            puts("Warning: relative_to_size is set but no flag setting the "
                 "relationship was passed.");
        }

        if ( layout->flags & LAYOUT_RELATIVE_TO_WIDTH ) {
            out_bounds->w = layout->relative_to_size->bounds.w;
        }

        if ( layout->flags & LAYOUT_RELATIVE_TO_HEIGHT ) {
            out_bounds->h = layout->relative_to_size->bounds.h;
        }
    }
}

static void measure_container_size(const etsuko_Container_t *container, etsuko_Bounds_t *out_bounds) {
    // Only measure height for now
    double max_y = 0, min_y = 0;
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        const etsuko_Drawable_t *drawable = container->child_drawables->data[i];
        double draw_y;
        renderer_drawable_get_canonical_pos(drawable, NULL, &draw_y);

        max_y = fmax(max_y, draw_y + drawable->bounds.h);
        min_y = fmin(min_y, draw_y);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        const etsuko_Container_t *child = container->child_containers->data[i];
        etsuko_Bounds_t child_bounds = {0};
        measure_container_size(child, &child_bounds);
        max_y = fmax(max_y, child_bounds.y + child_bounds.h);
        min_y = fmin(min_y, child_bounds.y);
    }

    out_bounds->h = fmax(out_bounds->h, max_y - min_y);
}

static void recalculate_container_alignment(etsuko_Container_t *container) {
    if ( container->parent != NULL )
        recalculate_container_alignment(container->parent);

    if ( container->flags & CONTAINER_VERTICAL_ALIGN_CONTENT ) {
        container->align_content_offset_y = 0;
        etsuko_Bounds_t bounds = {0};
        measure_container_size(container, &bounds);
        container->align_content_offset_y = (container->bounds.h - bounds.h) / 2.0;
    }
}

static void position_layout(const etsuko_Layout_t *layout, etsuko_Container_t *parent, etsuko_Bounds_t *out_bounds) {
    double x = layout->offset_x;
    double calc_w = 0;
    if ( layout->flags & LAYOUT_ANCHOR_RIGHT_X ) {
        calc_w = out_bounds->w;
    }
    if ( layout->flags & LAYOUT_CENTER_X ) {
        x = parent->bounds.w / 2.0 - out_bounds->w / 2.0 - calc_w;
    } else if ( layout->flags & LAYOUT_PROPORTIONAL_X ) {
        x = parent->bounds.w * x;
    } else {
        if ( x < 0 && layout->flags & LAYOUT_WRAP_AROUND_X )
            x = parent->bounds.w + x - calc_w;
        else
            x -= calc_w;
    }

    double y = layout->offset_y;
    double calc_h = 0;
    if ( layout->flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
        calc_h = out_bounds->h;
    }
    if ( layout->flags & LAYOUT_CENTER_Y ) {
        y = parent->bounds.h / 2.0 - out_bounds->h / 2.0 - calc_h;
    } else if ( layout->flags & LAYOUT_PROPORTIONAL_Y ) {
        y = parent->bounds.h * y;
    } else {
        if ( y < 0 && layout->flags & LAYOUT_WRAP_AROUND_Y )
            y = parent->bounds.h + y - calc_h;
        else
            y -= calc_h;
    }

    if ( layout->relative_to != NULL ) {
        if ( layout->relative_to->parent != parent ) {
            error_abort("Relative layout's parent is not the same as the container");
        }

        if ( (layout->flags & LAYOUT_RELATIVE_TO_POS) == 0 ) {
            puts("Warning: relative_to is set but no flag setting the relationship "
                 "was passed.");
        }

        if ( layout->flags & LAYOUT_RELATIVE_TO_X ) {
            x += layout->relative_to->bounds.x;
            if ( layout->flags & LAYOUT_RELATION_X_INCLUDE_WIDTH ) {
                x += layout->relative_to->bounds.w;
            }
        }

        if ( layout->flags & LAYOUT_RELATIVE_TO_Y ) {
            y += layout->relative_to->bounds.y;
            if ( layout->flags & LAYOUT_RELATION_Y_INCLUDE_HEIGHT ) {
                y += layout->relative_to->bounds.h;
            }
        }
    }

    out_bounds->x = x;
    out_bounds->y = y;

    recalculate_container_alignment(parent);
}

static double ease_out_cubic(const double t) { return 1 - pow(1 - t, 3); }

static void apply_translation_animation(etsuko_Animation_t *animation, etsuko_Bounds_t *final_bounds) {
    const etsuko_Animation_EaseTranslationData_t *data = animation->custom_data;

    double progress = animation->elapsed / animation->duration;

    if ( progress < 1.0 ) {
        progress = data->ease ? ease_out_cubic(progress) : progress;
        const double y_delta = fabs(data->to_y - data->from_y);
        if ( fabs(y_delta) > 0.01 ) {
            const double amount = y_delta * progress - y_delta;
            final_bounds->y -= amount;
        }
    } else {
        animation->active = false;
    }
}

static void apply_fade_animation(etsuko_Animation_t *animation, int32_t *final_alpha) {
    const etsuko_Animation_FadeInOutData_t *data = animation->custom_data;

    double progress = animation->elapsed / animation->duration;

    if ( progress < 1.0 ) {
        progress = ease_out_cubic(progress);
        const int32_t alpha_delta = data->to_alpha - data->from_alpha;
        const int32_t amount = data->from_alpha + (int32_t)(alpha_delta * progress);
        *final_alpha = amount;
    } else {
        animation->target->alpha_mod = data->to_alpha;
        animation->active = false;
    }
}

struct AnimationDelta {
    etsuko_Bounds_t final_bounds;
    int32_t final_alpha;
};

static void apply_animations(const etsuko_Drawable_t *drawable, struct AnimationDelta *animation_delta) {
    for ( size_t i = 0; i < drawable->animations->size; i++ ) {
        etsuko_Animation_t *animation = drawable->animations->data[i];
        if ( animation->active ) {
            if ( animation->type == ANIM_EASE_TRANSLATION ) {
                apply_translation_animation(animation, &animation_delta->final_bounds);
            } else if ( animation->type == ANIM_FADE_IN_OUT ) {
                apply_fade_animation(animation, &animation_delta->final_alpha);
            }
        }
    }
}

static void renderer_draw(const etsuko_Drawable_t *drawable, const etsuko_Bounds_t *base_bounds) {
    if ( !drawable->enabled ) {
        return;
    }

    if ( drawable->dynamic ) {
        if ( drawable->type == DRAW_TYPE_PROGRESS_BAR ) {
            draw_dynamic_progressbar(drawable, base_bounds);
        } else {
            error_abort("Unrecognized dynamic drawable");
        }
        return;
    }

    struct AnimationDelta delta = {
        .final_bounds = drawable->bounds,
        .final_alpha = drawable->alpha_mod,
    };
    apply_animations(drawable, &delta);

    SDL_FRect rect = {.w = (float)delta.final_bounds.w, .h = (float)delta.final_bounds.h};
    rect.x = (float)(base_bounds->x + delta.final_bounds.x);
    rect.y = (float)(base_bounds->y + delta.final_bounds.y);

    if ( rect.y < 0 || rect.y > g_renderer->viewport.h || rect.x < 0 || rect.x > g_renderer->viewport.w ) {
        return;
    }

    SDL_SetTextureAlphaMod(drawable->texture, delta.final_alpha);
    SDL_RenderCopyF(g_renderer->renderer, drawable->texture, NULL, &rect);
}

static void draw_all_container(const etsuko_Container_t *container, etsuko_Bounds_t base_bounds) {
    if ( !container->enabled )
        return;

    base_bounds.x += container->bounds.x;
    base_bounds.y += container->bounds.y + container->align_content_offset_y + container->viewport_y;

    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        renderer_draw(container->child_drawables->data[i], &base_bounds);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        draw_all_container(container->child_containers->data[i], base_bounds);
    }
}

void renderer_draw_all(void) {
    const etsuko_Bounds_t bounds = {0};
    draw_all_container(&g_renderer->root_container, bounds);
}

void renderer_end_loop(void) { SDL_RenderPresent(g_renderer->renderer); }

void renderer_set_window_title(const char *title) { SDL_SetWindowTitle(g_renderer->window, title); }

void renderer_finish(void) {
    // Unload fonts
    if ( g_renderer->ui_font != NULL )
        TTF_CloseFont(g_renderer->ui_font);
    if ( g_renderer->lyrics_font != NULL )
        TTF_CloseFont(g_renderer->lyrics_font);
    // Free stored textures and drawables
    renderer_container_destroy(&g_renderer->root_container);
    // Unload sdl things
    SDL_DestroyRenderer(g_renderer->renderer);
    SDL_DestroyWindow(g_renderer->window);
    // Cleanup
    free(g_renderer);
    g_renderer = NULL;
}

void renderer_set_bg_color(const uint32_t color) { g_renderer->bg_color = color; }

etsuko_Container_t *renderer_root_container(void) { return &g_renderer->root_container; }

void renderer_drawable_get_canonical_pos(const etsuko_Drawable_t *drawable, double *x, double *y) {
    double parent_x = 0, parent_y = 0;
    renderer_container_get_canonical_pos(drawable->parent, &parent_x, &parent_y);

    if ( x != NULL )
        *x = parent_x + drawable->bounds.x;
    if ( y != NULL )
        *y = parent_y + drawable->bounds.y;
}

void renderer_container_get_canonical_pos(const etsuko_Container_t *container, double *x, double *y) {
    double parent_x = 0, parent_y = 0;
    const etsuko_Container_t *parent = container;
    while ( parent != NULL ) {
        parent_x += parent->bounds.x;
        parent_y += parent->bounds.y + parent->align_content_offset_y;
        parent = parent->parent;
    }

    if ( x != NULL )
        *x = parent_x;
    if ( y != NULL )
        *y = parent_y;
}

static etsuko_Drawable_TextData_t *dup_text_data(const etsuko_Drawable_TextData_t *data) {
    etsuko_Drawable_TextData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate text data");
    }
    result->text = strdup(data->text);
    result->font_type = data->font_type;
    result->em = data->em;
    result->wrap_enabled = data->wrap_enabled;
    result->color = data->color;
    result->bold = data->bold;
    result->wrap_enabled = data->wrap_enabled;
    result->wrap_width_threshold = data->wrap_width_threshold;
    result->measure_at_em = data->measure_at_em;
    result->alignment = data->alignment;
    result->line_padding = data->line_padding;
    return result;
}

static void free_text_data(etsuko_Drawable_TextData_t *data) {
    free(data->text);
    free(data);
}

static etsuko_Drawable_ImageData_t *dup_image_data(const etsuko_Drawable_ImageData_t *data) {
    etsuko_Drawable_ImageData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate image data");
    }
    result->file_path = strdup(data->file_path);
    result->corner_radius = data->corner_radius;
    return result;
}

static void free_image_data(etsuko_Drawable_ImageData_t *data) {
    free(data->file_path);
    free(data);
}

static etsuko_Drawable_ProgressBarData_t *dup_progressbar_data(const etsuko_Drawable_ProgressBarData_t *data) {
    etsuko_Drawable_ProgressBarData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate image data");
    }
    result->progress = data->progress;
    result->thickness = data->thickness;
    result->fg_color = data->fg_color;
    result->bg_color = data->bg_color;
    return result;
}

static void free_progressbar_data(etsuko_Drawable_ProgressBarData_t *data) { free(data); }

static int32_t em_to_pt_size(const double em) {
    const double scale = g_renderer->viewport.w / DEFAULT_WIDTH;
    const double rem = fmax(12.0, round(DEFAULT_PT * scale));
    const double pixels = em * rem;
    const int32_t pt_size = (int32_t)lround(pixels * 72.0 / g_renderer->h_dpi);
    return pt_size;
}

static int32_t measure_line_size(const char *text, const int32_t pt, int32_t *w, int32_t *h, const etsuko_FontType_t kind) {
    TTF_Font *font = kind == FONT_UI ? g_renderer->ui_font : g_renderer->lyrics_font;
    if ( TTF_SetFontSizeDPI(font, pt, (int32_t)g_renderer->h_dpi, (int32_t)g_renderer->v_dpi) != 0 ) {
        error_abort("Failed to set font size/DPI");
    }

    if ( TTF_SizeUTF8(font, text, w, h) != 0 ) {
        error_abort("Failed to measure line size");
    }

    return 0;
}

static int32_t measure_text_wrap_stop(const etsuko_Drawable_TextData_t *data, const etsuko_Container_t *container,
                                      const int32_t start) {
    const double m_current_width = container->bounds.w;
    const double calculated_max_width = m_current_width * data->wrap_width_threshold;

    const int32_t size = (int32_t)strnlen(data->text, MAX_TEXT_SIZE);
    int32_t end_idx = start == 0 ? 0 : start + 1;
    while ( true ) {
        int32_t tmp_end_idx = str_find(data->text, ' ', end_idx + 1, -1);
        if ( tmp_end_idx < 0 ) {
            tmp_end_idx = size;
        }

        if ( start == tmp_end_idx )
            break;

        int32_t measure_pt_size = 0;
        if ( data->measure_at_em != 0 ) {
            measure_pt_size = em_to_pt_size(data->measure_at_em);
        } else {
            measure_pt_size = em_to_pt_size(data->em);
        }
        int32_t w, h;
        char *dup = strndup(data->text + start, tmp_end_idx - start);
        measure_line_size(dup, measure_pt_size, &w, &h, data->font_type);
        free(dup);

        if ( w > calculated_max_width ) {
            // go with whatever was on previously
            if ( end_idx != start ) {
                // do nothing, draw using last good value
                break;
            }
            end_idx = tmp_end_idx; // Go with what we have now, even if it surpasses
                                   // the threshold,
                                   // hopefully it doesn't blow it by much
        } else {
            const int32_t backup_end_idx = end_idx;
            end_idx = tmp_end_idx;
            if ( backup_end_idx == end_idx )
                break;
        }
    }

    return end_idx;
}

static SDL_Texture *make_new_texture_target(const int32_t w, const int32_t h) {
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

static void restore_texture_target(void) {
    SDL_SetRenderTarget(g_renderer->renderer, g_renderer->root_texture);
    g_renderer->root_texture = NULL;
}

static SDL_Texture *draw_text(const char *text, const int32_t pt_size, const bool bold, const etsuko_Color_t color,
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

    const SDL_Color sdl_color = (SDL_Color){color.r, color.g, color.b, color.a};
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

static etsuko_Drawable_t *make_drawable(etsuko_Container_t *parent, const etsuko_DrawableType_t type, const bool dynamic) {
    etsuko_Drawable_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate drawable");
    }

    result->dynamic = dynamic;
    result->type = type;
    result->parent = parent;
    result->enabled = true;
    result->alpha_mod = 0xFF;
    result->animations = vec_init();

    return result;
}

static etsuko_Drawable_t *internal_make_text(etsuko_Drawable_t *result, etsuko_Drawable_TextData_t *data,
                                             const etsuko_Container_t *container, const etsuko_Layout_t *layout) {
    SDL_Texture *final_texture = NULL;

    data = dup_text_data(data);
    const size_t text_size = strnlen(data->text, MAX_TEXT_SIZE);
    if ( data->wrap_enabled && measure_text_wrap_stop(data, container, 0) < text_size ) {
        int32_t start = 0;

        Vector_t *textures_vec = vec_init();
        int32_t max_w = 0, total_h = 0;

        do {
            const int32_t end = measure_text_wrap_stop(data, container, start);
            char *line_str = strndup(data->text + start, end - start);
            const int pt_size = em_to_pt_size(data->em);
            SDL_Texture *texture = draw_text(line_str, pt_size, data->bold, data->color, data->font_type);
            free(line_str);

            vec_add(textures_vec, texture);

            int32_t w, h;
            SDL_QueryTexture(texture, NULL, NULL, &w, &h);
            max_w = MAX(max_w, w);
            if ( total_h != 0 ) {
                total_h += data->line_padding;
            }
            total_h += h;

            start = end + 1;
        } while ( start < text_size - 1 );

        final_texture = make_new_texture_target(max_w, total_h);

        double x, y = 0;
        for ( int32_t i = 0; i < textures_vec->size; i++ ) {
            SDL_Texture *texture = textures_vec->data[i];
            int32_t w, h;
            SDL_QueryTexture(texture, NULL, NULL, &w, &h);
            if ( data->alignment == ALIGN_LEFT ) {
                x = 0;
            } else if ( data->alignment == ALIGN_RIGHT ) {
                x = max_w - w;
            } else if ( data->alignment == ALIGN_CENTER ) {
                x = max_w / 2.0 - w / 2.0;
            } else {
                error_abort("Invalid alignment mode");
            }

            SDL_Rect destination = {.x = (int32_t)x, .y = (int32_t)y, .w = w, .h = h};
            // Disable blend on the texture so it doesn't lose alpha from blending multiple times
            // when rendering onto a target texture
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
            SDL_RenderCopy(g_renderer->renderer, texture, NULL, &destination);
            SDL_DestroyTexture(texture);

            y += h + data->line_padding;
        }

        vec_destroy(textures_vec);
        restore_texture_target();
    } else {
        const int32_t pt_size = em_to_pt_size(data->em);
        final_texture = draw_text(data->text, pt_size, data->bold, data->color, data->font_type);
    }

    if ( result == NULL ) {
        error_abort("Failed to allocate drawable");
    }

    int32_t final_w, final_h;
    SDL_QueryTexture(final_texture, NULL, NULL, &final_w, &final_h);

    result->bounds = (etsuko_Bounds_t){.x = result->bounds.x, .y = result->bounds.y, .w = final_w, .h = final_h};
    result->custom_data = data;
    result->texture = final_texture;
    result->layout = *layout;
    renderer_reposition_drawable(result);

    return result;
}

etsuko_Drawable_t *renderer_drawable_make_text(etsuko_Drawable_TextData_t *data, etsuko_Container_t *container,
                                               const etsuko_Layout_t *layout) {
    etsuko_Drawable_t *result = make_drawable(container, DRAW_TYPE_TEXT, false);
    internal_make_text(result, data, container, layout);
    vec_add(container->child_drawables, result);
    return result;
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
            double alpha = 1.0;
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

            double alpha = 1.0;
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

            double alpha = 1.0;
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

            double alpha = 1.0;
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

static void internal_make_image(etsuko_Drawable_t *result, etsuko_Drawable_ImageData_t *data, etsuko_Container_t *container,
                                const etsuko_Layout_t *layout) {
    data = dup_image_data(data);

    SDL_Surface *loaded = IMG_Load(data->file_path);
    if ( loaded == NULL ) {
        printf("IMG_GetError: %s\n", IMG_GetError());
        error_abort("Failed to load image");
    }
    if ( data->corner_radius > 0 ) {
        SDL_Surface *prev = loaded;
        loaded = round_corners_surface(loaded, data->corner_radius);
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

    result->bounds.w = src_w;
    result->bounds.h = src_h;

    SDL_Texture *final_texture = make_new_texture_target(src_w, src_h);

    const SDL_FRect destination_rect = {.x = 0, .y = 0, .w = w, .h = h};
    SDL_RenderCopyF(g_renderer->renderer, texture, NULL, &destination_rect);
    SDL_DestroyTexture(texture);

    restore_texture_target();

    result->custom_data = data;
    result->texture = final_texture;
    result->layout = *layout;

    renderer_reposition_drawable(result);
}

etsuko_Drawable_t *renderer_drawable_make_image(etsuko_Drawable_ImageData_t *data, etsuko_Container_t *container,
                                                const etsuko_Layout_t *layout) {
    etsuko_Drawable_t *result = make_drawable(container, DRAW_TYPE_IMAGE, false);
    internal_make_image(result, data, container, layout);
    vec_add(container->child_drawables, result);
    return result;
}

etsuko_Drawable_t *renderer_drawable_make_progressbar(const etsuko_Drawable_ProgressBarData_t *data,
                                                      etsuko_Container_t *container, const etsuko_Layout_t *layout) {
    etsuko_Drawable_t *result = make_drawable(container, DRAW_TYPE_PROGRESS_BAR, true);

    result->custom_data = dup_progressbar_data(data);
    result->bounds.h = data->thickness;
    result->layout = *layout;

    renderer_reposition_drawable(result);
    vec_add(container->child_drawables, result);
    return result;
}

void renderer_drawable_destroy(etsuko_Drawable_t *drawable) {
    SDL_DestroyTexture(drawable->texture);
    if ( drawable->custom_data != NULL ) {
        if ( drawable->type == DRAW_TYPE_TEXT ) {
            etsuko_Drawable_TextData_t *text_data = drawable->custom_data;
            free_text_data(text_data);
        } else if ( drawable->type == DRAW_TYPE_IMAGE ) {
            etsuko_Drawable_ImageData_t *image_data = drawable->custom_data;
            free_image_data(image_data);
        } else if ( drawable->type == DRAW_TYPE_PROGRESS_BAR ) {
            etsuko_Drawable_ProgressBarData_t *progress_bar_data = drawable->custom_data;
            free_progressbar_data(progress_bar_data);
        } else {
            free(drawable->custom_data);
        }
    }
    for ( size_t i = 0; i < drawable->animations->size; i++ ) {
        animation_destroy(drawable->animations->data[i]);
    }
    free(drawable);
}

etsuko_Container_t *renderer_container_make(etsuko_Container_t *parent, const etsuko_Layout_t *layout,
                                            const etsuko_ContainerFlags_t flags) {
    etsuko_Container_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate container");
    }

    result->parent = parent;
    // Make a copy of the layout
    result->layout = *layout;
    result->child_drawables = vec_init();
    result->child_containers = vec_init();
    result->enabled = true;
    result->flags = flags;
    measure_layout(layout, parent, &result->bounds);
    position_layout(layout, parent, &result->bounds);

    vec_add(parent->child_containers, result);

    return result;
}

void renderer_container_destroy(etsuko_Container_t *container) {
    for ( int i = 0; i < container->child_drawables->size; i++ ) {
        renderer_drawable_destroy(container->child_drawables->data[i]);
    }
    vec_destroy(container->child_drawables);

    for ( int i = 0; i < container->child_containers->size; i++ ) {
        renderer_container_destroy(container->child_containers->data[i]);
    }
    vec_destroy(container->child_containers);

    if ( container != &g_renderer->root_container )
        free(container);
}

static void reapply_translate_animation(etsuko_Animation_t *animation, const double old_x, const double old_y) {
    etsuko_Animation_EaseTranslationData_t *data = animation->custom_data;
    data->from_x = old_x;
    data->from_y = old_y;
    data->to_x = animation->target->bounds.x;
    data->to_y = animation->target->bounds.y;

    animation->elapsed = 0.0;
    animation->active = true;
}

void renderer_reposition_drawable(etsuko_Drawable_t *drawable) {
    const double old_x = drawable->bounds.x, old_y = drawable->bounds.y;

    measure_layout(&drawable->layout, drawable->parent, &drawable->bounds);
    position_layout(&drawable->layout, drawable->parent, &drawable->bounds);

    if ( old_x != drawable->bounds.x || old_y != drawable->bounds.y ) {
        for ( size_t i = 0; i < drawable->animations->size; i++ ) {
            etsuko_Animation_t *animation = drawable->animations->data[i];
            if ( animation->target == drawable && animation->type == ANIM_EASE_TRANSLATION ) {
                reapply_translate_animation(animation, old_x, old_y);
            }
        }
    }
}

void renderer_drawable_set_alpha(etsuko_Drawable_t *drawable, const int32_t alpha) {
    if ( alpha == drawable->alpha_mod ) {
        return;
    }

    etsuko_Animation_t *fade_animation = NULL;
    for ( size_t i = 0; i < drawable->animations->size; i++ ) {
        etsuko_Animation_t *animation = drawable->animations->data[i];
        if ( animation->type == ANIM_FADE_IN_OUT ) {
            fade_animation = animation;
            break;
        }
    }

    if ( fade_animation != NULL ) {
        etsuko_Animation_FadeInOutData_t *data = fade_animation->custom_data;

        fade_animation->elapsed = 0.0;
        fade_animation->active = true;

        data->from_alpha = drawable->alpha_mod;
        data->to_alpha = alpha;
    }
    drawable->alpha_mod = alpha;
}

void renderer_recompute_drawable(etsuko_Drawable_t *drawable) {
    etsuko_Container_t *container = drawable->parent;
    if ( drawable->type == DRAW_TYPE_TEXT ) {
        void *old_custom_data = drawable->custom_data;
        internal_make_text(drawable, old_custom_data, container, &drawable->layout);
        free_text_data(old_custom_data);
    } else if ( drawable->type == DRAW_TYPE_IMAGE ) {
        void *old_custom_data = drawable->custom_data;
        internal_make_image(drawable, old_custom_data, container, &drawable->layout);
        free_image_data(old_custom_data);
    } else if ( drawable->type == DRAW_TYPE_PROGRESS_BAR ) {
        // There's nothing to do in this case
        renderer_reposition_drawable(drawable);
    } else {
        error_abort("Invalid drawable type");
    }
}

void renderer_recompute_container(etsuko_Container_t *container) {
    if ( container->parent != NULL ) {
        measure_layout(&container->layout, container->parent, &container->bounds);
        position_layout(&container->layout, container->parent, &container->bounds);
    }

    for ( int32_t i = 0; i < container->child_drawables->size; i++ ) {
        renderer_recompute_drawable(container->child_drawables->data[i]);
    }

    for ( int32_t i = 0; i < container->child_containers->size; i++ ) {
        if ( container->child_containers->data[i] != NULL ) {
            renderer_recompute_container(container->child_containers->data[i]);
        }
    }
}

void renderer_on_window_changed(void) {
    int32_t outW, outH;
    SDL_GetRendererOutputSize(g_renderer->renderer, &outW, &outH);
    SDL_RenderSetLogicalSize(g_renderer->renderer, outW, outH);

    g_renderer->viewport = (etsuko_Bounds_t){.x = 0, .y = 0, .w = outW, .h = outH};
    g_renderer->root_container.bounds = g_renderer->viewport;

    float hdpi_temp, v_dpi_temp;
    if ( SDL_GetDisplayDPI(0, NULL, &hdpi_temp, &v_dpi_temp) != 0 ) {
        puts(SDL_GetError());
        error_abort("Failed to get DPI");
    }
    g_renderer->h_dpi = hdpi_temp;
    g_renderer->v_dpi = v_dpi_temp;

    renderer_recompute_container(&g_renderer->root_container);
}

static etsuko_Animation_EaseTranslationData_t *dup_anim_translate_data(const etsuko_Animation_EaseTranslationData_t *data) {
    etsuko_Animation_EaseTranslationData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation ease translation data");
    }

    result->from_x = data->from_x;
    result->from_y = data->from_y;
    result->to_x = data->to_x;
    result->to_y = data->to_y;
    result->duration = data->duration;
    result->ease = data->ease;

    return result;
}

static etsuko_Animation_FadeInOutData_t *dup_anim_fade_in_out_data(const etsuko_Animation_FadeInOutData_t *data) {
    etsuko_Animation_FadeInOutData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation fade in out data");
    }
    result->from_alpha = data->from_alpha;
    result->to_alpha = data->to_alpha;
    result->duration = data->duration;

    return result;
}

void renderer_animate_translation(etsuko_Drawable_t *target, const etsuko_Animation_EaseTranslationData_t *data) {
    if ( target == NULL ) {
        error_abort("Target drawable is NULL");
    }

    etsuko_Animation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_EASE_TRANSLATION;
    result->custom_data = dup_anim_translate_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;

    vec_add(target->animations, result);
}

void renderer_animate_fade(etsuko_Drawable_t *target, const etsuko_Animation_FadeInOutData_t *data) {
    if ( target == NULL ) {
        error_abort("Target drawable is NULL");
    }

    etsuko_Animation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_FADE_IN_OUT;
    result->custom_data = dup_anim_fade_in_out_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;

    vec_add(target->animations, result);
}
