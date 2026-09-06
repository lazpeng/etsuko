/**
 * ui_widgets.h - Parts of the UI that relate to widgets, which are a fancy way of grouping multiple primitive drawables
 * together and coordinating behavior and changes between all of them.
 */

#ifndef ETSUKO_UI_WIDGETS_H
#define ETSUKO_UI_WIDGETS_H

#include "ui.h"

typedef struct WidgetEntry_t {
    c_reconfigure_widget configure_callback;
    c_destroy_widget destroy_callback;
    int id;
    void *widget_data;
} WidgetEntry_t;

struct ToggleWidget_t;
typedef void (*c_widget_toggle_on_change)(Ui_t *ui, const struct ToggleWidget_t *widget, int selected_opt);

typedef struct ToggleWidget_t {
    int active_index;
    OWNING Vector_t *text_drawables;  // of Drawable_t*
    OWNING Vector_t *option_hitboxes; // of Drawable_t*
    OWNING Drawable_t *d_anchor, *d_background, *d_foreground;
    bool editable;
    int entry_id;
    WEAK Container_t *parent;
    WEAK c_widget_toggle_on_change on_change_callback;
    WEAK void *custom_data;
} ToggleWidget_t;

typedef struct ToggleWidgetOpts_t {
    WEAK const char **const opts;
    int num_opts;
    double text_em;
    int active_index;
    Color_t text_color, active_color, background_color;
} ToggleWidgetOpts_t;

struct ButtonWidget_t;
typedef void (*c_widget_button_on_click)(Ui_t *ui, const struct ButtonWidget_t *widget);
typedef void (*c_widget_button_on_hover)(Ui_t *ui, const struct ButtonWidget_t *widget, UiEvent_t event);

typedef enum ButtonWidgetBackgroundShowType_t {
    BUTTON_BG_DISABLED = 0,
    BUTTON_BG_SHOW_ALWAYS,
    BUTTON_BG_SHOW_ON_HOVER,
} ButtonWidgetBackgroundShowType_t;

typedef enum ButtonWidgetContentType_t {
    BUTTON_CONTENT_TEXT = 0,
    BUTTON_CONTENT_IMAGE,
} ButtonWidgetContentType_t;

typedef struct ButtonWidget_t {
    bool active;
    OWNING Drawable_t *d_text;
    OWNING Drawable_t *d_image;
    OWNING Drawable_t *d_background;
    WEAK Container_t *parent;
    int entry_id;
    WEAK c_widget_button_on_click on_click_callback;
    WEAK c_widget_button_on_hover on_hover_callback;
    ButtonWidgetBackgroundShowType_t bg_show_type;
    int background_alpha;
    WEAK void *custom_data;
} ButtonWidget_t;

typedef struct ButtonWidgetOpts_t {
    ButtonWidgetContentType_t content_type;
    WEAK const char *text;
    double text_em;
    Color_t text_color;
    WEAK const unsigned char *image_bytes;
    int image_length;
    Color_t bg_color;
    ButtonWidgetBackgroundShowType_t bg_show_type;
} ButtonWidgetOpts_t;

struct ProgressBarWidget_t;
typedef void (*c_progress_bar_widget_on_change)(Ui_t *ui, const struct ProgressBarWidget_t *widget, double progress);

typedef struct ProgressBarWidget_t {
    OWNING Drawable_t *d_bg, *d_fg, *d_handle;
    WEAK Container_t *parent;
    Color_t bg_color, fg_color;
    bool editable;
    c_progress_bar_widget_on_change on_change_callback;
    int entry_id;
    double handle_relative_size;
} ProgressBarWidget_t;

typedef enum ProgressBarWidgetHandleType_t {
    PROG_BAR_HANDLE_NONE = 0,
    PROG_BAR_HANDLE_SHOW_ALWAYS,
    PROG_BAR_HANDLE_SHOW_ON_HOVER,
} ProgressBarWidgetHandleType_t;

typedef struct ProgressBarWidgetOpts_t {
    Color_t bg_color, fg_color;
    bool user_editable, draggable;
    ProgressBarWidgetHandleType_t handle_type;
    double handle_relative_size;
    double border_radius;
    double initial_progress;
} ProgressBarWidgetOpts_t;

int ui_register_widget(const Container_t *parent, c_reconfigure_widget reconfigure_callback, c_destroy_widget destroy_callback,
                       void *widget_data);
void ui_unregister_widget(const Container_t *parent, int id);
ToggleWidget_t *ui_build_toggle_widget(Ui_t *ui, Container_t *parent, const Layout_t *layout, const ToggleWidgetOpts_t *opts);
void ui_destroy_toggle_widget(Ui_t *ui, ToggleWidget_t *widget);
ButtonWidget_t *ui_build_button_widget(Ui_t *ui, Container_t *parent, const Layout_t *layout, const ButtonWidgetOpts_t *opts);
void ui_destroy_button_widget(Ui_t *ui, ButtonWidget_t *widget);
void ui_widget_button_enabled(const ButtonWidget_t *widget, bool enabled);
void ui_widget_toggle_enabled(const ToggleWidget_t *widget, bool enabled);
void ui_widget_toggle_selected(ToggleWidget_t *widget, int selected);
void ui_widget_button_set_image(const ButtonWidget_t *widget, const unsigned char *bytes, int length);
ProgressBarWidget_t *ui_build_progress_bar_widget(Ui_t *ui, Container_t *parent, const Layout_t *layout,
                                                  const ProgressBarWidgetOpts_t *opts);
void ui_destroy_progress_bar_widget(Ui_t *ui, ProgressBarWidget_t *widget);
void ui_widget_progress_bar_progress(ProgressBarWidget_t *widget, double progress);

#endif // ETSUKO_UI_WIDGETS_H
