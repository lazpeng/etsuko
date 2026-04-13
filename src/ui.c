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

typedef struct ZLayer_t {
    int index;
    Vector_t *nodes; // of Drawable_t*
    struct ZLayer_t *next, *prev;
} ZLayer_t;

typedef struct EventDef_t {
    UiEvent_t type;
    WEAK c_ui_event_callback callback;
    WEAK Drawable_t *target;
    WEAK void *custom_data;
} EventDef_t;

struct Ui_t {
    OWNING Container_t *root_container;
    OWNING Texture_t *null_texture;
    OWNING ZLayer_t *z_layers_head;
    OWNING Vector_t *global_events;
    OWNING Vector_t *opaque_containers; // containers with z_index > 0 that block events below them
    WEAK Drawable_t *current_hovered_drawable;
    WEAK Drawable_t *current_dragged_drawable;
    int32_t current_drag_grab_offset_x, current_drag_grab_offset_y;
};

static ZLayer_t *z_layer_init(const int index) {
    ZLayer_t *z_layer = calloc(1, sizeof(*z_layer));
    z_layer->index = index;
    z_layer->next = NULL;
    z_layer->prev = NULL;
    z_layer->nodes = vec_init();
    return z_layer;
}

static ZLayer_t *append_z_layer(ZLayer_t **head, const int index) {
    if ( *head == NULL )
        return *head = z_layer_init(index);

    if ( (*head)->index < index ) {
        // new head
        ZLayer_t *new_head = z_layer_init(index);
        (*head)->next = new_head;
        new_head->prev = *head;
        *head = new_head;
        return new_head;
    }

    ZLayer_t *cur = *head;
    while ( true ) {
        if ( cur->index == index )
            return cur;
        if ( cur->prev != NULL && cur->prev->index < index ) {
            // between these two
            ZLayer_t *new_between = z_layer_init(index);
            ZLayer_t *old_prev = cur->prev;
            old_prev->next = new_between;
            cur->prev = new_between;
            new_between->prev = old_prev;
            new_between->next = cur;
            return new_between;
        }
        if ( cur->prev == NULL ) {
            ZLayer_t *new_tail = z_layer_init(index);
            cur->prev = new_tail;
            return new_tail;
        }

        cur = cur->prev;
    }
}

Ui_t *ui_init(void) {
    Ui_t *ui = calloc(1, sizeof(*ui));
    if ( ui == NULL ) {
        error_abort("Failed to allocate UI");
    }

    const Layout_t layout = {.width = 0, .height = 0};
    ui->root_container = ui_make_container(ui, NULL, &layout, CONTAINER_NONE);
    ui->root_container->bounds = *render_get_viewport();

    ui->null_texture = render_make_null();
    ui->z_layers_head = append_z_layer(&ui->z_layers_head, 0);
    ui->global_events = vec_init();
    ui->opaque_containers = vec_init();

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

static void animation_destroy(Animation_t *animation, const bool recursive) {
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
        } else if ( animation->type == ANIM_BACKGROUND_COLOR || animation->type == ANIM_BLUR_RADIUS ) {
            free(animation->custom_data);
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

            if ( anim->elapsed < anim->duration + anim->delay ) {
                anim->elapsed += delta_time;
            }
        }
    }

    for ( size_t i = 0; i < container->animations->size; i++ ) {
        ContainerAnimation_t *anim = container->animations->data[i];
        if ( anim->elapsed < anim->duration + anim->delay ) {
            anim->elapsed += delta_time;
        }
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        const Container_t *child = container->child_containers->data[i];
        container_update_animations(child, delta_time);
    }
}

static void update_animations(const Ui_t *ui, const double delta_time) {
    container_update_animations(ui->root_container, delta_time);
}

static void handle_global_mouse_input(Ui_t *ui) {
    int32_t mouse_x, mouse_y;
    events_get_mouse_position(&mouse_x, &mouse_y);
    for ( size_t i = 0; i < ui->global_events->size; i++ ) {
        const EventDef_t *def = ui->global_events->data[i];

        if ( def->type == UI_EVENT_MOUSE_MOVE && events_mouse_moved() ) {
            const UiEventOpts_t opts = {
                .ui = ui, .event = def->type, .mouse = {.x = mouse_x, .y = mouse_y, .clicked = false, .duration = 0}};
            def->callback(&opts, NULL, def->custom_data);
        } else if ( def->type == UI_EVENT_MOUSE_STOPPED && !events_mouse_moved() ) {
            const UiEventOpts_t opts = {
                .ui = ui,
                .event = def->type,
                .mouse = {.x = mouse_x, .y = mouse_y, .clicked = false, .duration = events_time_since_mouse_stopped()}};
            def->callback(&opts, NULL, def->custom_data);
        }
    }
}

static void handle_global_keyboard_input(Ui_t *ui) {
    if ( !events_any_key_was_pressed() )
        return;
    // fire one event for every key that was pressed
    // but usually it'll be just one or two at most
    for ( size_t i = 0; i < ui->global_events->size; i++ ) {
        const EventDef_t *def = ui->global_events->data[i];
        if ( def->type == UI_EVENT_KEY_PRESSED ) {
            for ( Key_t key = 0; key < KEY_INVALID; key++ ) {
                if ( events_key_was_pressed(key) ) {
                    const UiEventOpts_t opts = {.ui = ui, .event = def->type, .keyboard = {.key = key}};
                    def->callback(&opts, NULL, def->custom_data);
                }
            }
        }
    }
}

static void handle_mouse_input(Ui_t *ui) {
    Drawable_t *hovered_drawable = NULL;
    int32_t mouse_x, mouse_y;
    events_get_mouse_position(&mouse_x, &mouse_y);
    const bool clicked = events_get_mouse_click(NULL, NULL);

    const bool button_down = events_mouse_button_down();
    int32_t drag_dx = 0, drag_dy = 0;
    if ( button_down && events_mouse_moved() )
        events_get_mouse_drag_delta(&drag_dx, &drag_dy);

    if ( ui->current_dragged_drawable != NULL && button_down && events_mouse_moved() ) {
        for ( size_t e = 0; e < ui->current_dragged_drawable->events->size; e++ ) {
            const EventDef_t *def = ui->current_dragged_drawable->events->data[e];
            if ( def->type == UI_EVENT_MOUSE_DRAG ) {
                const UiEventOpts_t opts = {.ui = ui,
                                            .event = UI_EVENT_MOUSE_DRAG,
                                            .mouse = {
                                                .x = mouse_x,
                                                .y = mouse_y,
                                                .dx = drag_dx,
                                                .dy = drag_dy,
                                                .grab_offset_x = ui->current_drag_grab_offset_x,
                                                .grab_offset_y = ui->current_drag_grab_offset_y,
                                            }};
                def->callback(&opts, ui->current_dragged_drawable, def->custom_data);
            }
        }
    }
    // TODO: Maybe a leave dragging event dispatch?
    if ( !button_down )
        ui->current_dragged_drawable = NULL;

    const ZLayer_t *cur = ui->z_layers_head;
    while ( cur != NULL ) {
        for ( size_t i = 0; i < cur->nodes->size; i++ ) {
            Drawable_t *drawable = cur->nodes->data[i];
            double d_pos_x, d_pos_y;
            ui_get_drawable_canon_pos(drawable, &d_pos_x, &d_pos_y);
            if ( !drawable->enabled ) {
                continue;
            }

            const bool not_visible = d_pos_x + drawable->bounds.w < 0 || d_pos_y + drawable->bounds.h < 0 ||
                                     d_pos_x > ui->root_container->bounds.w || d_pos_y > ui->root_container->bounds.h;
            const bool outside = mouse_x < d_pos_x || mouse_x > d_pos_x + drawable->bounds.w || mouse_y < d_pos_y ||
                                 mouse_y > d_pos_y + drawable->bounds.h;
            if ( outside || not_visible )
                continue;

            for ( size_t e = 0; e < drawable->events->size; e++ ) {
                const EventDef_t *def = drawable->events->data[e];
                if ( clicked && def->type == UI_EVENT_MOUSE_CLICK ) {
                    const UiEventOpts_t opts = {
                        .ui = ui, .event = def->type, .mouse = {.x = mouse_x, .y = mouse_y, .clicked = clicked}};
                    def->callback(&opts, drawable, def->custom_data);
                }
            }

            // On click, capture drawable for drag if it has a drag listener
            if ( clicked && ui->current_dragged_drawable == NULL ) {
                for ( size_t e = 0; e < drawable->events->size; e++ ) {
                    if ( ((EventDef_t *)drawable->events->data[e])->type == UI_EVENT_MOUSE_DRAG ) {
                        ui->current_dragged_drawable = drawable;

                        ui->current_drag_grab_offset_x = mouse_x - (int32_t)d_pos_x;
                        ui->current_drag_grab_offset_y = mouse_y - (int32_t)d_pos_y;
                        break;
                    }
                }
            }

            hovered_drawable = drawable;
            break;
        }

        if ( hovered_drawable != NULL )
            break;

        // Before descending to a lower z-layer, check if any opaque container
        // at a higher z-index covers the mouse, and if so, stop event propagation
        if ( cur->prev != NULL ) {
            const int next_index = cur->prev->index;
            for ( size_t oi = 0; oi < ui->opaque_containers->size; oi++ ) {
                const Container_t *oc = ui->opaque_containers->data[oi];
                if ( oc->z_layer_index <= next_index )
                    continue;
                double cx, cy;
                ui_get_container_canon_pos(oc, &cx, &cy, false);
                if ( mouse_x >= cx && mouse_x <= cx + oc->bounds.w && mouse_y >= cy && mouse_y <= cy + oc->bounds.h ) {
                    goto done_hit_test;
                }
            }
        }

        cur = cur->prev;
    }
done_hit_test:

    if ( ui->current_hovered_drawable != hovered_drawable ) {
        if ( ui->current_hovered_drawable != NULL ) {
            // Hover exit
            for ( size_t i = 0; i < ui->current_hovered_drawable->events->size; i++ ) {
                const EventDef_t *def = ui->current_hovered_drawable->events->data[i];
                if ( def->type == UI_EVENT_MOUSE_HOVER_EXITED ) {
                    const UiEventOpts_t opts = {.ui = ui,
                                                .event = def->type,
                                                .mouse = {
                                                    .x = mouse_x,
                                                    .y = mouse_y,
                                                    .clicked = clicked,
                                                    .duration = 0,
                                                    .scroll = 0,
                                                }};
                    def->callback(&opts, ui->current_hovered_drawable, def->custom_data);
                }
            }
        }
        ui->current_hovered_drawable = hovered_drawable;
        // Hover enter (always after hover exit so callbacks see a consistent transition)
        if ( hovered_drawable != NULL ) {
            for ( size_t e = 0; e < hovered_drawable->events->size; e++ ) {
                const EventDef_t *def = hovered_drawable->events->data[e];
                if ( def->type == UI_EVENT_MOUSE_HOVER_ENTERED ) {
                    const UiEventOpts_t opts = {
                        .ui = ui, .event = def->type, .mouse = {.x = mouse_x, .y = mouse_y, .clicked = clicked}};
                    def->callback(&opts, hovered_drawable, def->custom_data);
                }
            }
        }
    }
}

static Container_t *find_deepest_scrollable_container(Container_t *parent, Bounds_t bounds) {
    bounds.x += parent->bounds.x;
    bounds.y += parent->bounds.y;

    int32_t mouse_x, mouse_y;
    events_get_mouse_position(&mouse_x, &mouse_y);

    const bool within_x = mouse_x >= bounds.x && mouse_x <= bounds.x + parent->bounds.w;
    const bool within_y = mouse_y >= bounds.y && mouse_y <= bounds.y + parent->bounds.h;

    // Let's consider no other child container could be outside of the parent container's bounds
    if ( !within_x || !within_y )
        return NULL;

    for ( size_t i = 0; i < parent->child_containers->size; i++ ) {
        Container_t *child = parent->child_containers->data[i];
        Container_t *result = find_deepest_scrollable_container(child, bounds);
        if ( result )
            return result;
    }

    // If there's no child container that fits before us, return the current one if it does have scrolling enabled
    if ( parent->overflow_y.kind == OVERFLOW_SCROLL )
        return parent;

    return NULL;
}

static void handle_container_scroll(const Ui_t *ui) {
    const double amount = events_get_mouse_scrolled();
    if ( amount == 0 )
        return;
    // Find the deepest container under the mouse cursor that has overflow enabled
    Container_t *scrollable = find_deepest_scrollable_container(ui->root_container, (Bounds_t){0});
    if ( scrollable == NULL )
        return;

    ui_container_scroll_y_by(scrollable, amount);

    int32_t mouse_x, mouse_y;
    events_get_mouse_position(&mouse_x, &mouse_y);
    for ( size_t i = 0; i < ui->global_events->size; i++ ) {
        const EventDef_t *def = ui->global_events->data[i];
        if ( def->type == UI_EVENT_MOUSE_SCROLL ) {
            const UiEventOpts_t opts = {.event = def->type,
                                        .mouse = {.x = mouse_x,
                                                  .y = mouse_y,
                                                  .scroll = amount,
                                                  .scroll_info = {
                                                      .container = scrollable,
                                                  }}};
            def->callback(&opts, NULL, def->custom_data);
        }
    }
}

static void handle_user_input(Ui_t *ui) {
    handle_global_mouse_input(ui);
    handle_global_keyboard_input(ui);
    handle_mouse_input(ui);
    handle_container_scroll(ui);
}

void ui_begin_loop(Ui_t *ui) {
    if ( events_window_changed() )
        ui_on_window_changed(ui);

    handle_user_input(ui);

    render_clear();
    update_animations(ui, events_get_delta_time());
}

static void draw_dynamic_rectangle(const Drawable_t *drawable, const Bounds_t *bounds, int32_t alpha) {
    const Drawable_RectangleData_t *data = drawable->custom_data;

    float border_radius = (float)data->border_radius_em;
    if ( border_radius > 0 )
        border_radius = (float)render_measure_pt_from_em(data->border_radius_em);

    Color_t color = data->color;
    color.a = (uint8_t)alpha;
    render_draw_rounded_rect(drawable->texture, bounds, &color, border_radius);
}

static void measure_constraints(const SizeConstraint_t *constraint, MAYBE_NULL double *width, MAYBE_NULL double *height) {
    double computed_width = 0, computed_height = 0;
    if ( constraint->type & CONSTRAINT_RELATIVE && constraint->relative_to == NULL ) {
        printf("Warning: compute_constraints: RELATIVE flag is set but no relative_to has been assigned\n");
        goto finish;
    }

    if ( constraint->type & CONSTRAINT_ABSOLUTE ) {
        computed_width = constraint->value;
        computed_height = constraint->value;
    } else if ( constraint->type & CONSTRAINT_RELATIVE ) {
        computed_width = constraint->relative_to->w * constraint->value;
        computed_height = constraint->relative_to->h * constraint->value;
    }

finish:
    if ( width != NULL )
        *width = computed_width;
    if ( height != NULL )
        *height = computed_height;
}

static void measure_layout(const Layout_t *layout, const Container_t *parent, Bounds_t *out_bounds) {
    double w = layout->width, h = layout->height, padding_w = layout->padding_w, padding_h = layout->padding_h;
    if ( layout->width > 0 ) {
        if ( layout->flags & LAYOUT_PROPORTIONAL_W ) {
            w = parent->bounds.w * w;
            padding_w = parent->bounds.w * padding_w;
        }
    }

    if ( layout->height > 0 ) {
        if ( layout->flags & LAYOUT_PROPORTIONAL_H ) {
            h = parent->bounds.h * h;
            padding_h = parent->bounds.h * padding_h;
        }
    }

    if ( layout->relative_to_size != NULL ) {
        if ( (layout->flags & LAYOUT_RELATIVE_TO_SIZE) == 0 ) {
            puts("Warning: relative_to_size is set but no flag setting the relationship was passed.");
        }

        if ( layout->flags & LAYOUT_RELATIVE_TO_WIDTH ) {
            w = layout->relative_to_size->bounds.w * layout->width;
        }

        if ( layout->flags & LAYOUT_RELATIVE_TO_HEIGHT ) {
            h = layout->relative_to_size->bounds.h * layout->height;
        }
    }

    w += padding_w;
    h += padding_h;

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

    // TODO: This will currently mess with things that have the keep aspect ratio flag on
    //  come back to this when I can think of a solution to have both at the same time
    if ( layout->min_width.type != CONSTRAINT_NONE ) {
        double constraint_width = 0;
        measure_constraints(&layout->min_width, &constraint_width, NULL);
        if ( constraint_width != 0 )
            w = MAX(w, constraint_width);
    }
    if ( layout->max_width.type != CONSTRAINT_NONE ) {
        double constraint_width = 0;
        measure_constraints(&layout->max_width, &constraint_width, NULL);
        if ( constraint_width != 0 )
            w = MIN(w, constraint_width);
    }
    if ( layout->min_height.type != CONSTRAINT_NONE ) {
        double constraint_height = 0;
        measure_constraints(&layout->min_height, NULL, &constraint_height);
        if ( constraint_height != 0 )
            h = MAX(h, constraint_height);
    }
    if ( layout->max_height.type != CONSTRAINT_NONE ) {
        double constraint_height = 0;
        measure_constraints(&layout->max_height, NULL, &constraint_height);
        if ( constraint_height != 0 )
            h = MIN(h, constraint_height);
    }

    // width or height being zero means automatic so dependent on the actual size of the thing, e.g. text and images
    // this is of course a major oversight and it will be wrong if you forget to set these properly so... don't :)
    if ( layout->width != 0 || maintain_aspect_ratio )
        out_bounds->w = w;

    if ( layout->height != 0 || maintain_aspect_ratio )
        out_bounds->h = h;
}

static void measure_container_size(const Container_t *container, Bounds_t *out_bounds) {
    double max_x = 0, min_x = 0;
    double max_y = 0, min_y = 0;
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        const Drawable_t *drawable = container->child_drawables->data[i];
        if ( drawable->layout.absolute )
            continue;

        max_x = fmax(max_x, drawable->bounds.x + drawable->bounds.w);
        min_x = fmin(min_x, drawable->bounds.x);

        max_y = fmax(max_y, drawable->bounds.y + drawable->bounds.h);
        min_y = fmin(min_y, drawable->bounds.y);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        const Container_t *child = container->child_containers->data[i];
        Bounds_t child_bounds = {0};
        measure_container_size(child, &child_bounds);

        max_x = fmax(max_x, child->bounds.x + child_bounds.x + child_bounds.w);
        min_x = fmin(min_x, child->bounds.x + child_bounds.x);

        max_y = fmax(max_y, child->bounds.y + child_bounds.y + child_bounds.h);
        min_y = fmin(min_y, child->bounds.y + child_bounds.y);
    }

    out_bounds->h = fmax(out_bounds->h, max_y - min_y);
    out_bounds->w = fmax(out_bounds->w, max_x - min_x);
}

static void recalculate_container_alignment(Container_t *container) {
    if ( container->parent != NULL )
        recalculate_container_alignment(container->parent);

    if ( container->flags & CONTAINER_VERTICAL_ALIGN_CONTENT || container->flags & CONTAINER_HORIZONTAL_ALIGN_CONTENT ) {
        container->align_content_offset_y = 0;
        container->align_content_offset_x = 0;
        Bounds_t bounds = {0};
        measure_container_size(container, &bounds);
        if ( container->flags & CONTAINER_VERTICAL_ALIGN_CONTENT ) {
            container->align_content_offset_y = (container->bounds.h - bounds.h) / 2.f;
        }
        if ( container->flags & CONTAINER_HORIZONTAL_ALIGN_CONTENT ) {
            container->align_content_offset_x = (container->bounds.w - bounds.w) / 2.f;
            if ( container->align_content_offset_x < 0 ) {
                bounds = (Bounds_t){0};
                measure_container_size(container, &bounds);
            }
        }

        container->content_size_dirty = true;
    }
}

static void position_layout(const Layout_t *layout, Container_t *parent, Bounds_t *out_bounds) {
    const Bounds_t *proportional_bounds_x = &parent->bounds;
    if ( layout->flags & LAYOUT_PROPORTIONAL_X_POS_TO_RELATIVE ) {
        if ( layout->relative_to == NULL ) {
            printf("Warning: LAYOUT_PROPORTIONAL_POS_TO_RELATIVE is set but no relative_to is assigned\n");
        } else {
            proportional_bounds_x = &layout->relative_to->bounds;
        }
    }

    double x = layout->offset_x;
    double calc_w = 0;
    if ( layout->flags & LAYOUT_ANCHOR_RIGHT_X ) {
        calc_w = out_bounds->w;
    } else if ( layout->flags & LAYOUT_ANCHOR_CENTER_X ) {
        calc_w = out_bounds->w / 2.0;
    }
    if ( layout->flags & LAYOUT_CENTER_X ) {
        x = proportional_bounds_x->w / 2.f - out_bounds->w / 2.f - calc_w;
    } else if ( layout->flags & LAYOUT_PROPORTIONAL_X ) {
        x = proportional_bounds_x->w * x;
    }

    if ( x < 0 && layout->flags & LAYOUT_WRAP_AROUND_X )
        x = proportional_bounds_x->w + x;
    x -= calc_w;

    const Bounds_t *proportional_bounds_y = &parent->bounds;
    if ( layout->flags & LAYOUT_PROPORTIONAL_Y_POS_TO_RELATIVE ) {
        if ( layout->relative_to == NULL ) {
            printf("Warning: LAYOUT_PROPORTIONAL_POS_TO_RELATIVE is set but no relative_to is assigned\n");
        } else {
            proportional_bounds_y = &layout->relative_to->bounds;
        }
    }
    double y = layout->offset_y;
    double calc_h = 0;
    if ( layout->flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
        calc_h = out_bounds->h;
    } else if ( layout->flags & LAYOUT_ANCHOR_CENTER_Y ) {
        calc_h = out_bounds->h / 2.f;
    }
    if ( layout->flags & LAYOUT_CENTER_Y ) {
        y = proportional_bounds_y->h / 2.f - out_bounds->h / 2.f - calc_h;
    } else if ( layout->flags & LAYOUT_PROPORTIONAL_Y ) {
        y = proportional_bounds_y->h * y;
    }

    if ( y < 0 && layout->flags & LAYOUT_WRAP_AROUND_Y )
        y = proportional_bounds_y->h + y;
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

    if ( animation->elapsed <= animation->delay ) {
        final_bounds->y = data->from_y;
        return;
    }
    double progress = (animation->elapsed - animation->delay) / animation->duration;

    // TODO: Animate the x axis too
    if ( progress < 1.0 ) {
        progress = apply_ease_func(progress, animation->ease_func);
        const double y_delta = data->to_y - data->from_y;
        if ( fabs(y_delta) > 0.01 ) {
            final_bounds->y = data->from_y + y_delta * progress;
        }
    } else {
        animation->active = false;
    }
}

static void apply_fade_animation(Animation_t *animation, int32_t *final_alpha) {
    const Animation_FadeInOutData_t *data = animation->custom_data;

    if ( animation->elapsed <= animation->delay ) {
        *final_alpha = data->from_alpha;
        return;
    }
    double progress = (animation->elapsed - animation->delay) / animation->duration;

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

static void apply_blur_radius_animation(Animation_t *animation, float *final_radius) {
    const Animation_BlurRadiusData_t *data = animation->custom_data;
    if ( animation->elapsed <= animation->delay ) {
        *final_radius = data->from_radius;
        return;
    }
    double progress = (animation->elapsed - animation->delay) / animation->duration;
    if ( progress < 1.0 ) {
        progress = apply_ease_func(progress, animation->ease_func);
        const float delta = data->to_radius - data->from_radius;
        *final_radius = data->from_radius + (float)(delta * progress);
    } else {
        animation->target->blur_radius = data->to_radius;
        animation->active = false;
    }
}

static void apply_scale_animation(Animation_t *animation, Bounds_t *final_bounds) {
    const Animation_ScaleData_t *data = animation->custom_data;

    if ( animation->elapsed <= animation->delay ) {
        final_bounds->scale_mod = data->from_scale;
        return;
    }
    const double progress = (animation->elapsed - animation->delay) / animation->duration;
    if ( progress < 1.0 ) {
        const double scale_delta = data->to_scale - data->from_scale;
        final_bounds->scale_mod = data->from_scale + scale_delta * progress;
    } else {
        animation->active = false;
    }
}

static void apply_draw_region_animation(Animation_t *animation, DrawRegionOptSet_t *regions) {
    const Animation_DrawRegionData_t *data = animation->custom_data;
    if ( animation->elapsed <= animation->delay ) {
        for ( int i = 0; i < regions->num_regions; i++ )
            regions->regions[i].x1_perc = data->draw_regions.regions[i].x1_perc;
        return;
    }
    double progress = (animation->elapsed - animation->delay) / animation->duration;
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

    if ( animation->elapsed <= animation->delay )
        return;
    double progress = (animation->elapsed - animation->delay) / animation->duration;

    if ( progress >= 1.0 ) {
        progress = 1.0;
        animation->active = false;
    }

    // progress = apply_ease_func(progress, data->ease_func);
    // Consider this will be the num_region'th region of the final set
    ScaleRegionOpt_t *opt = NULL;
    if ( regions->num_regions >= MAX_SCALE_SUB_REGIONS ) {
        // Find the oldest scale region so we can replace it

        // Special case, it just wrapped around
        if ( regions->regions[0].sequence < regions->regions[regions->num_regions - 1].sequence ) {
            opt = &regions->regions[0];
            opt->sequence = regions->regions[regions->num_regions - 1].sequence + 1;
        } else {
            // Find sequentially
            for ( int32_t i = 1; i < regions->num_regions; i++ ) {
                if ( regions->regions[i - 1].sequence < regions->regions[i].sequence ) {
                    opt = &regions->regions[i - 1];
                    opt->sequence = regions->regions[i].sequence + 1;
                }
            }
        }
    } else {
        opt = &regions->regions[regions->num_regions];
        if ( regions->num_regions > 0 ) {
            opt->sequence = regions->regions[regions->num_regions - 1].sequence + 1;
        }
        regions->num_regions += 1;
    }

    if ( opt == NULL ) {
        error_abort("Failed to find opt to replace");
    }
    const float scale_diff = data->scale_region.to_scale - data->scale_region.from_scale;
    opt->x0_perc = data->scale_region.x0_perc;
    opt->x1_perc = data->scale_region.x1_perc;
    opt->y0_perc = data->scale_region.y0_perc;
    opt->y1_perc = data->scale_region.y1_perc;
    opt->relative_scale = data->scale_region.from_scale + scale_diff * (float)progress;
    opt->from_scale = data->scale_region.from_scale;
    opt->to_scale = data->scale_region.to_scale;
}

typedef struct AnimationDelta {
    Bounds_t final_bounds;
    int32_t final_alpha;
    float color_mod;
    DrawRegionOptSet_t draw_regions;
    ScaleRegionOptSet_t scale_regions;
    float final_blur_radius;
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
            } else if ( animation->type == ANIM_BLUR_RADIUS ) {
                apply_blur_radius_animation(animation, &animation_delta->final_blur_radius);
            }
        }
    }
}

static void perform_draw(const Ui_t *ui, Drawable_t *drawable, const Bounds_t *base_bounds) {
    if ( !drawable->enabled || drawable->pending_recompute ) {
        return;
    }

    AnimationDelta delta = {.final_bounds = drawable->bounds,
                            .final_alpha = drawable->alpha_mod,
                            .color_mod = drawable->color_mod,
                            .draw_regions = {0},
                            .final_blur_radius = drawable->blur_radius};
    delta.draw_regions = drawable->draw_regions;
    apply_animations(drawable, &delta);

    Bounds_t rect = delta.final_bounds;
    rect.x += base_bounds->x;
    rect.y += base_bounds->y;

    if ( drawable->layout.absolute ) {
        rect.x -= drawable->parent->align_content_offset_x;
        rect.y -= drawable->parent->align_content_offset_y - drawable->parent->overflow_y.current_amount;
    }

    const Bounds_t *root_bounds = &ui_root_container(ui)->bounds;

    if ( rect.x + rect.w < 0 || rect.x > root_bounds->w || rect.y + rect.h < 0 || rect.y > root_bounds->h ) {
        return;
    }

    if ( drawable->dynamic ) {
        if ( drawable->type == DRAW_TYPE_RECTANGLE ) {
            draw_dynamic_rectangle(drawable, &rect, delta.final_alpha);
        } else {
            error_abort("Unrecognized dynamic drawable");
        }
        return;
    }

    DrawTextureOpts_t opts = {0};
    opts.scale_regions = &delta.scale_regions;
    opts.center_on_scale = drawable->center_on_scale;
    opts.skip_fb_invalidation = true;

    if ( drawable->shadow != NULL ) {
        Bounds_t shadow_bounds = rect;
        shadow_bounds.w = drawable->shadow->bounds.w;
        shadow_bounds.h = drawable->shadow->bounds.h;

        DrawTextureOpts_t shadow_opts = opts;
        const int32_t max_alpha = drawable->type == DRAW_TYPE_IMAGE ? 50 : 128;
        const uint8_t alpha = MIN(max_alpha, drawable->alpha_mod);
        shadow_opts.alpha_mod = alpha;
        shadow_opts.scale_regions = NULL;

        shadow_opts.blur_radius = (float)drawable->shadow->offset / 2.f;
        render_draw_texture(drawable->shadow->texture, &shadow_bounds, &shadow_opts);
    }

    opts.color_mod = delta.color_mod;
    if ( drawable->draw_underlay ) {
        opts.alpha_mod = drawable->underlay_alpha;
        render_draw_texture(drawable->texture, &rect, &opts);
    }

    opts.alpha_mod = delta.final_alpha;
    opts.draw_regions = &delta.draw_regions;
    opts.skip_fb_invalidation = false;
    if ( delta.final_blur_radius > 0.f ) {
        opts.blur_radius = delta.final_blur_radius;
        opts.blur_with_bg = true;
    }
    render_draw_texture(drawable->texture, &rect, &opts);
}

static void apply_container_animations(Container_t *container, Bounds_t *bounds) {
    for ( size_t i = 0; i < container->animations->size; i++ ) {
        ContainerAnimation_t *animation = container->animations->data[i];

        if ( animation->active ) {
            if ( animation->elapsed <= animation->delay )
                continue;
            const double progress = MIN(1.0, (animation->elapsed - animation->delay) / animation->duration);

            if ( animation->type == ANIM_EASE_TRANSLATION ) {
                const Animation_EaseTranslationData_t *data = animation->custom_data;
                const double eased = apply_ease_func(progress, animation->ease_func);
                const double delta_x = data->to_x - data->from_x, delta_y = data->to_y - data->from_y;
                bounds->x = data->from_x + delta_x * eased;
                bounds->y = data->from_y + delta_y * eased;
            }

            if ( animation->type == ANIM_BACKGROUND_COLOR ) {
                const Animation_ColorLerpData_t *data = animation->custom_data;
                const double eased = apply_ease_func(progress, animation->ease_func);
                Color_t *colors = container->background->colors;
                for ( int c = 0; c < 5; c++ ) {
                    const Color_t from = data->from_colors[c], to = data->to_colors[c];
                    colors[c].r = (uint8_t)(from.r + (to.r - from.r) * eased);
                    colors[c].g = (uint8_t)(from.g + (to.g - from.g) * eased);
                    colors[c].b = (uint8_t)(from.b + (to.b - from.b) * eased);
                    colors[c].a = (uint8_t)(from.a + (to.a - from.a) * eased);
                }
            }

            if ( animation->type == ANIM_SCROLL_Y ) {
                const Animation_ScrollYData_t *data = animation->custom_data;
                const double eased = apply_ease_func(progress, animation->ease_func);
                const double delta = data->to_amount - data->from_amount;
                container->overflow_y.current_amount = data->from_amount + delta * eased;
            }

            if ( progress >= 1.0 )
                animation->active = false;
        }
    }
}

ContainerScrollableArea_t gather_container_scrollable_area(const Container_t *container) {
    ContainerScrollableArea_t result = {0};

    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        const Drawable_t *drawable = container->child_drawables->data[i];
        result.min_content_y = MIN(result.min_content_y, drawable->bounds.y);
        result.max_content_y = MAX(result.max_content_y, drawable->bounds.y + drawable->bounds.h - container->bounds.h);

        result.min_content_x = MIN(result.min_content_x, drawable->bounds.x);
        result.max_content_x = MAX(result.max_content_x, drawable->bounds.x + drawable->bounds.w - container->bounds.w);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        const Container_t *child = container->child_containers->data[i];

        Bounds_t bounds = {0};
        measure_container_size(child, &bounds);

        result.min_content_x = MIN(result.min_content_x, child->bounds.x);
        result.max_content_x = MAX(result.max_content_x, child->bounds.x + bounds.w - container->bounds.w);

        result.min_content_y = MIN(result.min_content_y, child->bounds.y);
        result.max_content_y = MAX(result.max_content_y, child->bounds.y + bounds.h - container->bounds.h);
    }

    result.min_content_y -= container->bounds.h * container->overflow_y.relative_start_padding;
    result.max_content_y += container->bounds.h * container->overflow_y.relative_end_padding;

    result.total_height = result.max_content_y - result.min_content_y;
    result.total_width = result.max_content_x - result.min_content_x;

    return result;
}

static void recompute_vertical_scroll_bar_bounds(Container_t *container) {
    if ( container->overflow_y.scrollbar.kind == SCROLL_BAR_NONE ) {
        return;
    }
    if ( container->overflow_y.scrollbar.handle == NULL || container->overflow_y.scrollbar.background == NULL ) {
        error_abort("draw_container_vertical_scroll_bar: Manually set scrollbar type without initialization");
    }

    if ( container->content_size_dirty ) {
        container->scrollable_area = gather_container_scrollable_area(container);
        container->content_size_dirty = false;
    }

    Drawable_t *background = container->overflow_y.scrollbar.background;
    Drawable_t *handle = container->overflow_y.scrollbar.handle;

    const double total_height = container->scrollable_area.total_height;
    const double min_y = container->scrollable_area.min_content_y;
    const double scroll_ratio = (container->overflow_y.current_amount - min_y) / total_height;
    const double computed_handle_height = container->bounds.h / (total_height + container->bounds.h) * 0.9;

    if ( computed_handle_height >= 1.0 ) {
        background->enabled = handle->enabled = false;
        return;
    }
    background->enabled = handle->enabled = true;

    background->layout.width = render_measure_pixels_from_em(container->overflow_y.scrollbar.width_em);
    measure_layout(&background->layout, container, &background->bounds);
    position_layout(&background->layout, container, &background->bounds);

    const Layout_t layout = {.flags =
                                 LAYOUT_ANCHOR_RIGHT_X | LAYOUT_PROPORTIONAL_POS | LAYOUT_PROPORTIONAL_H | LAYOUT_WRAP_AROUND_X,
                             .offset_x = -0.005,
                             .offset_y = 0.05 + scroll_ratio * (0.9 - computed_handle_height),
                             .width = render_measure_pixels_from_em(container->overflow_y.scrollbar.width_em),
                             .height = computed_handle_height,
                             .absolute = true,
                             .z_index = 999 + 1};

    measure_layout(&layout, container, &handle->bounds);
    position_layout(&layout, container, &handle->bounds);
    handle->layout = layout;
}

static void vec_add_sorted_drawable(Vector_t *vec, Drawable_t *drawable) {
    const int32_t z = drawable->z_layer_index;
    size_t insert_at = vec->size;
    for ( size_t i = 0; i < vec->size; i++ ) {
        const Drawable_t *current = vec->data[i];
        if ( current->z_layer_index > z ) {
            insert_at = i;
            break;
        }
    }
    vec_add(vec, NULL);
    for ( size_t i = vec->size - 1; i > insert_at; i-- )
        vec->data[i] = vec->data[i - 1];
    vec->data[insert_at] = drawable;
}

static void vec_add_sorted_container(Vector_t *vec, Container_t *container) {
    const int32_t z = container->z_layer_index;
    size_t insert_at = vec->size;
    for ( size_t i = 0; i < vec->size; i++ ) {
        const Container_t *current = vec->data[i];
        if ( current->z_layer_index > z ) {
            insert_at = i;
            break;
        }
    }
    vec_add(vec, NULL);
    for ( size_t i = vec->size - 1; i > insert_at; i-- )
        vec->data[i] = vec->data[i - 1];
    vec->data[insert_at] = container;
}

static void draw_all_container(const Ui_t *ui, Container_t *container, Bounds_t base_bounds) {
    if ( !container->enabled )
        return;

    if ( container->content_size_dirty ) {
        container->scrollable_area = gather_container_scrollable_area(container);
        container->content_size_dirty = false;
    }

    if ( container->overflow_y.kind == OVERFLOW_SCROLL ) {
        recompute_vertical_scroll_bar_bounds(container);
    }

    Bounds_t container_bounds = container->bounds;
    apply_container_animations(container, &container_bounds);
    base_bounds.x += container_bounds.x;
    base_bounds.y += container_bounds.y;

    const Bounds_t container_screen_bounds = {
        .x = base_bounds.x,
        .y = base_bounds.y,
        .w = container_bounds.w,
        .h = container_bounds.h,
    };

    if ( container->background->type != BACKGROUND_NONE ) {
        render_draw_background(container->background, &container_screen_bounds);
    }

    if ( container->draw_debug_overlay ) {
        const Color_t color = {.r = 255, .g = 100, .b = 100, .a = 50};
        render_draw_rounded_rect(ui->null_texture, &container_screen_bounds, &color, 0);
    }

    base_bounds.x += container->align_content_offset_x;
    base_bounds.y += container->align_content_offset_y - container->overflow_y.current_amount;

    // Draw descendants of the current container at the same time, following the order of the z indices
    // vectors are already sorted at insertion time
    // note that changing the z index after initial creation/dynamically is not supported and will give incorrect results
    size_t d_idx = 0, c_idx = 0;
    while ( d_idx < container->child_drawables->size || c_idx < container->child_containers->size ) {
        const int32_t dz = d_idx < container->child_drawables->size
                               ? ((Drawable_t *)container->child_drawables->data[d_idx])->z_layer_index
                               : INT32_MAX;
        const int32_t cz = c_idx < container->child_containers->size
                               ? ((Container_t *)container->child_containers->data[c_idx])->z_layer_index
                               : INT32_MAX;
        // In case they're equal, favor drawables
        if ( dz <= cz ) {
            perform_draw(ui, container->child_drawables->data[d_idx], &base_bounds);
            d_idx += 1;
        } else {
            draw_all_container(ui, container->child_containers->data[c_idx], base_bounds);
            c_idx += 1;
        }
    }
}

void ui_draw(const Ui_t *ui) {
    const Bounds_t bounds = {0};
    draw_all_container(ui, ui->root_container, bounds);
}

void ui_add_event_callback(Ui_t *ui, const UiEvent_t event_type, Drawable_t *target, const c_ui_event_callback callback,
                           void *custom_data) {
    if ( callback == NULL )
        error_abort("ui_add_event_callback: callback is NULL");

    switch ( event_type ) {
    case UI_EVENT_MOUSE_MOVE:
    case UI_EVENT_MOUSE_STOPPED:
    case UI_EVENT_MOUSE_SCROLL:
    case UI_EVENT_KEY_PRESSED:
        printf("Warning: Global event passed to ui_add_event_callback: nothing will happen\n");
        return;
    default:
        break;
    }

    const int layer_idx = target->z_layer_index;
    const ZLayer_t *layer = append_z_layer(&ui->z_layers_head, layer_idx);

    for ( size_t i = 0; i < target->events->size; i++ ) {
        const EventDef_t *existing = target->events->data[i];
        if ( existing->type == event_type && existing->callback == callback && existing->custom_data == custom_data ) {
            return;
        }
    }

    EventDef_t *event = calloc(1, sizeof(*event));
    event->type = event_type;
    event->target = target;
    event->callback = callback;
    event->custom_data = custom_data;

    // or else it's been added to the z layers already
    if ( target->events->size == 0 ) {
        vec_add(layer->nodes, target);
    }
    vec_add(target->events, event);
}

void ui_add_global_event_callback(const Ui_t *ui, const UiEvent_t event_type, const c_ui_event_callback callback,
                                  void *custom_data) {
    if ( callback == NULL )
        error_abort("ui_add_event_callback: callback is NULL");

    switch ( event_type ) {
    case UI_EVENT_MOUSE_MOVE:
    case UI_EVENT_MOUSE_STOPPED:
    case UI_EVENT_MOUSE_SCROLL:
    case UI_EVENT_KEY_PRESSED:
        break;
    default:
        printf("Warning: Non-global event passed to ui_add_global_event_callback: nothing will happen\n");
        return;
    }

    for ( size_t i = 0; i < ui->global_events->size; i++ ) {
        const EventDef_t *existing = ui->global_events->data[i];
        if ( existing->type == event_type && existing->callback == callback && existing->custom_data == custom_data ) {
            return;
        }
    }

    EventDef_t *event = calloc(1, sizeof(*event));
    event->type = event_type;
    event->callback = callback;
    event->target = NULL;
    event->custom_data = custom_data;
    vec_add(ui->global_events, event);
}

static ContainerAnimation_t *find_container_scroll_y_anim(const Container_t *container) {
    for ( size_t i = 0; i < container->animations->size; i++ ) {
        ContainerAnimation_t *a = container->animations->data[i];
        if ( a->type == ANIM_SCROLL_Y )
            return a;
    }
    return NULL;
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
    result->events = vec_init();

    // Recalculate true size on next draw
    parent->content_size_dirty = true;

    return result;
}

static void on_drag_scrollbar_handle(const UiEventOpts_t *opts, Drawable_t *, void *custom) {
    Container_t *container = custom;
    const ContainerScrollableArea_t area = gather_container_scrollable_area(container);

    const Drawable_t *bg = container->overflow_y.scrollbar.background;
    const double bg_height = bg->bounds.h;
    const double handle_height = container->overflow_y.scrollbar.handle->bounds.h;
    const double travel = bg_height - handle_height;
    if ( travel <= 0 )
        return;

    double bg_top;
    ui_get_drawable_canon_pos(bg, NULL, &bg_top);

    const double desired_handle_top = opts->mouse.y - opts->mouse.grab_offset_y - bg_top;
    const double scroll_ratio = MAX(0.0, MIN(1.0, desired_handle_top / travel));
    const double pos = area.min_content_y + scroll_ratio * area.total_height;
    ui_container_scroll_y_to(container, pos);
}

static void on_click_scrollbar_background(const UiEventOpts_t *opts, Drawable_t *drawable, void *custom) {
    Container_t *container = custom;
    const double total_height = container->scrollable_area.total_height;

    double bg_top;
    ui_get_drawable_canon_pos(drawable, NULL, &bg_top);

    const double progress = MAX(0.0, MIN(1.0, (opts->mouse.y - bg_top) / drawable->bounds.h));
    const double pos = container->scrollable_area.min_content_y + total_height * progress;
    ui_container_scroll_y_to_dur(container, pos, (AnimatedSetOpts_t){.duration = 0.5});
}

static void on_hover_scrollbar(const UiEventOpts_t *opts, Drawable_t *, void *custom) {
    Container_t *container = custom;

    Drawable_t *bg = container->overflow_y.scrollbar.background;
    Drawable_t *handle = container->overflow_y.scrollbar.handle;
    const int32_t bg_alpha = container->overflow_y.scrollbar.background_color.a;
    const int32_t handle_alpha = container->overflow_y.scrollbar.handle_color.a;
    const double fade_duration = 0.2;
    if ( opts->event == UI_EVENT_MOUSE_HOVER_ENTERED ) {
        const AnimatedSetOpts_t anim_opts = {.duration = fade_duration, .apply_type = ANIM_APPLY_OVERRIDE};
        ui_drawable_set_alpha_dur(bg, bg_alpha, anim_opts);
        ui_drawable_set_alpha_dur(handle, handle_alpha, anim_opts);
        container->overflow_y.scrollbar.hovered = true;
    } else if ( opts->event == UI_EVENT_MOUSE_HOVER_EXITED ) {
        const AnimatedSetOpts_t anim_opts = {.duration = fade_duration, .delay = 0.5, .apply_type = ANIM_APPLY_OVERRIDE};
        ui_drawable_set_alpha_dur(bg, 0, anim_opts);
        ui_drawable_set_alpha_dur(handle, 0, anim_opts);
        container->overflow_y.scrollbar.hovered = false;
    }
}

static void on_container_scroll(const UiEventOpts_t *opts, Drawable_t *, void *custom) {
    const Container_t *container = custom;

    if ( container != opts->mouse.scroll_info.container )
        return;

    Drawable_t *bg = container->overflow_y.scrollbar.background;
    Drawable_t *handle = container->overflow_y.scrollbar.handle;
    const int32_t bg_alpha = container->overflow_y.scrollbar.background_color.a;
    const int32_t handle_alpha = container->overflow_y.scrollbar.handle_color.a;
    // Show immediately, then fade out. Using OVERRIDE for fade-out means repeated scroll events
    // just reset the timer rather than chaining additional animations.
    if ( !container->overflow_y.scrollbar.hovered ) {
        ui_drawable_set_alpha_immediate(bg, bg_alpha);
        ui_drawable_set_alpha_immediate(handle, handle_alpha);
        const AnimatedSetOpts_t anim_opts = {.duration = 1.0, .delay = 0.5, .apply_type = ANIM_APPLY_OVERRIDE};
        ui_drawable_set_alpha_dur(bg, 0, anim_opts);
        ui_drawable_set_alpha_dur(handle, 0, anim_opts);
    }
}

void ui_container_add_vertical_scrollbar(Ui_t *ui, Container_t *container, const ScrollBarKind_t kind) {
    if ( container->overflow_y.scrollbar.handle != NULL ) {
        printf("Warning: ui_container_add_vertical_scrollbar: Already has scrollbar\n");
        return;
    }
    if ( container->overflow_y.kind != OVERFLOW_SCROLL )
        error_abort("ui_container_add_vertical_scrollbar: Container not scrollable\n");

    container->overflow_y.scrollbar.kind = kind;

    const Color_t bg_color = {.r = 100, .g = 100, .b = 100, .a = 150};
    const Color_t handle_color = {.r = 150, .g = 150, .b = 150, .a = 255};

    const Layout_t layout = {.flags =
                                 LAYOUT_ANCHOR_RIGHT_X | LAYOUT_PROPORTIONAL_POS | LAYOUT_PROPORTIONAL_H | LAYOUT_WRAP_AROUND_X,
                             .offset_x = -0.005,
                             .offset_y = 0.05,
                             .width = render_measure_pixels_from_em(container->overflow_y.scrollbar.width_em),
                             .height = 0.9,
                             .absolute = true,
                             .z_index = 999};
    Drawable_RectangleData_t data = {.border_radius_em = BORDER_RADIUS_AUTO, .color = bg_color};
    container->overflow_y.scrollbar.background = ui_make_rectangle(ui, &data, container, &layout);
    data.color = handle_color;
    container->overflow_y.scrollbar.handle = ui_make_rectangle(ui, &data, container, &(Layout_t){.z_index = 999 + 1});

    container->overflow_y.scrollbar.width_em = 0.5;
    container->overflow_y.scrollbar.background_color = bg_color;
    container->overflow_y.scrollbar.handle_color = handle_color;

    Drawable_t *background = container->overflow_y.scrollbar.background;
    ui_add_event_callback(ui, UI_EVENT_MOUSE_CLICK, background, on_click_scrollbar_background, container);

    Drawable_t *handle = container->overflow_y.scrollbar.handle;
    ui_add_event_callback(ui, UI_EVENT_MOUSE_DRAG, handle, on_drag_scrollbar_handle, container);

    if ( kind == SCROLL_BAR_AUTO_HIDE ) {
        const Animation_FadeInOutData_t fade_data = {.duration = 5.0, .ease_func = ANIM_EASE_NONE};
        ui_animate_fade(background, &fade_data);
        ui_animate_fade(handle, &fade_data);

        ui_drawable_set_alpha_immediate(background, 0);
        ui_drawable_set_alpha_immediate(handle, 0);

        ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_ENTERED, background, on_hover_scrollbar, container);
        ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_EXITED, background, on_hover_scrollbar, container);
        ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_ENTERED, handle, on_hover_scrollbar, container);
        ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_EXITED, handle, on_hover_scrollbar, container);

        ui_add_global_event_callback(ui, UI_EVENT_MOUSE_SCROLL, on_container_scroll, container);
    }

    container->content_size_dirty = true;
    recompute_vertical_scroll_bar_bounds(container);
}

void ui_container_scroll_y_by(Container_t *container, const double amount) {
    if ( container->overflow_y.kind != OVERFLOW_SCROLL )
        error_abort("ui_container_scroll_by_vertical: Container is not scrollable");

    const double current_amount = container->overflow_y.current_amount;
    const ContainerScrollableArea_t area = gather_container_scrollable_area(container);
    const double target = MAX(area.min_content_y, MIN(area.max_content_y, current_amount - amount));
    container->overflow_y.set_amount = target;

    ContainerAnimation_t *scroll_anim = find_container_scroll_y_anim(container);
    if ( scroll_anim != NULL ) {
        Animation_ScrollYData_t *data = scroll_anim->custom_data;
        data->from_amount = container->overflow_y.current_amount;
        data->to_amount = target;
        scroll_anim->elapsed = 0.0;
        scroll_anim->active = true;
        scroll_anim->duration = data->duration;
        scroll_anim->ease_func = data->ease_func;
    } else {
        container->overflow_y.current_amount = target;
    }
}

void ui_container_scroll_y_to(Container_t *container, const double position) {
    if ( container->overflow_y.kind != OVERFLOW_SCROLL )
        error_abort("ui_container_scroll_by_vertical: Container is not scrollable");

    const ContainerScrollableArea_t area = gather_container_scrollable_area(container);
    const double target = MAX(area.min_content_y, MIN(area.max_content_y, position));
    container->overflow_y.set_amount = target;

    ContainerAnimation_t *scroll_anim = find_container_scroll_y_anim(container);
    if ( scroll_anim != NULL ) {
        Animation_ScrollYData_t *data = scroll_anim->custom_data;
        data->from_amount = container->overflow_y.current_amount;
        data->to_amount = target;
        scroll_anim->elapsed = 0.0;
        scroll_anim->active = true;
        scroll_anim->duration = data->duration;
        scroll_anim->ease_func = data->ease_func;
    } else {
        container->overflow_y.current_amount = target;
    }
}

void ui_container_scroll_y_to_dur(Container_t *container, const double position, const AnimatedSetOpts_t opts) {
    if ( container->overflow_y.kind != OVERFLOW_SCROLL )
        error_abort("ui_container_scroll_by_vertical: Container is not scrollable");

    const ContainerScrollableArea_t area = gather_container_scrollable_area(container);
    const double target = MAX(area.min_content_y, MIN(area.max_content_y, position));
    container->overflow_y.set_amount = target;

    ContainerAnimation_t *scroll_anim = find_container_scroll_y_anim(container);
    if ( scroll_anim != NULL ) {
        Animation_ScrollYData_t *data = scroll_anim->custom_data;
        data->from_amount = container->overflow_y.current_amount;
        data->to_amount = target;
        scroll_anim->elapsed = 0.0;
        scroll_anim->active = true;
        scroll_anim->duration = opts.duration;
        scroll_anim->delay = opts.delay;
        scroll_anim->ease_func = data->ease_func;
    } else {
        printf("Warning: ui_container_scroll_y_to_dur: no scroll y animation set, no duration is applied.\n");
        container->overflow_y.current_amount = target;
    }
}

void ui_end_loop(void) { render_present(); }

void ui_set_window_title(const char *title) { render_set_window_title(title); }

void ui_finish(Ui_t *ui) {
    // Free stored textures and drawables
    ui_destroy_container(ui, ui->root_container);
    // Free global events
    for ( size_t i = 0; i < ui->global_events->size; i++ ) {
        free(ui->global_events->data[i]);
    }
    vec_destroy(ui->global_events);
    // Free z layers
    ZLayer_t *cur = ui->z_layers_head;
    while ( cur != NULL ) {
        // No need to individually destroy the nodes since they're weak references to drawables
        vec_destroy(cur->nodes);
        ZLayer_t *temp = cur->prev;
        free(cur);
        cur = temp;
    }
    // Cleanup
    free(ui);
}

Container_t *ui_root_container(const Ui_t *ui) { return ui->root_container; }

void ui_get_drawable_canon_pos(const Drawable_t *drawable, double *x, double *y) {
    double parent_x = 0, parent_y = 0;
    ui_get_container_canon_pos(drawable->parent, &parent_x, &parent_y, !drawable->layout.absolute);

    if ( !drawable->layout.absolute ) {
        parent_x += drawable->parent->align_content_offset_x;
        parent_y += drawable->parent->align_content_offset_y;
    }

    if ( x != NULL )
        *x = parent_x + drawable->bounds.x;
    if ( y != NULL )
        *y = parent_y + drawable->bounds.y;
}

void ui_get_container_canon_pos(const Container_t *container, double *x, double *y, const bool include_viewport_offset) {
    double parent_x = 0, parent_y = 0;
    const Container_t *parent = container, *prev = NULL;
    while ( parent != NULL ) {
        parent_x += parent->bounds.x;
        parent_y += parent->bounds.y;
        if ( prev != NULL ) {
            parent_x += parent->align_content_offset_x;
            parent_y += parent->align_content_offset_y;
        }
        if ( include_viewport_offset ) {
            parent_y -= parent->overflow_y.current_amount;
        }
        prev = parent;
        parent = parent->parent;
    }

    if ( x != NULL )
        *x = parent_x;
    if ( y != NULL )
        *y = parent_y;
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

static Drawable_RectangleData_t *dup_rectangle_data(const Drawable_RectangleData_t *data) {
    Drawable_RectangleData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate rectangle data");
    }
    result->color = data->color;
    result->border_radius_em = data->border_radius_em;
    return result;
}

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
        const int32_t c = str_u8_next(text, size, &current_idx);
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

        if ( c == ' ' || c == '(' || c == ')' ) {
            // If we're breaking on a space, keep it on the previous line so the alignment doesn't look weird
            // but for a ( or ), push it onto the next line
            if ( c == ' ' ) {
                // 1 here is fine because this char is guaranteed 1 byte
                last_safe_break_idx = char_start_idx + 1;
            } else {
                last_safe_break_idx = char_start_idx;
            }
        } else if ( str_ch_is_japanese_particle(c) || str_ch_is_japanese_punctuation(c) ) {
            if ( str_ch_is_japanese_punctuation(c) ) {
                // If we're breaking on a punctuation character, include it in the line as it looks weird if it's the first
                // character on the following line
                last_safe_break_idx = current_idx;
            } else {
                last_particle_break_idx = char_start_idx;
            }
        }

        if ( current_line_width > calculated_max_width ) {
            if ( current_idx == size && current_line_width <= m_current_width * 0.95 ) {
                return size;
            }

            // Prefer to break on punctuation rather than particles if possible
            if ( last_safe_break_idx != -1 && last_safe_break_idx > start ) {
                return last_safe_break_idx;
            }

            if ( is_japanese_context && last_particle_break_idx != -1 && last_particle_break_idx > start ) {
                return last_particle_break_idx;
            }

            if ( is_japanese_context ) {
                bool last_was_kanji = str_ch_is_kanji(c);

                while ( current_idx < size ) {
                    const int32_t prev_current_idx = current_idx;
                    const int32_t next_c = str_u8_next(text, size, &current_idx);
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
            return char_start_idx > start ? char_start_idx - 1 : current_idx - 1;
        }

        prev_c = c;
    }

    return size;
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

        const int32_t c = str_u8_next(line, text_size, &i);
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
        data->compute_offsets && (config_get()->karaoke.enable_dynamic_fill || config_get()->karaoke.enable_reading_hints);

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

            const double dest_x = x + texture->left_bearing_offset;
            Bounds_t destination = {.x = dest_x, .y = y, .w = (float)texture->width, .h = (float)texture->height};
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
    result->z_layer_index = layout->z_index + container->z_layer_index;
    ui_reposition_drawable(result);

    if ( data->draw_shadow ) {
        const int32_t text_pixels = render_measure_pixels_from_em(data->em);
        const int32_t offset = (int32_t)MAX(1.f, MIN(10.f, text_pixels * 0.1f));
        result->shadow = render_make_shadow(result->texture, &result->bounds, offset);
    }

    return result;
}

Drawable_t *ui_make_text(Ui_t *ui, const Drawable_TextData_t *data, Container_t *container, const Layout_t *layout) {
    Drawable_t *result = make_drawable(container, DRAW_TYPE_TEXT, false);
    internal_make_text(ui, result, data, container, layout);
    vec_add_sorted_drawable(container->child_drawables, result);
    return result;
}

static void apply_shadow_to_image(Drawable_t *drawable) {
    if ( drawable->shadow ) {
        render_destroy_shadow(drawable->shadow);
    }
    const int32_t offset = MAX(1, drawable->bounds.w * 0.01f);
    drawable->shadow = render_make_shadow(drawable->texture, &drawable->bounds, offset);
}

Drawable_t *ui_make_image(const unsigned char *bytes, const int length, const Drawable_ImageData_t *weak_data,
                          Container_t *container, const Layout_t *layout) {
    Drawable_t *result = make_drawable(container, DRAW_TYPE_IMAGE, false);
    Drawable_ImageData_t *data = dup_image_data(weak_data);

    Texture_t *texture = render_make_image(bytes, length, data->border_radius_em);
    result->bounds.w = texture->width;
    result->bounds.h = texture->height;

    result->custom_data = data;
    result->texture = texture;
    result->layout = *layout;
    result->z_layer_index = layout->z_index + container->z_layer_index;

    ui_reposition_drawable(result);

    if ( data->draw_shadow ) {
        apply_shadow_to_image(result);
    }
    vec_add_sorted_drawable(container->child_drawables, result);
    return result;
}

Drawable_t *ui_make_rectangle(Ui_t *ui, const Drawable_RectangleData_t *data, Container_t *container, const Layout_t *layout) {
    Drawable_t *result = make_drawable(container, DRAW_TYPE_RECTANGLE, true);

    result->texture = render_make_null();
    result->custom_data = dup_rectangle_data(data);
    result->layout = *layout;
    result->z_layer_index = layout->z_index + container->z_layer_index;
    result->alpha_mod = data->color.a;

    ui_reposition_drawable(result);
    vec_add_sorted_drawable(container->child_drawables, result);
    return result;
}

Drawable_t *ui_make_custom(Ui_t *ui, Container_t *container, const Layout_t *layout) {
    Drawable_t *result = make_drawable(container, DRAW_TYPE_CUSTOM_TEXTURE, false);

    result->texture = render_make_null();
    result->custom_data = NULL;
    result->layout = *layout;
    result->z_layer_index = layout->z_index + container->z_layer_index;
    result->pending_recompute = true;

    ui_reposition_drawable(result);
    vec_add_sorted_drawable(container->child_drawables, result);
    return result;
}

void ui_destroy_drawable(Ui_t *ui, Drawable_t *drawable) {
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
    vec_destroy(drawable->active_animations);
    for ( size_t i = 0; i < drawable->animations->size; i++ ) {
        animation_destroy(drawable->animations->data[i], true);
    }
    vec_destroy(drawable->animations);
    const bool has_events = drawable->events->size > 0;
    for ( size_t i = 0; i < drawable->events->size; i++ ) {
        free(drawable->events->data[i]);
    }
    vec_destroy(drawable->events);
    // Find the drawable in the scene graph
    const Container_t *parent = drawable->parent;
    for ( size_t i = 0; i < parent->child_drawables->size; i++ ) {
        if ( parent->child_drawables->data[i] == drawable ) {
            vec_remove(parent->child_drawables, i);
            break;
        }
    }
    // Find the drawable in the z layers
    if ( has_events ) {
        const int layer_index = drawable->z_layer_index;
        // guaranteed (maybe?) to not allocate since it should have been already allocated when the drawable
        //  was added to the z layers when an event was attached to it
        const ZLayer_t *layer = append_z_layer(&ui->z_layers_head, layer_index);
        for ( size_t i = 0; i < layer->nodes->size; i++ ) {
            if ( layer->nodes->data[i] == drawable ) {
                vec_remove(layer->nodes, i);
                break;
            }
        }
    }

    if ( ui->current_hovered_drawable == drawable )
        ui->current_hovered_drawable = NULL;
    if ( ui->current_dragged_drawable == drawable )
        ui->current_dragged_drawable = NULL;
    free(drawable);
}

double ui_compute_relative_horizontal(double value, const Container_t *parent) { return parent->bounds.w * value; }

Container_t *ui_make_container(const Ui_t *ui, Container_t *parent, const Layout_t *layout, const ContainerFlags_t flags) {
    Container_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate container");
    }

    if ( parent == NULL && ui->root_container != NULL ) {
        error_abort("Attempted to initialize a container without a parent");
    }

    result->parent = parent;
    // Make a copy of the layout
    result->layout = *layout;
    result->z_layer_index = layout->z_index + (parent != NULL ? parent->z_layer_index : 0);
    result->child_drawables = vec_init();
    result->child_containers = vec_init();
    result->animations = vec_init();
    result->widgets = vec_init();
    result->background = render_make_background(BACKGROUND_NONE);
    result->enabled = true;
    result->flags = flags;

    if ( parent != NULL ) {
        measure_layout(layout, parent, &result->bounds);
        position_layout(layout, parent, &result->bounds);
        vec_add_sorted_container(parent->child_containers, result);
        if ( result->z_layer_index > 0 )
            vec_add(ui->opaque_containers, result);
    }

    return result;
}

void ui_destroy_container(Ui_t *ui, Container_t *container) {
    while ( container->widgets->size > 0 ) {
        const WidgetEntry_t *entry = container->widgets->data[0];
        entry->destroy_callback(ui, entry->widget_data);
    }
    vec_destroy(container->widgets);

    // Destroy the actual drawables after because widgets will have already destroyed the widgets they spawned, and doing this
    // first would lead to a double free
    // The destroy callback needs to destroy the drawables otherwise they'll be stuck on the screen until the container is
    // destroyed
    while ( container->child_drawables->size > 0 )
        ui_destroy_drawable(ui, container->child_drawables->data[0]);
    vec_destroy(container->child_drawables);

    while ( container->child_containers->size > 0 )
        ui_destroy_container(ui, container->child_containers->data[0]);
    vec_destroy(container->child_containers);

    if ( container->parent != NULL ) {
        for ( size_t i = 0; i < container->parent->child_containers->size; i++ ) {
            if ( container->parent->child_containers->data[i] == container ) {
                vec_remove(container->parent->child_containers, i);
                break;
            }
        }
    }

    for ( size_t i = 0; i < ui->opaque_containers->size; i++ ) {
        if ( ui->opaque_containers->data[i] == container ) {
            vec_remove(ui->opaque_containers, i);
            break;
        }
    }

    for ( size_t i = 0; i < container->animations->size; i++ ) {
        ContainerAnimation_t *animation = container->animations->data[i];
        free(animation->custom_data);
        free(animation);
    }
    vec_destroy(container->animations);

    render_destroy_background(container->background);

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
    for ( int32_t i = (int32_t)drawable->active_animations->size - 1; i >= 0; i-- ) {
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
        if ( drawable->shadow != NULL ) {
            render_destroy_shadow(drawable->shadow);
            drawable->shadow = NULL;
        }
        internal_make_text(ui, drawable, old_custom_data, container, &drawable->layout);
        free_text_data(old_custom_data);
    } else if ( drawable->type == DRAW_TYPE_IMAGE ) {
        const Drawable_ImageData_t *data = drawable->custom_data;
        if ( data->border_radius_em > 0 ) {
            drawable->texture->border_radius = (float)render_measure_pt_from_em(data->border_radius_em);
        }
        ui_reposition_drawable(drawable);
        if ( data->draw_shadow ) {
            apply_shadow_to_image(drawable);
        }
    } else if ( drawable->type == DRAW_TYPE_RECTANGLE ) {
        ui_reposition_drawable(drawable);
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
        position_layout(&container->layout, container->parent, &container->bounds);
    }

    // First measure drawables that don't depend on any other for their size
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        Drawable_t *drawable = container->child_drawables->data[i];
        if ( drawable->layout.relative_to_size == NULL )
            ui_recompute_drawable(ui, drawable);
    }
    // then those that do (saves having to store a flag whether they were recomputed or not)
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        Drawable_t *drawable = container->child_drawables->data[i];
        if ( drawable->layout.relative_to_size != NULL )
            ui_recompute_drawable(ui, drawable);
    }
    // then position
    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        Drawable_t *drawable = container->child_drawables->data[i];
        position_layout(&drawable->layout, drawable->parent, &drawable->bounds);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        if ( container->child_containers->data[i] != NULL ) {
            ui_recompute_container(ui, container->child_containers->data[i]);
        }
    }

    for ( size_t i = 0; i < container->widgets->size; i++ ) {
        const WidgetEntry_t *cb = container->widgets->data[i];
        if ( cb->configure_callback != NULL )
            cb->configure_callback(cb->widget_data);
    }
}

void ui_reposition_container(Container_t *container) {
    const double old_x = container->bounds.x, old_y = container->bounds.y;
    if ( container->parent != NULL ) {
        measure_layout(&container->layout, container->parent, &container->bounds);
        position_layout(&container->layout, container->parent, &container->bounds);
    }

    container->content_size_dirty = true;
    if ( old_x != container->bounds.x || old_y != container->bounds.y ) {
        ContainerAnimation_t *animation = NULL;
        for ( size_t i = 0; i < container->animations->size; i++ ) {
            ContainerAnimation_t *current = container->animations->data[i];
            if ( current->type == ANIM_EASE_TRANSLATION ) {
                animation = current;
                break;
            }
        }

        if ( animation != NULL ) {
            animation->elapsed = 0;
            animation->active = true;
            Animation_EaseTranslationData_t *data = animation->custom_data;
            data->from_x = old_x;
            data->from_y = old_y;
            data->to_x = container->bounds.x;
            data->to_y = container->bounds.y;
        }
    }

    for ( size_t i = 0; i < container->child_drawables->size; i++ ) {
        ui_reposition_drawable(container->child_drawables->data[i]);
    }

    for ( size_t i = 0; i < container->child_containers->size; i++ ) {
        Container_t *child_container = container->child_containers->data[i];
        if ( child_container != NULL ) {
            ui_reposition_container(child_container);
        }
    }

    for ( size_t i = 0; i < container->widgets->size; i++ ) {
        const WidgetEntry_t *cb = container->widgets->data[i];
        if ( cb->configure_callback != NULL )
            cb->configure_callback(cb->widget_data);
    }
}

void ui_on_window_changed(Ui_t *ui) {
    render_on_window_changed();
    ui->root_container->bounds = *render_get_viewport();
    ui_recompute_container(ui, ui->root_container);
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

static Animation_BlurRadiusData_t *dup_anim_blur_radius_data(const Animation_BlurRadiusData_t *data) {
    Animation_BlurRadiusData_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate blur radius animation data");
    }
    result->from_radius = data->from_radius;
    result->to_radius = data->to_radius;
    result->duration = data->duration;
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
    // TODO: Remove allocation on every invocation of animations by reusing existing buffers
    Animation_t *animation = calloc(1, sizeof(*animation));
    // copy everything from the base anim
    memcpy(animation, base_anim, sizeof(*animation));
    animation->elapsed = 0.0;
    animation->delay = 0.0;
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
    case ANIM_BLUR_RADIUS:
        animation->custom_data = dup_anim_blur_radius_data(base_anim->custom_data);
        break;
    case ANIM_BACKGROUND_COLOR:
        error_abort("reapply_animation: ANIM_COLOR_LERP is container-only and cannot be reapplied as a drawable animation");
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

void ui_reposition_drawable(Drawable_t *drawable) {
    const double old_x = drawable->bounds.x, old_y = drawable->bounds.y;

    measure_layout(&drawable->layout, drawable->parent, &drawable->bounds);
    position_layout(&drawable->layout, drawable->parent, &drawable->bounds);

    if ( old_x != drawable->bounds.x || old_y != drawable->bounds.y ) {
        // recompute scrollable bounds
        drawable->parent->content_size_dirty = true;

        const Animation_t *base_anim = find_animation(drawable, ANIM_EASE_TRANSLATION);
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
        const Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
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

void ui_drawable_set_scale_factor_dur(Drawable_t *drawable, float scale, AnimatedSetOpts_t opts) {
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
            data->duration = opts.duration;
            animation->delay = opts.delay;
        }
    }
    drawable->bounds.scale_mod = scale_mod;
}

void ui_drawable_set_color_mod(Drawable_t *drawable, const float color_mod) { drawable->color_mod = color_mod; }

void ui_drawable_set_draw_region(Drawable_t *drawable, const DrawRegionOptSet_t *draw_regions) {
    double duration = 0.0;
    const Animation_t *base_anim = find_animation(drawable, ANIM_DRAW_REGION);
    if ( base_anim != NULL ) {
        const Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
        if ( animation != NULL ) {
            const Animation_DrawRegionData_t *data = animation->custom_data;
            duration = data->duration;
        }
    }

    ui_drawable_set_draw_region_dur(drawable, draw_regions, (AnimatedSetOpts_t){.duration = duration});
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

void ui_drawable_set_draw_region_dur(Drawable_t *drawable, const DrawRegionOptSet_t *draw_regions, AnimatedSetOpts_t opts) {
    const Animation_t *base_anim = find_animation(drawable, ANIM_DRAW_REGION);
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
            animation->duration = opts.duration;
            animation->delay = opts.delay;

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

void ui_drawable_set_image(Drawable_t *drawable, const unsigned char *bytes, const int length) {
    if ( drawable == NULL )
        error_abort("ui_drawable_set_image: Drawable is null");
    if ( drawable->type != DRAW_TYPE_IMAGE )
        error_abort("ui_drawable_set_image: Drawable is not an image");

    render_destroy_texture(drawable->texture);
    drawable->texture = NULL;

    const Drawable_ImageData_t *data = drawable->custom_data;
    drawable->texture = render_make_image(bytes, length, data->border_radius_em);
    drawable->bounds.w = drawable->texture->width;
    drawable->bounds.h = drawable->texture->height;

    ui_reposition_drawable(drawable);
}

void ui_drawable_set_alpha(Drawable_t *drawable, const int32_t alpha) {
    if ( alpha == drawable->alpha_mod ) {
        return;
    }

    const Animation_t *base_anim = find_animation(drawable, ANIM_FADE_IN_OUT);
    if ( base_anim != NULL ) {
        const Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
        if ( animation != NULL ) {
            Animation_FadeInOutData_t *data = animation->custom_data;
            data->from_alpha = drawable->alpha_mod;
            data->to_alpha = alpha;
        }
    }
    drawable->alpha_mod = alpha;
}

void ui_drawable_set_alpha_dur(Drawable_t *drawable, int32_t alpha, AnimatedSetOpts_t opts) {
    if ( alpha == drawable->alpha_mod ) {
        return;
    }

    const Animation_t *base_anim = find_animation(drawable, ANIM_FADE_IN_OUT);
    if ( base_anim != NULL ) {
        Animation_t *animation = reapply_animation(drawable, base_anim, opts.apply_type);
        if ( animation != NULL ) {
            Animation_FadeInOutData_t *data = animation->custom_data;
            data->from_alpha = drawable->alpha_mod;
            data->to_alpha = alpha;
            animation->duration = opts.duration;
            animation->delay = opts.delay;
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

void ui_drawable_set_blur_radius(Drawable_t *drawable, const float radius) {
    if ( radius == drawable->blur_radius )
        return;
    const Animation_t *base_anim = find_animation(drawable, ANIM_BLUR_RADIUS);
    if ( base_anim != NULL ) {
        const Animation_t *animation = reapply_animation(drawable, base_anim, base_anim->apply_type);
        if ( animation != NULL ) {
            Animation_BlurRadiusData_t *data = animation->custom_data;
            data->from_radius = drawable->blur_radius;
            data->to_radius = radius;
        }
    }
    drawable->blur_radius = radius;
}

void ui_drawable_set_blur_radius_immediate(Drawable_t *drawable, const float radius) {
    if ( radius == drawable->blur_radius )
        return;
    Animation_t *anim = find_active_animation(drawable, ANIM_BLUR_RADIUS);
    if ( anim != NULL ) {
        anim->elapsed = anim->duration;
        anim->active = false;
    }
    drawable->blur_radius = radius;
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

void ui_drawable_add_scale_region_dur(const Drawable_t *drawable, const ScaleRegionOpt_t *region, AnimatedSetOpts_t opts) {
    const Animation_t *base_anim = find_animation(drawable, ANIM_SCALE_REGION);
    // TODO: This function doesn't do anything if there's no animation attached
    if ( base_anim != NULL ) {
        AnimationApplyType_t apply_type = opts.apply_type;
        if ( apply_type == ANIM_APPLY_DEFAULT )
            apply_type = base_anim->apply_type;
        Animation_t *animation = reapply_animation(drawable, base_anim, apply_type);
        if ( animation != NULL ) {
            Animation_ScaleRegionData_t *data = animation->custom_data;
            data->scale_region = *region;
            animation->duration = opts.duration;
            animation->delay = opts.delay;
        }
    }
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

void ui_animate_blur(Drawable_t *target, const Animation_BlurRadiusData_t *data) {
    if ( target == NULL ) {
        error_abort("Target drawable is NULL");
    }

    Animation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_BLUR_RADIUS;
    result->custom_data = dup_anim_blur_radius_data(data);
    result->target = target;
    result->duration = data->duration;
    result->active = false;
    result->ease_func = data->ease_func;
    result->apply_type = ANIM_APPLY_OVERRIDE;

    vec_add(target->animations, result);
}

void ui_container_animate_translation(Container_t *container, const Animation_EaseTranslationData_t *data) {
    if ( container == NULL ) {
        error_abort("Target drawable is NULL");
    }

    ContainerAnimation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate animation");
    }

    result->type = ANIM_EASE_TRANSLATION;
    result->custom_data = dup_anim_translate_data(data);
    result->target = container;
    result->duration = data->duration;
    result->active = false;
    result->ease_func = data->ease_func;

    vec_add(container->animations, result);
}

void ui_container_animate_color_lerp(Container_t *container, const double duration, const AnimationEaseType_t ease_func) {
    ContainerAnimation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate color lerp animation");
    }
    Animation_ColorLerpData_t *data = calloc(1, sizeof(*data));
    if ( data == NULL ) {
        error_abort("Failed to allocate color lerp animation data");
    }
    data->duration = duration;
    data->ease_func = ease_func;
    result->type = ANIM_BACKGROUND_COLOR;
    result->custom_data = data;
    result->target = container;
    result->duration = duration;
    result->active = false;
    result->ease_func = ease_func;
    vec_add(container->animations, result);
}

void ui_container_animate_scroll_y(Container_t *container, const double duration, const AnimationEaseType_t ease_func) {
    ContainerAnimation_t *result = calloc(1, sizeof(*result));
    if ( result == NULL ) {
        error_abort("Failed to allocate scroll y animation");
    }
    Animation_ScrollYData_t *data = calloc(1, sizeof(*data));
    if ( data == NULL ) {
        error_abort("Failed to allocate scroll y animation data");
    }
    data->duration = duration;
    data->ease_func = ease_func;

    result->type = ANIM_SCROLL_Y;
    result->custom_data = data;
    result->target = container;
    result->duration = duration;
    result->active = false;
    result->ease_func = ease_func;
    vec_add(container->animations, result);
}

void ui_container_update_background_colors(const Container_t *container, const Color_t *colors, size_t size) {
    ContainerAnimation_t *lerp_anim = NULL;
    for ( size_t i = 0; i < container->animations->size; i++ ) {
        ContainerAnimation_t *a = container->animations->data[i];
        if ( a->type == ANIM_BACKGROUND_COLOR ) {
            lerp_anim = a;
            break;
        }
    }

    if ( lerp_anim != NULL ) {
        Animation_ColorLerpData_t *data = lerp_anim->custom_data;
        for ( int i = 0; i < 5; i++ )
            data->from_colors[i] = container->background->colors[i];
        for ( int i = 0; i < 5; i++ ) {
            if ( (size_t)i < size )
                data->to_colors[i] = colors[i];
            else
                data->to_colors[i] = (Color_t){0, 0, 0, 255};
        }
        lerp_anim->elapsed = 0.0;
        lerp_anim->active = true;
    } else {
        for ( int i = 0; i < 5; i++ ) {
            if ( (size_t)i < size )
                container->background->colors[i] = colors[i];
            else
                container->background->colors[i] = (Color_t){0, 0, 0, 255};
        }
    }
}

void ui_container_update_background_colors_immediate(const Container_t *container, const Color_t *colors, size_t size) {
    for ( size_t i = 0; i < container->animations->size; i++ ) {
        ContainerAnimation_t *a = container->animations->data[i];
        if ( a->type == ANIM_BACKGROUND_COLOR ) {
            a->active = false;
            break;
        }
    }
    for ( int i = 0; i < 5; i++ ) {
        if ( (size_t)i < size )
            container->background->colors[i] = colors[i];
        else
            container->background->colors[i] = (Color_t){0, 0, 0, 255};
    }
}

static void toggle_widget_reconfigure(void *widget_data) {
    const ToggleWidget_t *result = widget_data;

    // Update each text's offset_x using the current (post-resize) text height and
    // reposition before reading end_x, so final_text_width is based on correct positions.
    for ( size_t i = 0; i < result->text_drawables->size; i++ ) {
        Drawable_t *text = result->text_drawables->data[i];
        if ( i > 0 )
            text->layout.offset_x = text->bounds.h * 1.5;
        ui_reposition_drawable(text);
    }

    const double start_x = result->d_anchor->bounds.x;
    double end_x = 0, end_w = 0;
    Drawable_t *prev = result->d_anchor;
    for ( size_t i = 0; i < result->text_drawables->size; i++ ) {
        prev = result->text_drawables->data[i];
        end_x = prev->bounds.x;
        end_w = prev->bounds.w;
    }

    const double padding = prev->bounds.h;
    const double final_text_width = end_x + end_w - start_x;
    const double final_width = final_text_width + padding * 2;
    const double extra_height = prev->bounds.h * 0.5;
    result->d_anchor->bounds.w = final_width;
    result->d_anchor->bounds.h = prev->bounds.h + extra_height;
    ui_reposition_drawable(result->d_anchor);

    const Layout_t bg_layout = {
        .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATIVE_TO_HEIGHT | LAYOUT_PROPORTIONAL_H,
        .height = 1.5,
        .width = final_text_width,
        .offset_x = -padding / 2.0,
        .offset_y = -extra_height / 2.0,
        .padding_w = padding,
        .relative_to = result->d_anchor,
        .relative_to_size = prev,
    };
    result->d_background->layout = bg_layout;
    ui_reposition_drawable(result->d_background);

    if ( result->active_index >= (int32_t)result->text_drawables->size )
        error_abort("toggle_widget_reconfigure: active index is out of bounds");
    const Drawable_t *active_opt = result->text_drawables->data[result->active_index];
    const Layout_t fg_layout = {
        .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATIVE_TO_SIZE,
        .width = 1.0,
        .height = 1.5,
        .offset_x = -padding / 2.0,
        .offset_y = -extra_height / 2.0,
        .padding_w = padding,
        .relative_to = active_opt,
        .relative_to_size = active_opt,
    };
    result->d_foreground->layout = fg_layout;
    ui_reposition_drawable(result->d_foreground);
}

static void toggle_widget_destroy(Ui_t *ui, void *widget_data) {
    ToggleWidget_t *widget = widget_data;
    ui_destroy_toggle_widget(ui, widget);
}

static void toggle_widget_on_click(const UiEventOpts_t *opts, Drawable_t *target, void *custom_data) {
    ToggleWidget_t *widget = custom_data;
    if ( !widget->editable )
        return;

    int index = -1;
    for ( size_t i = 0; i < widget->text_drawables->size; i++ ) {
        const Drawable_t *text = widget->text_drawables->data[i];
        if ( text == target ) {
            index = (int32_t)i;
            break;
        }
    }

    if ( index < 0 )
        return;

    if ( widget->on_change_callback != NULL ) {
        widget->on_change_callback(opts->ui, widget, index);
    }
    widget->active_index = index;
    toggle_widget_reconfigure(widget);
}

int ui_register_widget(const Container_t *parent, const c_reconfigure_widget reconfigure_callback,
                       const c_destroy_widget destroy_callback, void *widget_data) {
    if ( destroy_callback == NULL )
        error_abort("ui_register_widget: Every widget needs to register a destroy callback");

    int id = 0;
    const size_t size = parent->widgets->size;
    if ( size > 0 ) {
        const WidgetEntry_t *last = parent->widgets->data[size - 1];
        id = last->id + 1;
    }

    WidgetEntry_t *cb = calloc(1, sizeof(*cb));
    cb->configure_callback = reconfigure_callback;
    cb->destroy_callback = destroy_callback;
    cb->widget_data = widget_data;
    cb->id = id;

    vec_add(parent->widgets, cb);

    return id;
}

void ui_unregister_widget(const Container_t *parent, int id) {
    for ( size_t i = 0; i < parent->widgets->size; i++ ) {
        WidgetEntry_t *widget = parent->widgets->data[i];
        if ( widget->id == id ) {
            vec_remove(parent->widgets, i);
            free(widget);
            return;
        }
    }

    printf("Warning: ui_unregister_widget: Id not found\n");
}

ToggleWidget_t *ui_build_toggle_widget(Ui_t *ui, Container_t *parent, const Layout_t *layout, const ToggleWidgetOpts_t *opts) {
    if ( opts->num_opts <= 0 )
        error_abort("ui_build_toggle_widget: empty opts");

    ToggleWidget_t *widget = calloc(1, sizeof(*widget));

    widget->parent = parent;
    widget->d_anchor = ui_make_custom(ui, parent, layout);
    widget->d_anchor->enabled = false;
    widget->editable = true;
    widget->active_index = opts->active_index;
    widget->text_drawables = vec_init();

    Drawable_t *prev = widget->d_anchor;
    for ( int i = 0; i < opts->num_opts; i++ ) {
        const char *text = opts->opts[i];
        const Drawable_TextData_t data = {
            .color = opts->text_color,
            .em = opts->text_em,
            .text = (char *)text,
        };
        const int add_flag = prev == widget->d_anchor ? 0 : LAYOUT_RELATION_X_INCLUDE_WIDTH;
        const Layout_t text_layout = {.flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATIVE_TO_SIZE | add_flag,
                                      .relative_to = prev,
                                      .offset_x = i > 0 ? prev->bounds.h * 1.5 : 0,
                                      .z_index = 1};

        prev = ui_make_text(ui, &data, parent, &text_layout);
        vec_add(widget->text_drawables, prev);
        // TODO: Maybe create a similar rectangle but hidden for every text and use that as the event callback target
        ui_add_event_callback(ui, UI_EVENT_MOUSE_CLICK, prev, toggle_widget_on_click, widget);
    }

    const Drawable_RectangleData_t bg_data = {.color = opts->background_color, .border_radius_em = BORDER_RADIUS_AUTO};
    widget->d_background = ui_make_rectangle(ui, &bg_data, parent, &(Layout_t){});

    if ( opts->active_index > (int32_t)widget->text_drawables->size - 1 || opts->active_index < 0 )
        error_abort("ui_build_toggle_widget: active_index is off bounds");

    const Drawable_RectangleData_t fg_data = {.color = opts->active_color, .border_radius_em = BORDER_RADIUS_AUTO};
    widget->d_foreground = ui_make_rectangle(ui, &fg_data, parent, &(Layout_t){});

    toggle_widget_reconfigure(widget);
    widget->entry_id = ui_register_widget(parent, toggle_widget_reconfigure, toggle_widget_destroy, widget);

    return widget;
}

void ui_destroy_toggle_widget(Ui_t *ui, ToggleWidget_t *widget) {
    ui_destroy_drawable(ui, widget->d_anchor);
    ui_destroy_drawable(ui, widget->d_background);
    ui_destroy_drawable(ui, widget->d_foreground);

    for ( size_t i = 0; i < widget->text_drawables->size; i++ ) {
        Drawable_t *drawable = widget->text_drawables->data[i];
        ui_destroy_drawable(ui, drawable);
    }
    vec_destroy(widget->text_drawables);

    ui_unregister_widget(widget->parent, widget->entry_id);
    free(widget);
}

static void button_widget_destroy(Ui_t *ui, void *widget_data) {
    ButtonWidget_t *widget = widget_data;
    ui_destroy_button_widget(ui, widget);
}

static void button_widget_mouse_event(const UiEventOpts_t *opts, Drawable_t *bg_drawable, void *widget_data) {
    const ButtonWidget_t *widget = widget_data;

    if ( opts->event == UI_EVENT_MOUSE_HOVER_ENTERED ) {
        if ( widget->bg_show_type == BUTTON_BG_SHOW_ON_HOVER )
            ui_drawable_set_alpha(bg_drawable, widget->background_alpha);
        if ( widget->on_hover_callback != NULL )
            widget->on_hover_callback(opts->ui, widget, UI_EVENT_MOUSE_HOVER_ENTERED);
    } else if ( opts->event == UI_EVENT_MOUSE_HOVER_EXITED ) {
        if ( widget->bg_show_type == BUTTON_BG_SHOW_ON_HOVER )
            ui_drawable_set_alpha(bg_drawable, 0);
        if ( widget->on_hover_callback != NULL )
            widget->on_hover_callback(opts->ui, widget, UI_EVENT_MOUSE_HOVER_EXITED);
    }

    if ( opts->event == UI_EVENT_MOUSE_CLICK && widget->active )
        if ( widget->on_click_callback != NULL )
            widget->on_click_callback(opts->ui, widget);
}

ButtonWidget_t *ui_build_button_widget(Ui_t *ui, Container_t *parent, const Layout_t *layout, const ButtonWidgetOpts_t *opts) {
    ButtonWidget_t *widget = calloc(1, sizeof(*widget));
    widget->parent = parent;
    widget->active = true;
    widget->bg_show_type = opts->bg_show_type;
    widget->background_alpha = opts->bg_color.a;

    const Layout_t content_layout = {
        .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_CENTER,
        .z_index = 1,
    };
    Drawable_t *d_content;
    if ( opts->content_type == BUTTON_CONTENT_IMAGE ) {
        const Drawable_ImageData_t image_data = {.border_radius_em = 0.0, .draw_shadow = false};
        const int size_flags = LAYOUT_RELATIVE_TO_SIZE | LAYOUT_PROPORTIONAL_SIZE | LAYOUT_RELATION_INCLUDE_SIZE;
        Layout_t image_layout = content_layout;
        if ( layout->width != 0 || layout->height != 0 ) {
            image_layout.flags |= (layout->flags & size_flags) | LAYOUT_SPECIAL_KEEP_ASPECT_RATIO;
            image_layout.width = layout->width;
            image_layout.height = layout->height;
        }
        widget->d_image = ui_make_image(opts->image_bytes, opts->image_length, &image_data, parent, &image_layout);
        d_content = widget->d_image;
    } else {
        const Drawable_TextData_t text_data = {
            .text = (char *)opts->text,
            .em = opts->text_em,
            .color = opts->text_color,
        };
        widget->d_text = ui_make_text(ui, &text_data, parent, &content_layout);
        ui_drawable_set_alpha_immediate(widget->d_text, opts->text_color.a);
        d_content = widget->d_text;
    }

    const Drawable_RectangleData_t rect_data = {.border_radius_em = BORDER_RADIUS_AUTO, .color = opts->bg_color};
    Layout_t rect_layout = *layout;
    rect_layout.flags |= LAYOUT_RELATIVE_TO_SIZE;
    rect_layout.relative_to_size = d_content;
    rect_layout.width = 1.5;
    rect_layout.height = 1.5;

    widget->d_background = ui_make_rectangle(ui, &rect_data, parent, &rect_layout);
    d_content->layout.relative_to = widget->d_background;
    d_content->layout.flags |= LAYOUT_PROPORTIONAL_POS_TO_RELATIVE;
    ui_reposition_drawable(d_content);

    widget->entry_id = ui_register_widget(parent, NULL, button_widget_destroy, widget);

    if ( opts->bg_show_type != BUTTON_BG_SHOW_ALWAYS ) {
        ui_drawable_set_alpha_immediate(widget->d_background, 0);
        const Animation_FadeInOutData_t data = {.duration = 0.3, .ease_func = ANIM_EASE_NONE};
        ui_animate_fade(widget->d_background, &data);
    }

    ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_ENTERED, widget->d_background, button_widget_mouse_event, widget);
    ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_EXITED, widget->d_background, button_widget_mouse_event, widget);
    ui_add_event_callback(ui, UI_EVENT_MOUSE_CLICK, widget->d_background, button_widget_mouse_event, widget);

    return widget;
}

void ui_destroy_button_widget(Ui_t *ui, ButtonWidget_t *widget) {
    if ( widget->d_text )
        ui_destroy_drawable(ui, widget->d_text);
    if ( widget->d_image )
        ui_destroy_drawable(ui, widget->d_image);
    ui_destroy_drawable(ui, widget->d_background);
    ui_unregister_widget(widget->parent, widget->entry_id);
    free(widget);
}

void ui_widget_button_enabled(const ButtonWidget_t *widget, const bool enabled) {
    widget->d_background->enabled = enabled;
    if ( widget->d_text )
        widget->d_text->enabled = enabled;
    if ( widget->d_image )
        widget->d_image->enabled = enabled;
}

void ui_widget_button_set_image(const ButtonWidget_t *widget, const unsigned char *bytes, const int length) {
    if ( widget->d_image == NULL )
        error_abort("ui_widget_button_set_image: button was not built with BUTTON_CONTENT_IMAGE");
    ui_drawable_set_image(widget->d_image, bytes, length);
}

static void progress_bar_reconfigure(void *widget_data) {
    const ProgressBarWidget_t *widget = widget_data;

    if ( widget->d_handle != NULL ) {
        widget->d_handle->layout.width = widget->d_fg->bounds.h * widget->handle_relative_size;
        widget->d_handle->layout.offset_x = widget->d_fg->layout.width;
        ui_reposition_drawable(widget->d_handle);
    }
}

static void progress_bar_destroy(Ui_t *ui, void *widget) { ui_destroy_progress_bar_widget(ui, widget); }

static void progress_bar_widget_event(const UiEventOpts_t *opts, Drawable_t *, void *custom_data) {
    ProgressBarWidget_t *widget = custom_data;

    if ( opts->event == UI_EVENT_MOUSE_HOVER_ENTERED && widget->d_handle != NULL ) {
        ui_drawable_set_alpha(widget->d_handle, widget->fg_color.a);
    }
    if ( opts->event == UI_EVENT_MOUSE_HOVER_EXITED && widget->d_handle != NULL ) {
        ui_drawable_set_alpha(widget->d_handle, 0);
    }

    const bool changed = opts->event == UI_EVENT_MOUSE_CLICK || opts->event == UI_EVENT_MOUSE_DRAG;
    if ( widget->editable && changed ) {
        double progress_bar_x;
        ui_get_drawable_canon_pos(widget->d_bg, &progress_bar_x, NULL);
        const double distance_from_x = opts->mouse.x - progress_bar_x;
        const double progress = MAX(0.0, MIN(1.0, distance_from_x / widget->d_bg->bounds.w));

        if ( progress != widget->d_fg->layout.width ) {
            ui_widget_progress_bar_progress(widget, progress);
            if ( widget->on_change_callback != NULL )
                widget->on_change_callback(opts->ui, widget, progress);
        }
    }
}

ProgressBarWidget_t *ui_build_progress_bar_widget(Ui_t *ui, Container_t *parent, const Layout_t *layout,
                                                  const ProgressBarWidgetOpts_t *opts) {
    ProgressBarWidget_t *widget = calloc(1, sizeof(*widget));
    widget->parent = parent;
    widget->bg_color = opts->bg_color;
    widget->fg_color = opts->fg_color;
    widget->editable = opts->user_editable;

    const Drawable_RectangleData_t bg_data = {.border_radius_em = opts->border_radius, .color = opts->bg_color};
    widget->d_bg = ui_make_rectangle(ui, &bg_data, parent, layout);

    const double progress = MAX(0.0, MIN(1.0, opts->initial_progress));
    const Drawable_RectangleData_t fg_data = {.border_radius_em = opts->border_radius, .color = opts->fg_color};
    const Layout_t fg_layout = {.flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATIVE_TO_SIZE,
                                .height = 1.0,
                                .width = progress,
                                .relative_to = widget->d_bg,
                                .relative_to_size = widget->d_bg};
    widget->d_fg = ui_make_rectangle(ui, &fg_data, parent, &fg_layout);

    if ( opts->handle_type != PROG_BAR_HANDLE_NONE ) {
        widget->handle_relative_size = opts->handle_relative_size <= 0 ? 1.5 : opts->handle_relative_size;
        Color_t handle_color = opts->fg_color;
        handle_color.a = 0xFF;

        const Drawable_RectangleData_t handle_data = {.border_radius_em = BORDER_RADIUS_AUTO, .color = handle_color};
        const Layout_t handle_layout = {.flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATIVE_TO_HEIGHT | LAYOUT_PROPORTIONAL_POS |
                                                 LAYOUT_PROPORTIONAL_POS_TO_RELATIVE | LAYOUT_ANCHOR_CENTER_X |
                                                 LAYOUT_ANCHOR_CENTER_Y,
                                        .height = widget->handle_relative_size,
                                        .offset_y = 0.5,
                                        .offset_x = progress,
                                        .width = widget->d_fg->bounds.h * widget->handle_relative_size,
                                        .relative_to = widget->d_bg,
                                        .relative_to_size = widget->d_bg};
        widget->d_handle = ui_make_rectangle(ui, &handle_data, parent, &handle_layout);
    }

    if ( opts->user_editable ) {
        ui_add_event_callback(ui, UI_EVENT_MOUSE_CLICK, widget->d_bg, progress_bar_widget_event, widget);

        if ( opts->draggable )
            ui_add_event_callback(ui, UI_EVENT_MOUSE_DRAG, widget->d_bg, progress_bar_widget_event, widget);

        if ( opts->handle_type == PROG_BAR_HANDLE_SHOW_ON_HOVER ) {
            ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_ENTERED, widget->d_bg, progress_bar_widget_event, widget);
            ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_EXITED, widget->d_bg, progress_bar_widget_event, widget);
            ui_animate_fade(widget->d_handle, &(Animation_FadeInOutData_t){.duration = 0.3});
            ui_drawable_set_alpha_immediate(widget->d_handle, 0);
        }
    }

    widget->entry_id = ui_register_widget(parent, progress_bar_reconfigure, progress_bar_destroy, widget);
    return widget;
}

void ui_destroy_progress_bar_widget(Ui_t *ui, ProgressBarWidget_t *widget) {
    ui_destroy_drawable(ui, widget->d_bg);
    ui_destroy_drawable(ui, widget->d_fg);
    if ( widget->d_handle != NULL )
        ui_destroy_drawable(ui, widget->d_handle);

    ui_unregister_widget(widget->parent, widget->entry_id);
    free(widget);
}

void ui_widget_progress_bar_progress(ProgressBarWidget_t *widget, const double progress) {
    if ( progress != widget->d_fg->layout.width ) {
        widget->d_fg->layout.width = progress;
        ui_reposition_drawable(widget->d_fg);
        progress_bar_reconfigure(widget);
    }
}
