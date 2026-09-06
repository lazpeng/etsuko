#include "ui_widgets.h"

#include "error.h"

#include <stdio.h>

static void reposition_toggle_widget_text(const ToggleWidget_t *widget) {
    for ( size_t i = 0; i < widget->text_drawables->size; i++ ) {
        Drawable_t *text = widget->text_drawables->data[i];
        if ( i > 0 )
            text->layout.offset_x = text->bounds.h;
        ui_reposition_drawable(text);
        if ( i < widget->option_hitboxes->size ) {
            const double height = text->bounds.h;
            const double padding = height * 0.5;
            Drawable_t *hitbox = widget->option_hitboxes->data[i];
            hitbox->layout.offset_y = -padding / 2.0;
            hitbox->layout.offset_x = -padding;
            hitbox->layout.padding_w = height;
            ui_reposition_drawable(hitbox);
        }
    }
}

static void toggle_widget_reconfigure(void *widget_data) {
    const ToggleWidget_t *result = widget_data;

    reposition_toggle_widget_text(result);

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
    const double extra_height = prev->bounds.h * 0.5;
    result->d_anchor->bounds.w = final_text_width;
    result->d_anchor->bounds.h = prev->bounds.h + extra_height;
    ui_reposition_drawable(result->d_anchor);

    // Reposition the text AGAIN since they depend on the anchor
    reposition_toggle_widget_text(result);

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
    for ( size_t i = 0; i < widget->option_hitboxes->size; i++ ) {
        const Drawable_t *hitbox = widget->option_hitboxes->data[i];
        if ( hitbox == target ) {
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

void ui_unregister_widget(const Container_t *parent, const int id) {
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
    widget->option_hitboxes = vec_init();

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

        const Layout_t hitbox_layout = {
            .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATIVE_TO_SIZE,
            .relative_to = prev,
            .relative_to_size = prev,
            .height = 1.5,
            .width = 1.0,
        };
        const Drawable_RectangleData_t hitbox_data = {
            .border_radius_em = BORDER_RADIUS_AUTO,
            .color = {.r = 0, .g = 0, .b = 0, .a = 0},
        };
        Drawable_t *hitbox = ui_make_rectangle(ui, &hitbox_data, parent, &hitbox_layout);
        vec_add(widget->option_hitboxes, hitbox);
        ui_add_event_callback(ui, UI_EVENT_MOUSE_CLICK, hitbox, toggle_widget_on_click, widget);
    }

    const Drawable_RectangleData_t bg_data = {.color = opts->background_color, .border_radius_em = BORDER_RADIUS_AUTO};
    widget->d_background = ui_make_rectangle(ui, &bg_data, parent, &(Layout_t){});

    if ( opts->active_index > (int32_t)widget->text_drawables->size - 1 || opts->active_index < 0 )
        error_abort("ui_build_toggle_widget: active_index is off bounds");

    const Drawable_RectangleData_t fg_data = {.color = opts->active_color, .border_radius_em = BORDER_RADIUS_AUTO};
    widget->d_foreground = ui_make_rectangle(ui, &fg_data, parent, &(Layout_t){});

    toggle_widget_reconfigure(widget);
    widget->entry_id = ui_register_widget(parent, toggle_widget_reconfigure, toggle_widget_destroy, widget);

    // Add animation after the first reconfigure so it doesn't animate the initial positioning
    const Animation_EaseTranslationData_t fg_translation_data = {.duration = 0.1, .ease_func = ANIM_EASE_OUT_CUBIC};
    ui_animate_translation(widget->d_foreground, &fg_translation_data);

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

    for ( size_t i = 0; i < widget->option_hitboxes->size; i++ ) {
        Drawable_t *drawable = widget->option_hitboxes->data[i];
        ui_destroy_drawable(ui, drawable);
    }
    vec_destroy(widget->option_hitboxes);

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

void ui_widget_toggle_enabled(const ToggleWidget_t *widget, const bool enabled) {
    widget->d_background->enabled = enabled;
    widget->d_foreground->enabled = enabled;
    for ( size_t i = 0; i < widget->text_drawables->size; i++ ) {
        Drawable_t *text = widget->text_drawables->data[i];
        text->enabled = enabled;
    }
    for ( size_t i = 0; i < widget->option_hitboxes->size; i++ ) {
        Drawable_t *option_hitbox = widget->option_hitboxes->data[i];
        option_hitbox->enabled = enabled;
    }
}

void ui_widget_toggle_selected(ToggleWidget_t *widget, const int selected) {
    if ( selected == widget->active_index )
        return;

    if ( selected >= (int32_t)widget->option_hitboxes->size )
        error_abort("Error: ui_widget_toggle_selected: selected index is larger than number of options");

    widget->active_index = selected;
    toggle_widget_reconfigure(widget);
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