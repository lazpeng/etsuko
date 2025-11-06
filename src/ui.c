#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "events.h"
#include "str_utils.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "constants.h"
#include "container_utils.h"

struct etsuko_UiState_t {
    etsuko_Container_t root_container;
};

etsuko_UiState_t *ui_init() {
    etsuko_UiState_t *ui = calloc(1, sizeof(*ui));
    if ( ui == nullptr ) {
        error_abort("Failed to allocate UI");
    }

    ui->root_container.child_containers = vec_init();
    ui->root_container.child_drawables = vec_init();
    ui->root_container.enabled = true;

    ui_on_window_changed(ui);

    return ui;
}

void ui_load_font(const etsuko_FontType_t type, const char *path) { render_load_font(path, type); }

static void animation_translation_data_destroy(etsuko_Animation_EaseTranslationData_t *data) { free(data); }
static void animation_fade_data_destroy(etsuko_Animation_FadeInOutData_t *data) { free(data); }

static void animation_destroy(etsuko_Animation_t *animation) {
    if ( animation->custom_data != nullptr ) {
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

static void update_animations(const etsuko_UiState_t *ui, const double delta_time) {
    container_update_animations(&ui->root_container, delta_time);
}

void ui_begin_loop(etsuko_UiState_t *ui) {
    if ( events_window_changed() )
        ui_on_window_changed(ui);

    const double delta_time = events_get_delta_time();
    render_clear();
    update_animations(ui, delta_time);
}

static void draw_dynamic_progressbar(const etsuko_Drawable_t *drawable, const etsuko_Bounds_t *base_bounds) {
    const etsuko_Drawable_ProgressBarData_t *data = drawable->custom_data;

    etsuko_Bounds_t bounds = drawable->bounds;
    bounds.x += base_bounds->x;
    bounds.y += base_bounds->y;

    const float border_radius = (float)render_measure_pt_from_em(data->border_radius_em);
    render_draw_rounded_rect(&bounds, &data->bg_color, border_radius);
    bounds.w *= data->progress;
    render_draw_rounded_rect(&bounds, &data->fg_color, border_radius);
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

    if ( layout->relative_to_size != nullptr ) {
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

static void measure_container_size(etsuko_UiState_t *ui, const etsuko_Container_t *container, etsuko_Bounds_t *out_bounds) {
    // Only measure height for now
    double max_y = 0, min_y = 0;
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        const etsuko_Drawable_t *drawable = container->child_drawables->data[i];
        double draw_y;
        ui_get_drawable_canon_pos(ui, drawable, nullptr, &draw_y);

        max_y = fmax(max_y, draw_y + drawable->bounds.h);
        min_y = fmin(min_y, draw_y);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        const etsuko_Container_t *child = container->child_containers->data[i];
        etsuko_Bounds_t child_bounds = {0};
        measure_container_size(ui, child, &child_bounds);
        max_y = fmax(max_y, child_bounds.y + child_bounds.h);
        min_y = fmin(min_y, child_bounds.y);
    }

    out_bounds->h = fmax(out_bounds->h, max_y - min_y);
}

static void recalculate_container_alignment(etsuko_UiState_t *ui, etsuko_Container_t *container) {
    if ( container->parent != nullptr )
        recalculate_container_alignment(ui, container->parent);

    if ( container->flags & CONTAINER_VERTICAL_ALIGN_CONTENT ) {
        container->align_content_offset_y = 0;
        etsuko_Bounds_t bounds = {0};
        measure_container_size(ui, container, &bounds);
        container->align_content_offset_y = (container->bounds.h - bounds.h) / 2.f;
    }
}

static void position_layout(etsuko_UiState_t *ui, const etsuko_Layout_t *layout, etsuko_Container_t *parent,
                            etsuko_Bounds_t *out_bounds) {
    double x = layout->offset_x;
    double calc_w = 0;
    if ( layout->flags & LAYOUT_ANCHOR_RIGHT_X ) {
        calc_w = out_bounds->w;
    }
    if ( layout->flags & LAYOUT_CENTER_X ) {
        x = parent->bounds.w / 2.f - out_bounds->w / 2.f - calc_w;
    } else if ( layout->flags & LAYOUT_PROPORTIONAL_X ) {
        x = parent->bounds.w * x;
    } else {
        if ( x < 0 && layout->flags & LAYOUT_WRAP_AROUND_X )
            x = parent->bounds.w + x;
    }
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
    } else {
        if ( y < 0 && layout->flags & LAYOUT_WRAP_AROUND_Y )
            y = parent->bounds.h + y;
    }
    y -= calc_h;

    if ( layout->relative_to != nullptr ) {
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

    recalculate_container_alignment(ui, parent);
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

static void perform_draw(const etsuko_Drawable_t *drawable, const etsuko_Bounds_t *base_bounds) {
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

    etsuko_Bounds_t rect = delta.final_bounds;
    rect.x += base_bounds->x;
    rect.y += base_bounds->y;

    render_draw_texture(drawable->texture, &rect, delta.final_alpha);
}

static void draw_all_container(const etsuko_Container_t *container, etsuko_Bounds_t base_bounds) {
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

void ui_draw(const etsuko_UiState_t *ui) {
    const etsuko_Bounds_t bounds = {0};
    draw_all_container(&ui->root_container, bounds);
}

void ui_end_loop() { render_present(); }

void ui_set_window_title(const char *title) { render_set_window_title(title); }

void ui_finish(etsuko_UiState_t *ui) {
    // Free stored textures and drawables
    ui_destroy_container(ui, &ui->root_container);
    // Cleanup
    free(ui);
}

void ui_set_bg_color(const uint32_t color) { render_set_bg_color(render_color_parse(color)); }

void ui_set_bg_gradient(const uint32_t primary, const uint32_t secondary) {
    const auto primary_color = render_color_parse(primary);
    const auto secondary_color = secondary == 0 ? render_color_darken(primary_color) : render_color_parse(secondary);
    render_set_bg_gradient(primary_color, secondary_color);
}

etsuko_Container_t *ui_root_container(etsuko_UiState_t *ui) { return &ui->root_container; }

void ui_get_drawable_canon_pos(etsuko_UiState_t *ui, const etsuko_Drawable_t *drawable, double *x, double *y) {
    double parent_x = 0, parent_y = 0;
    ui_get_container_canon_pos(ui, drawable->parent, &parent_x, &parent_y);

    if ( x != nullptr )
        *x = parent_x + drawable->bounds.x;
    if ( y != nullptr )
        *y = parent_y + drawable->bounds.y;
}

void ui_get_container_canon_pos(etsuko_UiState_t *ui, const etsuko_Container_t *container, double *x, double *y) {
    double parent_x = 0, parent_y = 0;
    const etsuko_Container_t *parent = container;
    while ( parent != nullptr ) {
        parent_x += parent->bounds.x;
        parent_y += parent->bounds.y + parent->align_content_offset_y;
        parent = parent->parent;
    }

    if ( x != nullptr )
        *x = parent_x;
    if ( y != nullptr )
        *y = parent_y;
}

static etsuko_Drawable_TextData_t *dup_text_data(const etsuko_Drawable_TextData_t *data) {
    etsuko_Drawable_TextData_t *result = calloc(1, sizeof(*result));
    if ( result == nullptr ) {
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
    if ( result == nullptr ) {
        error_abort("Failed to allocate image data");
    }
    result->file_path = strdup(data->file_path);
    result->border_radius_em = data->border_radius_em;
    return result;
}

static void free_image_data(etsuko_Drawable_ImageData_t *data) {
    free(data->file_path);
    free(data);
}

static etsuko_Drawable_ProgressBarData_t *dup_progressbar_data(const etsuko_Drawable_ProgressBarData_t *data) {
    etsuko_Drawable_ProgressBarData_t *result = calloc(1, sizeof(*result));
    if ( result == nullptr ) {
        error_abort("Failed to allocate image data");
    }
    result->progress = data->progress;
    result->border_radius_em = data->border_radius_em;
    result->fg_color = data->fg_color;
    result->bg_color = data->bg_color;
    return result;
}

static void free_progressbar_data(etsuko_Drawable_ProgressBarData_t *data) { free(data); }

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
            measure_pt_size = render_measure_pt_from_em(data->measure_at_em);
        } else {
            measure_pt_size = render_measure_pt_from_em(data->em);
        }
        int32_t w, h;
        char *dup = strndup(data->text + start, tmp_end_idx - start);
        render_measure_text_size(dup, measure_pt_size, &w, &h, data->font_type);
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

static etsuko_Drawable_t *make_drawable(etsuko_Container_t *parent, const etsuko_DrawableType_t type, const bool dynamic) {
    etsuko_Drawable_t *result = calloc(1, sizeof(*result));
    if ( result == nullptr ) {
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

static etsuko_Drawable_t *internal_make_text(etsuko_UiState_t *ui, etsuko_Drawable_t *result, etsuko_Drawable_TextData_t *data,
                                             const etsuko_Container_t *container, const etsuko_Layout_t *layout) {
    etsuko_Texture_t *final_texture;

    data = dup_text_data(data);
    const size_t text_size = strnlen(data->text, MAX_TEXT_SIZE);
    if ( data->wrap_enabled && measure_text_wrap_stop(data, container, 0) < (int32_t)text_size ) {
        size_t start = 0;

        Vector_t *textures_vec = vec_init();
        int32_t max_w = 0, total_h = 0;

        do {
            const size_t end = measure_text_wrap_stop(data, container, (int32_t)start);
            char *line_str = strndup(data->text + start, end - start);
            const int pt_size = render_measure_pt_from_em(data->em);
            etsuko_Texture_t *texture = render_make_text(line_str, pt_size, data->bold, &data->color, data->font_type);
            free(line_str);

            vec_add(textures_vec, texture);

            max_w = MAX(max_w, texture->width);
            if ( total_h != 0 ) {
                total_h += data->line_padding;
            }
            total_h += texture->height;

            start = end + 1;
        } while ( start < text_size - 1 );

        const etsuko_RenderTarget_t *target = render_make_texture_target(max_w, total_h);
        final_texture = target->texture;

        double x, y = total_h;
        for ( size_t i = 0; i < textures_vec->size; i++ ) {
            etsuko_Texture_t *texture = textures_vec->data[i];
            // int32_t w, h;
            // render_measure_texture(texture, &w, &h);
            if ( data->alignment == ALIGN_LEFT ) {
                x = 0;
            } else if ( data->alignment == ALIGN_RIGHT ) {
                x = max_w - texture->width;
            } else if ( data->alignment == ALIGN_CENTER ) {
                x = max_w / 2.0 - texture->width / 2.0;
            } else {
                error_abort("Invalid alignment mode");
            }

            y -= texture->height - data->line_padding;
            etsuko_Bounds_t destination = {.x = x, .y = y, .w = (float)texture->width, .h = (float)texture->height};
            // Disable blend on the texture so it doesn't lose alpha from blending multiple times
            // when rendering onto a target texture
            const etsuko_BlendMode_t blend_mode = render_get_blend_mode();
            render_set_blend_mode(BLEND_MODE_NONE);
            render_draw_texture(texture, &destination, 0xFF);
            render_set_blend_mode(blend_mode);

            render_destroy_texture(texture);
        }

        vec_destroy(textures_vec);
        render_restore_texture_target();
    } else {
        const int32_t pt_size = render_measure_pt_from_em(data->em);
        final_texture = render_make_text(data->text, pt_size, data->bold, &data->color, data->font_type);
    }

    if ( result == nullptr ) {
        error_abort("Failed to allocate drawable");
    }

    const double width = final_texture->width, height = final_texture->height;
    result->bounds = (etsuko_Bounds_t){.x = result->bounds.x, .y = result->bounds.y, .w = width, .h = height};
    result->custom_data = data;
    result->texture = final_texture;
    result->layout = *layout;
    ui_reposition_drawable(ui, result);

    return result;
}

etsuko_Drawable_t *ui_make_text(etsuko_UiState_t *ui, etsuko_Drawable_TextData_t *data, etsuko_Container_t *container,
                                const etsuko_Layout_t *layout) {
    etsuko_Drawable_t *result = make_drawable(container, DRAW_TYPE_TEXT, false);
    internal_make_text(ui, result, data, container, layout);
    vec_add(container->child_drawables, result);
    return result;
}

static void internal_make_image(etsuko_UiState_t *ui, etsuko_Drawable_t *result, etsuko_Drawable_ImageData_t *data,
                                const etsuko_Layout_t *layout) {
    data = dup_image_data(data);

    etsuko_Texture_t *texture = render_make_image(data->file_path, data->border_radius_em);
    result->bounds.w = texture->width;
    result->bounds.h = texture->height;

    result->custom_data = data;
    result->texture = texture;
    result->layout = *layout;

    ui_reposition_drawable(ui, result);
}

etsuko_Drawable_t *ui_make_image(etsuko_UiState_t *ui, etsuko_Drawable_ImageData_t *data, etsuko_Container_t *container,
                                 const etsuko_Layout_t *layout) {
    etsuko_Drawable_t *result = make_drawable(container, DRAW_TYPE_IMAGE, false);
    internal_make_image(ui, result, data, layout);
    vec_add(container->child_drawables, result);
    return result;
}

etsuko_Drawable_t *ui_make_progressbar(etsuko_UiState_t *ui, const etsuko_Drawable_ProgressBarData_t *data,
                                       etsuko_Container_t *container, const etsuko_Layout_t *layout) {
    etsuko_Drawable_t *result = make_drawable(container, DRAW_TYPE_PROGRESS_BAR, true);

    result->custom_data = dup_progressbar_data(data);
    result->layout = *layout;

    ui_reposition_drawable(ui, result);
    vec_add(container->child_drawables, result);
    return result;
}

void ui_destroy_drawable(etsuko_Drawable_t *drawable) {
    if ( drawable->texture != nullptr ) {
        render_destroy_texture(drawable->texture);
    }
    if ( drawable->custom_data != nullptr ) {
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

etsuko_Container_t *ui_make_container(etsuko_UiState_t *ui, etsuko_Container_t *parent, const etsuko_Layout_t *layout,
                                      const etsuko_ContainerFlags_t flags) {
    etsuko_Container_t *result = calloc(1, sizeof(*result));
    if ( result == nullptr ) {
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

void ui_destroy_container(etsuko_UiState_t *ui, etsuko_Container_t *container) {
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

static void reapply_translate_animation(etsuko_Animation_t *animation, const double old_x, const double old_y) {
    etsuko_Animation_EaseTranslationData_t *data = animation->custom_data;
    data->from_x = old_x;
    data->from_y = old_y;
    data->to_x = animation->target->bounds.x;
    data->to_y = animation->target->bounds.y;

    animation->elapsed = 0.0;
    animation->active = true;
}

void ui_reposition_drawable(etsuko_UiState_t *ui, etsuko_Drawable_t *drawable) {
    const double old_x = drawable->bounds.x, old_y = drawable->bounds.y;

    measure_layout(&drawable->layout, drawable->parent, &drawable->bounds);
    position_layout(ui, &drawable->layout, drawable->parent, &drawable->bounds);

    if ( old_x != drawable->bounds.x || old_y != drawable->bounds.y ) {
        for ( size_t i = 0; i < drawable->animations->size; i++ ) {
            etsuko_Animation_t *animation = drawable->animations->data[i];
            if ( animation->target == drawable && animation->type == ANIM_EASE_TRANSLATION ) {
                reapply_translate_animation(animation, old_x, old_y);
            }
        }
    }
}

void ui_drawable_set_alpha(etsuko_Drawable_t *drawable, const int32_t alpha) {
    if ( alpha == drawable->alpha_mod ) {
        return;
    }

    etsuko_Animation_t *fade_animation = nullptr;
    for ( size_t i = 0; i < drawable->animations->size; i++ ) {
        etsuko_Animation_t *animation = drawable->animations->data[i];
        if ( animation->type == ANIM_FADE_IN_OUT ) {
            fade_animation = animation;
            break;
        }
    }

    if ( fade_animation != nullptr ) {
        etsuko_Animation_FadeInOutData_t *data = fade_animation->custom_data;

        fade_animation->elapsed = 0.0;
        fade_animation->active = true;

        data->from_alpha = drawable->alpha_mod;
        data->to_alpha = alpha;
    }
    drawable->alpha_mod = alpha;
}

void ui_recompute_drawable(etsuko_UiState_t *ui, etsuko_Drawable_t *drawable) {
    const etsuko_Container_t *container = drawable->parent;
    if ( drawable->type == DRAW_TYPE_TEXT ) {
        void *old_custom_data = drawable->custom_data;
        internal_make_text(ui, drawable, old_custom_data, container, &drawable->layout);
        free_text_data(old_custom_data);
    } else if ( drawable->type == DRAW_TYPE_IMAGE ) {
        void *old_custom_data = drawable->custom_data;
        internal_make_image(ui, drawable, old_custom_data, &drawable->layout);
        free_image_data(old_custom_data);
    } else if ( drawable->type == DRAW_TYPE_PROGRESS_BAR ) {
        // There's nothing to do in this case
        ui_reposition_drawable(ui, drawable);
    } else {
        error_abort("Invalid drawable type");
    }
}

void ui_recompute_container(etsuko_UiState_t *ui, etsuko_Container_t *container) {
    if ( container->parent != nullptr ) {
        measure_layout(&container->layout, container->parent, &container->bounds);
        position_layout(ui, &container->layout, container->parent, &container->bounds);
    }

    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        ui_recompute_drawable(ui, container->child_drawables->data[i]);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        if ( container->child_containers->data[i] != nullptr ) {
            ui_recompute_container(ui, container->child_containers->data[i]);
        }
    }
}

void ui_on_window_changed(etsuko_UiState_t *ui) {
    render_on_window_changed();
    ui->root_container.bounds = *render_get_viewport();
    ui_recompute_container(ui, &ui->root_container);
}

static etsuko_Animation_EaseTranslationData_t *dup_anim_translate_data(const etsuko_Animation_EaseTranslationData_t *data) {
    etsuko_Animation_EaseTranslationData_t *result = calloc(1, sizeof(*result));
    if ( result == nullptr ) {
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
    if ( result == nullptr ) {
        error_abort("Failed to allocate animation fade in out data");
    }
    result->from_alpha = data->from_alpha;
    result->to_alpha = data->to_alpha;
    result->duration = data->duration;

    return result;
}

void ui_animate_translation(etsuko_Drawable_t *target, const etsuko_Animation_EaseTranslationData_t *data) {
    if ( target == nullptr ) {
        error_abort("Target drawable is nullptr");
    }

    etsuko_Animation_t *result = calloc(1, sizeof(*result));
    if ( result == nullptr ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_EASE_TRANSLATION;
    result->custom_data = dup_anim_translate_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;

    vec_add(target->animations, result);
}

void ui_animate_fade(etsuko_Drawable_t *target, const etsuko_Animation_FadeInOutData_t *data) {
    if ( target == nullptr ) {
        error_abort("Target drawable is nullptr");
    }

    etsuko_Animation_t *result = calloc(1, sizeof(*result));
    if ( result == nullptr ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_FADE_IN_OUT;
    result->custom_data = dup_anim_fade_in_out_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;

    vec_add(target->animations, result);
}
