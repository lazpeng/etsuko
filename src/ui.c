#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "events.h"
#include "str_utils.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "constants.h"
#include "container_utils.h"

struct Ui_t {
    Container_t root_container;
};

Ui_t *ui_init(void) {
    Ui_t *ui = calloc(1, sizeof(*ui));
    if ( ui == NULL ) {
        error_abort("Failed to allocate UI");
    }

    ui->root_container.child_containers = vec_init();
    ui->root_container.child_drawables = vec_init();
    ui->root_container.enabled = true;

    ui_on_window_changed(ui);

    return ui;
}

void ui_load_font(const unsigned char *data, const int data_size, const FontType_t type) {
    render_load_font(data, data_size, type);
}

static void animation_translation_data_destroy(Animation_EaseTranslationData_t *data) { free(data); }
static void animation_fade_data_destroy(Animation_FadeInOutData_t *data) { free(data); }
static void animation_scale_data_destroy(Animation_ScaleData_t *data) { free(data); }
static void animation_draw_region_data_destroy(Animation_DrawRegionData_t *data) { free(data); }
static void animation_scale_region_data_destroy(Animation_ScaleRegionData_t *data) { free(data); }

static void animation_destroy(Animation_t *animation, bool recursive) {
    if ( animation->custom_data != NULL ) {
        if ( animation->type == ANIM_EASE_TRANSLATION ) {
            animation_translation_data_destroy(animation->custom_data);
        } else if ( animation->type == ANIM_FADE_IN_OUT ) {
            animation_fade_data_destroy(animation->custom_data);
        } else if ( animation->type == ANIM_SCALE ) {
            animation_scale_data_destroy(animation->custom_data);
        } else if ( animation->type == ANIM_DRAW_REGION ) {
            animation_draw_region_data_destroy(animation->custom_data);
        } else if ( animation->type == ANIM_SCALE_REGION ) {
            animation_scale_region_data_destroy(animation->custom_data);
        } else {
            error_abort("Unrecognized animation type for animation_destroy");
        }
    }

    if ( recursive && animation->next != NULL )
        animation_destroy(animation->next, recursive);

    free(animation);
}

static void container_update_animations(const Container_t *container, const double delta_time) {
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        const Drawable_t *drawable = container->child_drawables->data[i];

        for ( size_t j = 0; j < drawable->active_animations->size; j++ ) {
            Animation_t *anim = drawable->active_animations->data[j];

            if ( !anim->active ) {
                // Check if we have a next animation to put in place of this one
                Animation_t *next = anim->next;
                animation_destroy(anim, false); // Destroy only the current animation
                anim = NULL;

                if ( next != NULL ) {
                    anim = next;
                    drawable->active_animations->data[j] = anim;
                } else {
                    // Do not go past this point if we have no next animation to add the elapsed time to
                    // but first delete the animation from the vector
                    vec_remove(drawable->active_animations, j);
                    j -= 1;
                    continue;
                }
            }

            if ( anim->elapsed < anim->duration ) {
                anim->elapsed += delta_time;
            }
        }
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        const Container_t *child = container->child_containers->data[i];
        container_update_animations(child, delta_time);
    }
}

static void update_animations(const Ui_t *ui, const double delta_time) {
    container_update_animations(&ui->root_container, delta_time);
}

void ui_begin_loop(Ui_t *ui) {
    if ( events_window_changed() )
        ui_on_window_changed(ui);

    render_clear();
    update_animations(ui, events_get_delta_time());
}

static void draw_dynamic_progressbar(const Drawable_t *drawable, const Bounds_t *base_bounds) {
    const Drawable_ProgressBarData_t *data = drawable->custom_data;

    Bounds_t bounds = *base_bounds;

    const float border_radius = (float)render_measure_pt_from_em(data->border_radius_em);
    render_draw_rounded_rect(drawable->texture, &bounds, &data->bg_color, border_radius);
    bounds.w *= MIN(1.0, data->progress);
    render_draw_rounded_rect(drawable->texture, &bounds, &data->fg_color, border_radius);
}

static void draw_dynamic_rectangle(const Drawable_t *drawable, const Bounds_t *bounds) {
    const Drawable_RectangleData_t *data = drawable->custom_data;

    const float border_radius = (float)render_measure_pt_from_em(data->border_radius_em);
    render_draw_rounded_rect(drawable->texture, bounds, &data->color, border_radius);
}

static void measure_layout(const Layout_t *layout, const Container_t *parent, Bounds_t *out_bounds) {
    double w = layout->width, h = layout->height;
    if ( layout->width > 0 ) {
        if ( layout->flags & LAYOUT_PROPORTIONAL_W ) {
            w = parent->bounds.w * w;
        }
    }

    if ( layout->height > 0 ) {
        if ( layout->flags & LAYOUT_PROPORTIONAL_H ) {
            h = parent->bounds.h * h;
        }
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
            w = layout->relative_to_size->bounds.w * layout->width;
        }

        if ( layout->flags & LAYOUT_RELATIVE_TO_HEIGHT ) {
            h = layout->relative_to_size->bounds.h * layout->height;
        }
    }

    const bool maintain_aspect_ratio = layout->flags & LAYOUT_SPECIAL_KEEP_ASPECT_RATIO;
    if ( maintain_aspect_ratio ) {
        // Decide based on the smaller axis
        const double aspect_ratio = out_bounds->w / out_bounds->h;

        if ( w != 0.0 && h != 0.0 ) {
            if ( w < h ) {
                h = w / aspect_ratio;
            } else {
                w = h * aspect_ratio;
            }
        } else {
            if ( h != 0.0 ) {
                w = h * aspect_ratio;
            } else if ( w != 0.0 ) {
                h = w / aspect_ratio;
            } else {
                puts("Warning: Keep aspect ratio layout has no size set.");
            }
        }
    }

    if ( layout->width != 0 || maintain_aspect_ratio )
        out_bounds->w = w;

    if ( layout->height != 0 || maintain_aspect_ratio )
        out_bounds->h = h;
}

static void measure_container_size(Ui_t *ui, const Container_t *container, Bounds_t *out_bounds) {
    // Only measure height for now
    double max_y = 0, min_y = 0;
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        const Drawable_t *drawable = container->child_drawables->data[i];
        double draw_y;
        ui_get_drawable_canon_pos(drawable, NULL, &draw_y);

        max_y = fmax(max_y, draw_y + drawable->bounds.h * (1.0 + drawable->bounds.scale_mod));
        min_y = fmin(min_y, draw_y);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        const Container_t *child = container->child_containers->data[i];
        Bounds_t child_bounds = {0};
        measure_container_size(ui, child, &child_bounds);
        max_y = fmax(max_y, child_bounds.y + child_bounds.h);
        min_y = fmin(min_y, child_bounds.y);
    }

    out_bounds->h = fmax(out_bounds->h, max_y - min_y);
}

static void recalculate_container_alignment(Ui_t *ui, Container_t *container) {
    if ( container->parent != NULL )
        recalculate_container_alignment(ui, container->parent);

    if ( container->flags & CONTAINER_VERTICAL_ALIGN_CONTENT ) {
        container->align_content_offset_y = 0;
        Bounds_t bounds = {0};
        measure_container_size(ui, container, &bounds);
        container->align_content_offset_y = (container->bounds.h - bounds.h) / 2.f;
    }
}

static void position_layout(Ui_t *ui, const Layout_t *layout, Container_t *parent, Bounds_t *out_bounds) {
    double x = layout->offset_x;
    double calc_w = 0;
    if ( layout->flags & LAYOUT_ANCHOR_RIGHT_X ) {
        calc_w = out_bounds->w;
    }
    if ( layout->flags & LAYOUT_CENTER_X ) {
        x = parent->bounds.w / 2.f - out_bounds->w / 2.f - calc_w;
    } else if ( layout->flags & LAYOUT_PROPORTIONAL_X ) {
        x = parent->bounds.w * x;
    }

    if ( x < 0 && layout->flags & LAYOUT_WRAP_AROUND_X )
        x = parent->bounds.w + x;
    x -= calc_w;

    double y = layout->offset_y;
    double calc_h = 0;
    if ( layout->flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
        calc_h = out_bounds->h;
    }
    if ( layout->flags & LAYOUT_CENTER_Y ) {
        y = parent->bounds.h / 2.f - out_bounds->h / 2.f - calc_h;
    } else if ( layout->flags & LAYOUT_PROPORTIONAL_Y ) {
        y = parent->bounds.h * y;
    }

    if ( y < 0 && layout->flags & LAYOUT_WRAP_AROUND_Y )
        y = parent->bounds.h + y;
    y -= calc_h;

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
                x += layout->relative_to->bounds.w * (1.0 + layout->relative_to->bounds.scale_mod);
            }
        }

        if ( layout->flags & LAYOUT_RELATIVE_TO_Y ) {
            y += layout->relative_to->bounds.y;
            if ( layout->flags & LAYOUT_RELATION_Y_INCLUDE_HEIGHT ) {
                y += layout->relative_to->bounds.h * (1.0 + layout->relative_to->bounds.scale_mod);
            }
        }
    }

    out_bounds->x = x;
    out_bounds->y = y;

    recalculate_container_alignment(ui, parent);
}

/* Easing functions */
static double ease_out_sine(const double t) { return sin((t * M_PI) / 2.0); }
static double ease_out_cubic(const double t) { return 1 - pow(1 - t, 3); }
static double ease_out_quad(const double t) { return 1 - (1 - t) * (1 - t); }
static double ease_out_circ(const double t) { return sqrt(1 - pow(1 - t, 2)); }

static double apply_ease_func(const double progress, const AnimationEaseType_t ease_func) {
    switch ( ease_func ) {
    case ANIM_EASE_NONE:
        break;
    case ANIM_EASE_OUT_CUBIC:
        return ease_out_cubic(progress);
    case ANIM_EASE_OUT_SINE:
        return ease_out_sine(progress);
    case ANIM_EASE_OUT_QUAD:
        return ease_out_quad(progress);
    case ANIM_EASE_OUT_CIRC:
        return ease_out_circ(progress);
    default:
        break;
    }

    return progress;
}

static void apply_translation_animation(Animation_t *animation, Bounds_t *final_bounds) {
    const Animation_EaseTranslationData_t *data = animation->custom_data;

    double progress = animation->elapsed / animation->duration;

    if ( progress < 1.0 ) {
        progress = apply_ease_func(progress, animation->ease_func);
        const double y_delta = fabs(data->to_y - data->from_y);
        if ( fabs(y_delta) > 0.01 ) {
            const double amount = y_delta * progress - y_delta;
            final_bounds->y -= amount;
        }
    } else {
        animation->active = false;
    }
}

static void apply_fade_animation(Animation_t *animation, int32_t *final_alpha) {
    const Animation_FadeInOutData_t *data = animation->custom_data;

    double progress = animation->elapsed / animation->duration;

    if ( progress < 1.0 ) {
        progress = apply_ease_func(progress, animation->ease_func);
        const int32_t alpha_delta = data->to_alpha - data->from_alpha;
        const int32_t amount = data->from_alpha + (int32_t)(alpha_delta * progress);
        *final_alpha = amount;
    } else {
        animation->target->alpha_mod = data->to_alpha;
        animation->active = false;
    }
}

static void apply_scale_animation(Animation_t *animation, Bounds_t *final_bounds) {
    const Animation_ScaleData_t *data = animation->custom_data;

    const double progress = animation->elapsed / animation->duration;
    if ( progress < 1.0 ) {
        const double scale_delta = data->to_scale - data->from_scale;
        final_bounds->scale_mod = data->from_scale + scale_delta * progress;
    } else {
        animation->active = false;
    }
}

static void apply_draw_region_animation(Animation_t *animation, DrawRegionOptSet_t *regions) {
    const Animation_DrawRegionData_t *data = animation->custom_data;
    double progress = animation->elapsed / animation->duration;
    if ( progress < 1.0 ) {
        progress = apply_ease_func(progress, animation->ease_func);
        for ( int i = 0; i < regions->num_regions; i++ ) {
            // Naively consider that just the end indexes that are going to change
            const float delta_x = regions->regions[i].x1_perc - data->draw_regions.regions[i].x1_perc;
            // const float delta_y = drawable->regions[i][3] - data->prev_regions[i][3];
            regions->regions[i].x1_perc = (float)(data->draw_regions.regions[i].x1_perc + delta_x * progress);
            // TODO: Figure out a way to include animating the y axis but for now leave it commented
            // final_regions[i][3] = data->prev_regions[i][3] + delta_y * (float)progress;
        }
    } else {
        animation->active = false;
    }
}

static void apply_scale_region_animation(Animation_t *animation, ScaleRegionOptSet_t *regions) {
    const Animation_ScaleRegionData_t *data = animation->custom_data;
    double progress = animation->elapsed / animation->duration;

    if ( progress >= 1.0 ) {
        progress = 1.0;
        animation->active = false;
    }

    // progress = apply_ease_func(progress, data->ease_func);
    // Consider this will be the num_region'th region of the final set
    if ( regions->num_regions >= MAX_SCALE_SUB_REGIONS ) {
        error_abort("apply_scale_region_animation: Max number of scale regions exceeded");
    }
    const float scale_diff = data->scale_region.to_scale - data->scale_region.from_scale;
    ScaleRegionOpt_t *opt = &regions->regions[regions->num_regions++];
    opt->x0_perc = data->scale_region.x0_perc;
    opt->x1_perc = data->scale_region.x1_perc;
    opt->y0_perc = data->scale_region.y0_perc;
    opt->y1_perc = data->scale_region.y1_perc;
    opt->relative_scale = data->scale_region.from_scale + scale_diff * progress;
    opt->from_scale = data->scale_region.from_scale;
    opt->to_scale = data->scale_region.to_scale;
}

typedef struct AnimationDelta {
    Bounds_t final_bounds;
    int32_t final_alpha;
    float color_mod;
    DrawRegionOptSet_t draw_regions;
    ScaleRegionOptSet_t scale_regions;
} AnimationDelta;

static void apply_animations(const Drawable_t *drawable, AnimationDelta *animation_delta) {
    for ( size_t i = 0; i < drawable->active_animations->size; i++ ) {
        Animation_t *animation = drawable->active_animations->data[i];
        // This is probably not needed anymore...
        if ( animation->active ) {
            if ( animation->type == ANIM_EASE_TRANSLATION ) {
                apply_translation_animation(animation, &animation_delta->final_bounds);
            } else if ( animation->type == ANIM_FADE_IN_OUT ) {
                apply_fade_animation(animation, &animation_delta->final_alpha);
            } else if ( animation->type == ANIM_SCALE ) {
                apply_scale_animation(animation, &animation_delta->final_bounds);
            } else if ( animation->type == ANIM_DRAW_REGION ) {
                apply_draw_region_animation(animation, &animation_delta->draw_regions);
            } else if ( animation->type == ANIM_SCALE_REGION ) {
                apply_scale_region_animation(animation, &animation_delta->scale_regions);
            }
        }
    }
}

static void perform_draw(const Drawable_t *drawable, const Bounds_t *base_bounds) {
    if ( !drawable->enabled || drawable->pending_recompute ) {
        return;
    }

    AnimationDelta delta = {.final_bounds = drawable->bounds,
                            .final_alpha = drawable->alpha_mod,
                            .color_mod = drawable->color_mod,
                            .draw_regions = {0}};
    delta.draw_regions = drawable->draw_regions;
    apply_animations(drawable, &delta);

    Bounds_t rect = delta.final_bounds;
    rect.x += base_bounds->x;
    rect.y += base_bounds->y;

    if ( drawable->dynamic ) {
        if ( drawable->type == DRAW_TYPE_PROGRESS_BAR ) {
            draw_dynamic_progressbar(drawable, &rect);
        } else if ( drawable->type == DRAW_TYPE_RECTANGLE ) {
            draw_dynamic_rectangle(drawable, &rect);
        } else {
            error_abort("Unrecognized dynamic drawable");
        }
        return;
    }

    DrawTextureOpts_t opts = {0};
    opts.scale_regions = &delta.scale_regions;
    if ( drawable->shadow != NULL ) {
        Bounds_t shadow_bounds = rect;
        shadow_bounds.w = drawable->shadow->bounds.w;
        shadow_bounds.h = drawable->shadow->bounds.h;
        const int32_t max_alpha = drawable->type == DRAW_TYPE_IMAGE ? 50 : 128;
        const uint8_t alpha = MIN(max_alpha, drawable->alpha_mod);
        opts.alpha_mod = alpha;
        opts.color_mod = 0.f;
        render_draw_texture(drawable->shadow->texture, &shadow_bounds, &opts);
    }

    opts.color_mod = delta.color_mod;
    if ( drawable->draw_underlay ) {
        opts.alpha_mod = drawable->underlay_alpha;
        render_draw_texture(drawable->texture, &rect, &opts);
    }

    opts.alpha_mod = delta.final_alpha;
    opts.draw_regions = &delta.draw_regions;
    render_draw_texture(drawable->texture, &rect, &opts);
}

static void draw_all_container(const Container_t *container, Bounds_t base_bounds) {
    if ( !container->enabled )
        return;

    base_bounds.x += container->bounds.x;
    base_bounds.y += container->bounds.y + container->align_content_offset_y + container->viewport_y;

    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        perform_draw(container->child_drawables->data[i], &base_bounds);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        draw_all_container(container->child_containers->data[i], base_bounds);
    }
}

void ui_draw(const Ui_t *ui) {
    const Bounds_t bounds = {0};
    draw_all_container(&ui->root_container, bounds);
}

void ui_end_loop(void) { render_present(); }

void ui_set_window_title(const char *title) { render_set_window_title(title); }

void ui_finish(Ui_t *ui) {
    // Free stored textures and drawables
    ui_destroy_container(ui, &ui->root_container);
    // Cleanup
    free(ui);
}

void ui_set_bg_color(const uint32_t color) { render_set_bg_color(render_color_parse(color)); }

void ui_set_bg_gradient(const uint32_t primary, const uint32_t secondary, const BackgroundType_t type) {
    const Color_t primary_color = render_color_parse(primary);
    const Color_t secondary_color = render_color_parse(secondary);
    render_set_bg_gradient(primary_color, secondary_color, type);
}

void ui_sample_bg_colors_from_image(const unsigned char *bytes, const int length) {
    render_sample_bg_colors_from_image(bytes, length);
}

Container_t *ui_root_container(Ui_t *ui) { return &ui->root_container; }

void ui_get_drawable_canon_pos(const Drawable_t *drawable, double *x, double *y) {
    double parent_x = 0, parent_y = 0;
    ui_get_container_canon_pos(drawable->parent, &parent_x, &parent_y, true);

    if ( x != NULL )
        *x = parent_x + drawable->bounds.x;
    if ( y != NULL )
        *y = parent_y + drawable->bounds.y;
}

void ui_get_container_canon_pos(const Container_t *container, double *x, double *y, const bool include_viewport_offset) {
    double parent_x = 0, parent_y = 0;
    const Container_t *parent = container;
    while ( parent != NULL ) {
        parent_x += parent->bounds.x;
        parent_y += parent->bounds.y + parent->align_content_offset_y;
        if ( include_viewport_offset )
            parent_y += parent->viewport_y;
        parent = parent->parent;
    }

    if ( x != NULL )
        *x = parent_x;
    if ( y != NULL )
        *y = parent_y;
}

bool ui_mouse_hovering_container(const Container_t *container, Bounds_t *out_canon_bounds, int32_t *out_mouse_x,
                                 int32_t *out_mouse_y) {
    double canon_x, canon_y;
    ui_get_container_canon_pos(container, &canon_x, &canon_y, false);

    int32_t mouse_x, mouse_y;
    events_get_mouse_position(&mouse_x, &mouse_y);

    if ( out_canon_bounds ) {
        out_canon_bounds->x = canon_x;
        out_canon_bounds->y = canon_y;
        out_canon_bounds->w = container->bounds.w;
        out_canon_bounds->h = container->bounds.h;
    }

    if ( out_mouse_x )
        *out_mouse_x = mouse_x;
    if ( out_mouse_y )
        *out_mouse_y = mouse_y;

    return mouse_x >= canon_x && mouse_x <= canon_x + container->bounds.w && mouse_y >= canon_y &&
           mouse_y <= canon_y + container->bounds.h;
}

static Drawable_TextData_t *dup_text_data(const Drawable_TextData_t *data) {
    Drawable_TextData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate text data");
    }
    result->text = strdup(data->text);
    result->font_type = data->font_type;
    result->em = data->em;
    result->wrap_enabled = data->wrap_enabled;
    result->color = data->color;
    result->wrap_enabled = data->wrap_enabled;
    result->wrap_width_threshold = data->wrap_width_threshold;
    result->measure_at_em = data->measure_at_em;
    result->alignment = data->alignment;
    result->line_padding_em = data->line_padding_em;
    result->draw_shadow = data->draw_shadow;
    result->compute_offsets = data->compute_offsets;
    return result;
}

static void free_text_line_offsets(Drawable_TextData_t *data) {
    for ( size_t i = 0; i < data->line_offsets->size; i++ ) {
        TextOffsetInfo_t *info = data->line_offsets->data[i];
        for ( size_t j = 0; j < info->char_offsets->size; j++ ) {
            CharOffsetInfo_t *char_info = info->char_offsets->data[j];
            free(char_info);
        }
        vec_destroy(info->char_offsets);
        free(info);
    }
    vec_destroy(data->line_offsets);
    data->line_offsets = NULL;
}

static void free_text_data(Drawable_TextData_t *data) {
    free(data->text);
    if ( data->line_offsets != NULL ) {
        free_text_line_offsets(data);
    }
    free(data);
}

static Drawable_ImageData_t *dup_image_data(const Drawable_ImageData_t *data) {
    Drawable_ImageData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate image data");
    }
    result->border_radius_em = data->border_radius_em;
    result->draw_shadow = data->draw_shadow;
    return result;
}

static void free_image_data(Drawable_ImageData_t *data) { free(data); }

static Drawable_ProgressBarData_t *dup_progressbar_data(const Drawable_ProgressBarData_t *data) {
    Drawable_ProgressBarData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate progress bar data");
    }
    result->progress = data->progress;
    result->border_radius_em = data->border_radius_em;
    result->fg_color = data->fg_color;
    result->bg_color = data->bg_color;
    return result;
}

static Drawable_RectangleData_t *dup_rectangle_data(const Drawable_RectangleData_t *data) {
    Drawable_RectangleData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate rectangle data");
    }
    result->color = data->color;
    return result;
}

static void free_progressbar_data(Drawable_ProgressBarData_t *data) { free(data); }
static void free_rectangle_data(Drawable_RectangleData_t *data) { free(data); }

static int32_t measure_text_wrap_stop(const Drawable_TextData_t *data, const Container_t *container, const int32_t start) {
    const double m_current_width = container->bounds.w;
    const double calculated_max_width = m_current_width * data->wrap_width_threshold;

    const char *text = data->text;
    const int32_t size = (int32_t)strlen(text);

    if ( start >= size )
        return size;

    /**
     * Wrap logic should be as follows:
     * - In regular, latin-script text, break on spaces (it is impossible for it to contain newlines in the first place)
     * - In japanese text, try to break at the longest particle or punctuation point immediately after a kanji after we
     * exceeded the maximum width. If that fails, break on the first kana (non-kanji) after we exceeded the max width.
     * Obs.: if the second line is too small, and the whole thing still fits in 95% of the container width (no matter the
     * max width that has been set), we don't break the line
     */

    int32_t measure_pixels_size = 0;
    if ( data->measure_at_em != 0 ) {
        measure_pixels_size = render_measure_pixels_from_em(data->measure_at_em);
    } else {
        measure_pixels_size = render_measure_pixels_from_em(data->em);
    }

    double current_line_width = 0;
    int32_t current_idx = start;
    int32_t last_safe_break_idx = -1;
    int32_t last_particle_break_idx = -1;

    int32_t prev_c = -1;
    bool is_japanese_context = false;

    while ( current_idx < size ) {
        const int32_t char_start_idx = current_idx;
        int32_t c;
        c = str_u8_next(text, size, &current_idx);
        if ( c < 0 )
            break;

        if ( !is_japanese_context ) {
            if ( str_ch_is_kanji(c) || str_ch_is_kana(c) ) {
                is_japanese_context = true;
            }
        }

        CharBounds_t char_bounds;
        render_measure_char_bounds(c, prev_c, measure_pixels_size, &char_bounds, data->font_type);
        current_line_width += char_bounds.width;

        if ( c == ' ' ) {
            last_safe_break_idx = char_start_idx + 1;
        } else if ( str_ch_is_japanese_particle(c) || str_ch_is_japanese_punctuation(c) ) {
            if ( str_ch_is_japanese_punctuation(c) ) {
                // If we're breaking on a punctuation character, include it in the line as it looks weird if it's the first
                // character on the following line
                last_particle_break_idx = current_idx;
            } else {
                last_particle_break_idx = char_start_idx;
            }
        }

        if ( current_line_width > calculated_max_width ) {
            if ( current_idx == size && current_line_width <= m_current_width * 0.95 ) {
                return size;
            }

            if ( is_japanese_context && last_particle_break_idx != -1 && last_particle_break_idx > start ) {
                return last_particle_break_idx;
            }

            if ( last_safe_break_idx != -1 && last_safe_break_idx > start ) {
                return last_safe_break_idx;
            }

            if ( is_japanese_context ) {
                bool last_was_kanji = str_ch_is_kanji(c);

                while ( current_idx < size ) {
                    int32_t next_c;
                    const int32_t prev_current_idx = current_idx;
                    // U8_NEXT(text, current_idx, size, next_c);
                    next_c = str_u8_next(text, size, &current_idx);
                    if ( next_c < 0 )
                        break;

                    if ( last_was_kanji && str_ch_is_kana(next_c) ) {
                        return prev_current_idx;
                    }

                    last_was_kanji = str_ch_is_kanji(next_c);
                }

                // If we ran out of text, just return end.
                return size;
            }

            // when not breaking japanese text
            return (char_start_idx > start) ? (char_start_idx - 1) : (current_idx - 1);
        }

        prev_c = c;
    }

    return size;
}

static Drawable_t *make_drawable(Container_t *parent, const DrawableType_t type, const bool dynamic) {
    Drawable_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate drawable");
    }

    result->dynamic = dynamic;
    result->type = type;
    result->parent = parent;
    result->enabled = true;
    result->alpha_mod = 0xFF;
    result->color_mod = 1.f;
    result->animations = vec_init();
    result->active_animations = vec_init();

    return result;
}

/**
 * Partially computes text offsets character by character. Some of the information is later populated by internal_make_text.
 */
static void internal_partial_compute_text_offsets(const Drawable_TextData_t *data, const char *line, const int32_t byte_offset) {
    const size_t text_size = strlen(line);
    const int32_t pixels_size = render_measure_pixels_from_em(data->em);

    const TextOffsetInfo_t *prev = data->line_offsets->size > 0 ? data->line_offsets->data[data->line_offsets->size - 1] : NULL;

    TextOffsetInfo_t *info = calloc(1, sizeof(*info));
    if ( info == NULL ) {
        error_abort("Failed to allocate text offset info");
    }
    vec_add(data->line_offsets, info);
    info->char_offsets = vec_init();
    info->start_byte_offset = byte_offset;
    info->start_char_idx = prev != NULL ? prev->start_char_idx + prev->num_chars : 0;
    info->num_chars = 0;

    double x = 0;
    int32_t prev_c = -1;
    for ( int32_t i = 0; i < (int32_t)text_size; ) {
        const int32_t prev_i = i;

        int32_t c;
        // U8_NEXT(line, i, text_size, c);
        c = str_u8_next(line, text_size, &i);
        if ( c < 0 )
            break;

        CharBounds_t char_bounds;
        render_measure_char_bounds(c, prev_c, pixels_size, &char_bounds, data->font_type);

        CharOffsetInfo_t *char_info = calloc(1, sizeof(*char_info));
        if ( char_info == NULL ) {
            error_abort("Failed to allocate char offset info");
        }
        char_info->start_byte_offset = prev_i;
        char_info->char_idx = info->num_chars++;
        char_info->height = char_bounds.font_height;
        char_info->width = char_bounds.width;
        char_info->end_byte_offset = byte_offset + i;
        char_info->x = x;
        char_info->y = 0; // Will be computed later

        info->end_byte_offset = byte_offset + i;
        info->width += char_bounds.width;
        info->height = char_info->height;

        x += char_info->width;

        vec_add(info->char_offsets, char_info);

        prev_c = c;
    }
}

static Drawable_t *internal_make_text(Ui_t *ui, Drawable_t *result, const Drawable_TextData_t *weak_data,
                                      const Container_t *container, const Layout_t *layout) {
    Texture_t *final_texture;

    Drawable_TextData_t *data = dup_text_data(weak_data);
    const bool should_compute_offsets =
        data->compute_offsets && (config_get()->enable_dynamic_fill || config_get()->enable_reading_hints);

    if ( should_compute_offsets ) {
        if ( data->line_offsets != NULL ) {
            // TODO: Maybe check if we really need to recompute this
            free_text_line_offsets(data);
        }
        data->line_offsets = vec_init();
    }

    const int32_t line_padding = render_measure_pixels_from_em(data->line_padding_em);

    const size_t text_size = strlen(data->text);
    if ( data->wrap_enabled && measure_text_wrap_stop(data, container, 0) < (int32_t)text_size ) {
        size_t start = 0;

        Vector_t *textures_vec = vec_init();
        int32_t max_w = 0, total_h = 0;

        do {
            const size_t end = measure_text_wrap_stop(data, container, (int32_t)start);
            char *line_str = strndup(data->text + start, end - start);
            const int pixels_size = render_measure_pixels_from_em(data->em);
            Texture_t *texture = render_make_text(line_str, pixels_size, &data->color, data->font_type);

            if ( should_compute_offsets ) {
                internal_partial_compute_text_offsets(data, line_str, (int32_t)start);
            }
            free(line_str);

            vec_add(textures_vec, texture);

            max_w = MAX(max_w, texture->width);
            if ( total_h != 0 ) {
                total_h += line_padding;
            }
            total_h += texture->height;

            start = end;
        } while ( start < text_size - 1 );

        const RenderTarget_t *target = render_make_texture_target(max_w, total_h);
        final_texture = target->texture;

        const DrawTextureOpts_t opts = {.color_mod = 1.f, .alpha_mod = 255};
        double x, y = 0;
        for ( size_t i = 0; i < textures_vec->size; i++ ) {
            Texture_t *texture = textures_vec->data[i];
            if ( data->alignment == ALIGN_LEFT ) {
                x = 0;
            } else if ( data->alignment == ALIGN_RIGHT ) {
                x = max_w - texture->width;
            } else if ( data->alignment == ALIGN_CENTER ) {
                x = max_w / 2.0 - texture->width / 2.0;
            } else {
                error_abort("Invalid alignment mode");
            }

            if ( data->compute_offsets ) {
                TextOffsetInfo_t *info = data->line_offsets->data[i];
                info->start_x = x;
                info->start_y = y;
            }

            Bounds_t destination = {.x = x, .y = y, .w = (float)texture->width, .h = (float)texture->height};
            // Disable blend on the texture so it doesn't lose alpha from blending multiple times
            // when rendering onto a target texture
            const BlendMode_t blend_mode = render_get_blend_mode();
            render_set_blend_mode(BLEND_MODE_NONE);
            render_draw_texture(texture, &destination, &opts);
            y += texture->height + line_padding;

            render_set_blend_mode(blend_mode);

            render_destroy_texture(texture);
        }

        vec_destroy(textures_vec);
        render_restore_texture_target();
    } else {
        const int32_t pixels_size = render_measure_pixels_from_em(data->em);
        final_texture = render_make_text(data->text, pixels_size, &data->color, data->font_type);

        if ( should_compute_offsets ) {
            internal_partial_compute_text_offsets(data, data->text, 0);
        }
    }

    if ( result == NULL ) {
        error_abort("Failed to allocate drawable");
    }

    result->bounds.w = final_texture->width;
    result->bounds.h = final_texture->height;
    result->custom_data = data;
    result->texture = final_texture;
    result->layout = *layout;
    ui_reposition_drawable(ui, result);

    if ( data->draw_shadow ) {
        const int32_t text_pixels = render_measure_pixels_from_em(data->em);
        const int32_t offset = (int32_t)MAX(1.f, MIN(10.f, text_pixels * 0.1f));
        const float blur_radius = (float)data->em; // Make blur radius relative to text size in a shitty way
        result->shadow = render_make_shadow(result->texture, &result->bounds, blur_radius, offset);
    }

    return result;
}

Drawable_t *ui_make_text(Ui_t *ui, const Drawable_TextData_t *data, Container_t *container, const Layout_t *layout) {
    Drawable_t *result = make_drawable(container, DRAW_TYPE_TEXT, false);
    internal_make_text(ui, result, data, container, layout);
    vec_add(container->child_drawables, result);
    return result;
}

static void apply_shadow_to_image(Drawable_t *drawable) {
    if ( drawable->shadow ) {
        render_destroy_shadow(drawable->shadow);
    }
    const int32_t offset = MAX(1, drawable->bounds.w * 0.01f);
    drawable->shadow = render_make_shadow(drawable->texture, &drawable->bounds, 1.f, offset);
}

Drawable_t *ui_make_image(Ui_t *ui, const unsigned char *bytes, const int length, const Drawable_ImageData_t *weak_data,
                          Container_t *container, const Layout_t *layout) {
    Drawable_t *result = make_drawable(container, DRAW_TYPE_IMAGE, false);
    Drawable_ImageData_t *data = dup_image_data(weak_data);

    Texture_t *texture = render_make_image(bytes, length, data->border_radius_em);
    result->bounds.w = texture->width;
    result->bounds.h = texture->height;

    result->custom_data = data;
    result->texture = texture;
    result->layout = *layout;

    ui_reposition_drawable(ui, result);

    if ( data->draw_shadow ) {
        apply_shadow_to_image(result);
    }
    vec_add(container->child_drawables, result);
    return result;
}

Drawable_t *ui_make_progressbar(Ui_t *ui, const Drawable_ProgressBarData_t *data, Container_t *container,
                                const Layout_t *layout) {
    Drawable_t *result = make_drawable(container, DRAW_TYPE_PROGRESS_BAR, true);

    result->texture = render_make_null();
    result->custom_data = dup_progressbar_data(data);
    result->layout = *layout;

    ui_reposition_drawable(ui, result);
    vec_add(container->child_drawables, result);
    return result;
}

Drawable_t *ui_make_rectangle(Ui_t *ui, const Drawable_RectangleData_t *data, Container_t *container, const Layout_t *layout) {
    Drawable_t *result = make_drawable(container, DRAW_TYPE_RECTANGLE, true);

    result->texture = render_make_null();
    result->custom_data = dup_rectangle_data(data);
    result->layout = *layout;

    ui_reposition_drawable(ui, result);
    vec_add(container->child_drawables, result);
    return result;
}

Drawable_t *ui_make_custom(Ui_t *ui, Container_t *container, const Layout_t *layout) {
    Drawable_t *result = make_drawable(container, DRAW_TYPE_CUSTOM_TEXTURE, false);

    result->texture = render_make_null();
    result->custom_data = NULL;
    result->layout = *layout;
    result->pending_recompute = true;

    ui_reposition_drawable(ui, result);
    vec_add(container->child_drawables, result);
    return result;
}

void ui_destroy_drawable(Drawable_t *drawable) {
    if ( drawable->texture != NULL ) {
        render_destroy_texture(drawable->texture);
    }
    if ( drawable->shadow != NULL ) {
        render_destroy_shadow(drawable->shadow);
    }
    if ( drawable->custom_data != NULL ) {
        if ( drawable->type == DRAW_TYPE_TEXT ) {
            Drawable_TextData_t *text_data = drawable->custom_data;
            free_text_data(text_data);
        } else if ( drawable->type == DRAW_TYPE_IMAGE ) {
            Drawable_ImageData_t *image_data = drawable->custom_data;
            free_image_data(image_data);
        } else if ( drawable->type == DRAW_TYPE_PROGRESS_BAR ) {
            Drawable_ProgressBarData_t *progress_bar_data = drawable->custom_data;
            free_progressbar_data(progress_bar_data);
        } else if ( drawable->type == DRAW_TYPE_RECTANGLE ) {
            Drawable_RectangleData_t *rectangle_data = drawable->custom_data;
            free_rectangle_data(rectangle_data);
        } else if ( drawable->type == DRAW_TYPE_CUSTOM_TEXTURE ) {
            // Custom drawables can mantain weak references to pieces of custom data, but they must also save a pointer to it
            // elsewhere and free it there Ideally whoever created the drawable should be the one to, ultimately, destroy it, or
            // else it'll have a dangling pointer ready to do some use-after-free Gotta think this design a little better later
        } else {
            error_abort("Unknown drawable type");
        }
    }
    for ( size_t i = 0; i < drawable->active_animations->size; i++ ) {
        animation_destroy(drawable->active_animations->data[i], true);
    }
    for ( size_t i = 0; i < drawable->animations->size; i++ ) {
        animation_destroy(drawable->animations->data[i], true);
    }
    // Find the drawable in the scene graph
    const Container_t *parent = drawable->parent;
    for ( size_t i = 0; i < parent->child_drawables->size; i++ ) {
        if ( parent->child_drawables->data[i] == drawable ) {
            vec_remove(parent->child_drawables, i);
            break;
        }
    }
    free(drawable);
}

double ui_compute_relative_horizontal(Ui_t *ui, double value, Container_t *parent) { return parent->bounds.w * value; }

Container_t *ui_make_container(Ui_t *ui, Container_t *parent, const Layout_t *layout, const ContainerFlags_t flags) {
    Container_t *result = calloc(1, sizeof(*result));
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
    position_layout(ui, layout, parent, &result->bounds);

    vec_add(parent->child_containers, result);

    return result;
}

void ui_destroy_container(Ui_t *ui, Container_t *container) {
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        ui_destroy_drawable(container->child_drawables->data[i]);
    }
    vec_destroy(container->child_drawables);

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        ui_destroy_container(ui, container->child_containers->data[i]);
    }
    vec_destroy(container->child_containers);

    if ( container != &ui->root_container )
        free(container);
}

static void reapply_translate_animation(Animation_t *animation, const double old_x, const double old_y) {
    Animation_EaseTranslationData_t *data = animation->custom_data;
    data->from_x = old_x;
    data->from_y = old_y;
    data->to_x = animation->target->bounds.x;
    data->to_y = animation->target->bounds.y;

    animation->elapsed = 0.0;
    animation->active = true;
}

static Animation_t *find_animation(const Drawable_t *drawable, const AnimationType_t type) {
    for ( size_t i = 0; i < drawable->animations->size; i++ ) {
        Animation_t *animation = drawable->animations->data[i];
        if ( animation->type == type ) {
            return animation;
        }
    }
    return NULL;
}

static Animation_t *find_active_animation(const Drawable_t *drawable, const AnimationType_t type) {
    // Traverse backwards so we find the most recent animation
    for ( int32_t i = drawable->active_animations->size - 1; i >= 0; i-- ) {
        Animation_t *animation = drawable->active_animations->data[i];
        if ( animation->type == type ) {
            return animation;
        }
    }
    return NULL;
}

void ui_recompute_drawable(Ui_t *ui, Drawable_t *drawable) {
    const Container_t *container = drawable->parent;
    if ( drawable->type == DRAW_TYPE_TEXT ) {
        void *old_custom_data = drawable->custom_data;
        // TODO: Maybe we don't need to destroy the texture and realloc/remake it? just make a new opengl texture
        //  and reconfigure the VBA/VAO
        if ( drawable->texture != NULL ) {
            render_destroy_texture(drawable->texture);
            drawable->texture = NULL;
        }
        internal_make_text(ui, drawable, old_custom_data, container, &drawable->layout);
        free_text_data(old_custom_data);
    } else if ( drawable->type == DRAW_TYPE_IMAGE ) {
        const Drawable_ImageData_t *data = drawable->custom_data;
        ui_reposition_drawable(ui, drawable);
        if ( data->draw_shadow ) {
            apply_shadow_to_image(drawable);
        }
    } else if ( drawable->type == DRAW_TYPE_PROGRESS_BAR || drawable->type == DRAW_TYPE_RECTANGLE ) {
        ui_reposition_drawable(ui, drawable);
    } else if ( drawable->type == DRAW_TYPE_CUSTOM_TEXTURE ) {
        // It should recompute itself inside some loop() function somewhere
        drawable->pending_recompute = true;
    } else {
        error_abort("Invalid drawable type");
    }
}

void ui_recompute_container(Ui_t *ui, Container_t *container) {
    if ( container->parent != NULL ) {
        measure_layout(&container->layout, container->parent, &container->bounds);
        position_layout(ui, &container->layout, container->parent, &container->bounds);
    }

    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        ui_recompute_drawable(ui, container->child_drawables->data[i]);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        if ( container->child_containers->data[i] != NULL ) {
            ui_recompute_container(ui, container->child_containers->data[i]);
        }
    }
}

void ui_on_window_changed(Ui_t *ui) {
    render_on_window_changed();
    ui->root_container.bounds = *render_get_viewport();
    ui_recompute_container(ui, &ui->root_container);
}

static Animation_EaseTranslationData_t *dup_anim_translate_data(const Animation_EaseTranslationData_t *data) {
    Animation_EaseTranslationData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation ease translation data");
    }

    result->from_x = data->from_x;
    result->from_y = data->from_y;
    result->to_x = data->to_x;
    result->to_y = data->to_y;
    result->duration = data->duration;
    result->ease_func = data->ease_func;

    return result;
}

static Animation_FadeInOutData_t *dup_anim_fade_in_out_data(const Animation_FadeInOutData_t *data) {
    Animation_FadeInOutData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation fade in out data");
    }
    result->from_alpha = data->from_alpha;
    result->to_alpha = data->to_alpha;
    result->duration = data->duration;
    result->ease_func = data->ease_func;

    return result;
}

static Animation_ScaleData_t *dup_anim_scale_data(const Animation_ScaleData_t *data) {
    Animation_ScaleData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation scale data");
    }
    result->from_scale = data->from_scale;
    result->to_scale = data->to_scale;
    result->duration = data->duration;
    return result;
}

static Animation_DrawRegionData_t *dup_anim_draw_region_data(const Animation_DrawRegionData_t *data) {
    Animation_DrawRegionData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation draw region data");
    }
    result->duration = data->duration;
    result->ease_func = data->ease_func;

    return result;
}

static Animation_ScaleRegionData_t *dup_anim_scale_region_data(const Animation_ScaleRegionData_t *data) {
    Animation_ScaleRegionData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation scale region data");
    }
    result->duration = data->duration;
    result->default_apply = data->default_apply;
    result->ease_func = data->ease_func;

    return result;
}

/**
 * Attempts to (re)apply the given animation to a certain drawable, applying the apply rule (with a possible override)
 */
static Animation_t *reapply_animation(const Drawable_t *drawable, const Animation_t *base_anim,
                                      const AnimationApplyType_t apply_type) {
    // First check if we won't actually apply the animation.
    Animation_t *existing = find_active_animation(drawable, base_anim->type);
    if ( apply_type == ANIM_APPLY_BLOCK && existing != NULL ) {
        return NULL; // There's already one running and we should block until it's done
    }
    if ( apply_type == ANIM_APPLY_OVERRIDE && existing != NULL ) {
        // Reuse the current animation, just reset the elapsed
        existing->elapsed = 0.0;
        existing->active = true;
        return existing;
    }
    // duplicate the animation
    Animation_t *animation = calloc(1, sizeof(*animation));
    // copy everything from the base anim
    memcpy(animation, base_anim, sizeof(*animation));
    animation->elapsed = 0.0;
    animation->active = true;
    // then duplicate the data
    animation->custom_data = NULL;
    switch ( animation->type ) {
    case ANIM_EASE_TRANSLATION:
        animation->custom_data = dup_anim_translate_data(base_anim->custom_data);
        break;
    case ANIM_FADE_IN_OUT:
        animation->custom_data = dup_anim_fade_in_out_data(base_anim->custom_data);
        break;
    case ANIM_SCALE:
        animation->custom_data = dup_anim_scale_data(base_anim->custom_data);
        break;
    case ANIM_DRAW_REGION:
        animation->custom_data = dup_anim_draw_region_data(base_anim->custom_data);
        break;
    case ANIM_SCALE_REGION:
        animation->custom_data = dup_anim_scale_region_data(base_anim->custom_data);
        break;
    default:
        error_abort("reapply_animation: Unrecognized animation type");
    }
    // Now depending on the apply type, we either append it right to the root of the active_animations vector, or as a next node
    // to an existing one In the case of overriding, we should already have had an early return above if there's an animation
    // currently playing so we avoid re-allocating the custom data and the struct itself, so it behaves the same as BLOCK (with no
    // existing anim) or CONCURRENT (whatever is the case)
    if ( apply_type == ANIM_APPLY_BLOCK || apply_type == ANIM_APPLY_CONCURRENT || apply_type == ANIM_APPLY_OVERRIDE ) {
        vec_add(drawable->active_animations, animation);
    } else if ( apply_type == ANIM_APPLY_SEQUENTIAL ) {
        // Add the current animation to the linked list of "pending" animations
        if ( existing != NULL ) {
            while ( existing->next != NULL ) {
                existing = existing->next;
            }
            existing->next = animation;
        } else {
            vec_add(drawable->active_animations, animation);
        }
    } else {
        error_abort("reapply_animation: Unrecognized animation type");
    }

    return animation;
}

void ui_reposition_drawable(Ui_t *ui, Drawable_t *drawable) {
    const double old_x = drawable->bounds.x, old_y = drawable->bounds.y;

    measure_layout(&drawable->layout, drawable->parent, &drawable->bounds);
    position_layout(ui, &drawable->layout, drawable->parent, &drawable->bounds);

    if ( old_x != drawable->bounds.x || old_y != drawable->bounds.y ) {
        Animation_t *base_anim = find_animation(drawable, ANIM_EASE_TRANSLATION);
        if ( base_anim != NULL ) {
            Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
            if ( animation != NULL ) {
                reapply_translate_animation(animation, old_x, old_y);
            }
        }
    }
}

void ui_drawable_set_scale_factor(Drawable_t *drawable, const float scale) {
    // Do this so that we can specify scale in a way that makes sense (that is, 1.0 for the default size, anything other as
    // a transformation)
    const float scale_mod = scale - 1.f;
    if ( scale_mod == drawable->bounds.scale_mod )
        return;

    const Animation_t *base_anim = find_animation(drawable, ANIM_SCALE);
    if ( base_anim != NULL ) {
        Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
        if ( animation != NULL ) {
            Animation_ScaleData_t *data = animation->custom_data;
            data->from_scale = drawable->bounds.scale_mod;
            data->to_scale = scale_mod;
        }
    }
    drawable->bounds.scale_mod = scale_mod;
}

void ui_drawable_set_scale_factor_immediate(Drawable_t *drawable, const float scale) {
    const float scale_mod = scale - 1.f;
    if ( scale_mod == drawable->bounds.scale_mod )
        return;

    Animation_t *animation = find_active_animation(drawable, ANIM_SCALE);
    if ( animation != NULL ) {
        animation->elapsed = animation->duration;
        animation->active = false;
    }
    drawable->bounds.scale_mod = scale_mod;
}

void ui_drawable_set_scale_factor_dur(Drawable_t *drawable, float scale, double duration) {
    const float scale_mod = scale - 1.f;
    if ( scale_mod == drawable->bounds.scale_mod )
        return;

    const Animation_t *base_anim = find_animation(drawable, ANIM_SCALE);
    if ( base_anim != NULL ) {
        Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
        if ( animation != NULL ) {
            Animation_ScaleData_t *data = animation->custom_data;
            data->from_scale = drawable->bounds.scale_mod;
            data->to_scale = scale_mod;
            data->duration = duration;
        }
    }
    drawable->bounds.scale_mod = scale_mod;
}

void ui_drawable_set_color_mod(Drawable_t *drawable, const float color_mod) { drawable->color_mod = color_mod; }

void ui_drawable_set_draw_region(Drawable_t *drawable, const DrawRegionOptSet_t *draw_regions) {
    double duration = 0.0;
    const Animation_t *base_anim = find_animation(drawable, ANIM_DRAW_REGION);
    if ( base_anim != NULL ) {
        Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
        if ( animation != NULL ) {
            const Animation_DrawRegionData_t *data = animation->custom_data;
            duration = data->duration;
        }
    }

    ui_drawable_set_draw_region_dur(drawable, draw_regions, duration);
}

void ui_drawable_set_draw_region_immediate(Drawable_t *drawable, const DrawRegionOptSet_t *draw_regions) {
    Animation_t *animation = find_active_animation(drawable, ANIM_DRAW_REGION);
    if ( animation != NULL ) {
        // Cancel animation if it's active
        animation->active = false;
        animation->elapsed = animation->duration;
    }

    for ( int i = 0; i < draw_regions->num_regions; i++ ) {
        drawable->draw_regions.regions[i].x0_perc = draw_regions->regions[i].x0_perc;
        drawable->draw_regions.regions[i].x1_perc = draw_regions->regions[i].x1_perc;
        drawable->draw_regions.regions[i].y0_perc = draw_regions->regions[i].y0_perc;
        drawable->draw_regions.regions[i].y1_perc = draw_regions->regions[i].y1_perc;
    }
    drawable->draw_regions.num_regions = draw_regions->num_regions;
}

void ui_drawable_set_draw_region_dur(Drawable_t *drawable, const DrawRegionOptSet_t *draw_regions, const double duration) {
    Animation_t *base_anim = find_animation(drawable, ANIM_DRAW_REGION);
    if ( base_anim != NULL ) {
        // If it's the same, do nothing
        // TODO: Currently we only check for the x1 values
        bool different = false;
        for ( int i = 0; i < draw_regions->num_regions; i++ ) {
            if ( drawable->draw_regions.regions[i].x1_perc != draw_regions->regions[i].x1_perc ) {
                different = true;
                break;
            }
        }
        if ( !different )
            return;

        Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
        if ( animation != NULL ) {
            Animation_DrawRegionData_t *data = animation->custom_data;
            // If it's finished or not active yet, and it's different, copy the drawable's previous values to the animation
            for ( int i = 0; i < draw_regions->num_regions; i++ ) {
                data->draw_regions.regions[i] = drawable->draw_regions.regions[i];
            }
            data->draw_regions.num_regions = draw_regions->num_regions;
            animation->duration = duration;

            // TODO: Refactor this piece of code is duplicated
            // Copy the real values to the drawable
            for ( int i = 0; i < draw_regions->num_regions; i++ ) {
                drawable->draw_regions.regions[i] = draw_regions->regions[i];
            }
            drawable->draw_regions.num_regions = draw_regions->num_regions;
        }
    } else {
        // Copy the real values to the drawable
        for ( int i = 0; i < draw_regions->num_regions; i++ ) {
            drawable->draw_regions.regions[i] = draw_regions->regions[i];
        }
        drawable->draw_regions.num_regions = draw_regions->num_regions;
    }
}

void ui_drawable_set_alpha(Drawable_t *drawable, const int32_t alpha) {
    if ( alpha == drawable->alpha_mod ) {
        return;
    }

    const Animation_t *base_anim = find_animation(drawable, ANIM_FADE_IN_OUT);
    if ( base_anim != NULL ) {
        Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
        if ( animation != NULL ) {
            Animation_FadeInOutData_t *data = animation->custom_data;
            data->from_alpha = drawable->alpha_mod;
            data->to_alpha = alpha;
        }
    }
    drawable->alpha_mod = alpha;
}

void ui_drawable_set_alpha_immediate(Drawable_t *drawable, const int32_t alpha) {
    if ( alpha == drawable->alpha_mod ) {
        return;
    }

    Animation_t *fade_animation = find_active_animation(drawable, ANIM_FADE_IN_OUT);
    if ( fade_animation != NULL ) {
        // Cancel animation
        fade_animation->elapsed = fade_animation->duration;
        fade_animation->active = false;
    }
    drawable->alpha_mod = alpha;
}

void ui_drawable_disable_draw_region(Drawable_t *drawable) {
    // Reset to defaults
    for ( int i = 0; i < MAX_DRAW_SUB_REGIONS; i++ ) {
        drawable->draw_regions.regions[i].x0_perc = 0.f;
        drawable->draw_regions.regions[i].x1_perc = 0.f;
        drawable->draw_regions.regions[i].y0_perc = 0.f;
        drawable->draw_regions.regions[i].y1_perc = 0.f;
    }
    drawable->draw_regions.num_regions = 0;
}

void ui_drawable_set_draw_underlay(Drawable_t *drawable, const bool draw, const uint8_t alpha) {
    drawable->draw_underlay = draw;
    drawable->underlay_alpha = alpha;
}

void ui_drawable_add_scale_region_dur(Drawable_t *drawable, const ScaleRegionOpt_t *region, const double duration,
                                      AnimationApplyType_t apply_type) {
    const Animation_t *base_anim = find_animation(drawable, ANIM_SCALE_REGION);
    // TODO: This function doesn't do anything if there's no animation attached
    if ( base_anim != NULL ) {
        if ( apply_type == ANIM_APPLY_DEFAULT )
            apply_type = base_anim->apply_type;
        Animation_t *animation = reapply_animation(drawable, base_anim, apply_type);
        if ( animation != NULL ) {
            Animation_ScaleRegionData_t *data = animation->custom_data;
            data->scale_region = *region;
            animation->duration = duration;
        }
    }
}

bool ui_mouse_hovering_drawable(const Drawable_t *drawable, const int padding, Bounds_t *out_canon_bounds, int32_t *out_mouse_x,
                                int32_t *out_mouse_y) {
    double canon_x, canon_y;
    ui_get_drawable_canon_pos(drawable, &canon_x, &canon_y);

    int32_t mouse_x, mouse_y;
    events_get_mouse_position(&mouse_x, &mouse_y);

    if ( out_canon_bounds ) {
        out_canon_bounds->x = canon_x;
        out_canon_bounds->y = canon_y;
        out_canon_bounds->w = drawable->bounds.w;
        out_canon_bounds->h = drawable->bounds.h;
    }

    if ( out_mouse_x )
        *out_mouse_x = mouse_x;
    if ( out_mouse_y )
        *out_mouse_y = mouse_y;

    return mouse_x >= canon_x - padding && mouse_x <= canon_x + drawable->bounds.w + padding && mouse_y >= canon_y - padding &&
           mouse_y <= canon_y + drawable->bounds.h + padding;
}

bool ui_mouse_clicked_drawable(const Drawable_t *drawable, const int padding, Bounds_t *out_canon_bounds, int32_t *out_mouse_x,
                               int32_t *out_mouse_y) {
    if ( ui_mouse_hovering_drawable(drawable, padding, out_canon_bounds, out_mouse_x, out_mouse_y) ) {
        return events_get_mouse_click(NULL, NULL);
    }
    return false;
}

void ui_animate_translation(Drawable_t *target, const Animation_EaseTranslationData_t *data) {
    if ( target == NULL ) {
        error_abort("Target drawable is NULL");
    }

    Animation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_EASE_TRANSLATION;
    result->custom_data = dup_anim_translate_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;
    result->ease_func = data->ease_func;
    result->apply_type = ANIM_APPLY_OVERRIDE;

    vec_add(target->animations, result);
}

void ui_animate_fade(Drawable_t *target, const Animation_FadeInOutData_t *data) {
    if ( target == NULL ) {
        error_abort("Target drawable is NULL");
    }

    Animation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_FADE_IN_OUT;
    result->custom_data = dup_anim_fade_in_out_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;
    result->ease_func = data->ease_func;
    result->apply_type = ANIM_APPLY_OVERRIDE;

    vec_add(target->animations, result);
}

void ui_animate_scale(Drawable_t *target, const Animation_ScaleData_t *data) {
    if ( target == NULL ) {
        error_abort("Target drawable is NULL");
    }

    Animation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_SCALE;
    result->custom_data = dup_anim_scale_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;
    result->ease_func = ANIM_EASE_NONE;

    vec_add(target->animations, result);
}

void ui_animate_draw_region(Drawable_t *target, const Animation_DrawRegionData_t *data) {
    if ( target == NULL ) {
        error_abort("Target drawable is NULL");
    }

    Animation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_DRAW_REGION;
    result->custom_data = dup_anim_draw_region_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;
    result->ease_func = data->ease_func;
    result->apply_type = ANIM_APPLY_BLOCK;

    vec_add(target->animations, result);
}

void ui_animate_scale_region(Drawable_t *target, const Animation_ScaleRegionData_t *data) {
    if ( target == NULL ) {
        error_abort("Target drawable is null");
    }

    Animation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_SCALE_REGION;
    result->custom_data = dup_anim_scale_region_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;
    result->ease_func = data->ease_func;
    result->apply_type = data->default_apply;

    vec_add(target->animations, result);
}
