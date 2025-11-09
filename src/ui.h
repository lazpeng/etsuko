/**
 * ui.h - Takes care of drawing stuff to the screen, managing the scene graph, computing layouts and rendering effects.
 * Defines primitives and basic functions that can be reused as building blocks for making up the UI.
 */

#ifndef ETSUKO_UI_H
#define ETSUKO_UI_H

#include <stdint.h>

#include "container_utils.h"
#include "renderer.h"

// Fw declarations
typedef struct Ui_t Ui_t;
typedef struct Drawable_t Drawable_t;

// Primitives
typedef enum LayoutFlags_t {
    LAYOUT_NONE = 0,
    LAYOUT_CENTER_X = 1 << 0,
    LAYOUT_CENTER_Y = 1 << 1,
    LAYOUT_CENTER = LAYOUT_CENTER_X | LAYOUT_CENTER_Y,
    LAYOUT_PROPORTIONAL_X = 1 << 2,
    LAYOUT_PROPORTIONAL_Y = 1 << 3,
    LAYOUT_PROPORTIONAL_POS = LAYOUT_PROPORTIONAL_X | LAYOUT_PROPORTIONAL_Y,
    LAYOUT_PROPORTIONAL_W = 1 << 4,
    LAYOUT_PROPORTIONAL_H = 1 << 5,
    LAYOUT_PROPORTIONAL_SIZE = LAYOUT_PROPORTIONAL_W | LAYOUT_PROPORTIONAL_H,
    LAYOUT_ANCHOR_BOTTOM_Y = 1 << 6,
    LAYOUT_ANCHOR_RIGHT_X = 1 << 7,
    LAYOUT_SPECIAL_KEEP_ASPECT_RATIO = 1 << 8,
    LAYOUT_RELATION_Y_INCLUDE_HEIGHT = 1 << 9,
    LAYOUT_RELATION_X_INCLUDE_WIDTH = 1 << 10,
    LAYOUT_RELATION_INCLUDE_SIZE = LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_RELATION_X_INCLUDE_WIDTH,
    LAYOUT_RELATIVE_TO_Y = 1 << 11,
    LAYOUT_RELATIVE_TO_X = 1 << 12,
    LAYOUT_RELATIVE_TO_POS = LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATIVE_TO_X,
    LAYOUT_RELATIVE_TO_HEIGHT = 1 << 13,
    LAYOUT_RELATIVE_TO_WIDTH = 1 << 14,
    LAYOUT_RELATIVE_TO_SIZE = LAYOUT_RELATIVE_TO_HEIGHT | LAYOUT_RELATIVE_TO_WIDTH,
    LAYOUT_WRAP_AROUND_X = 1 << 15,
    LAYOUT_WRAP_AROUND_Y = 1 << 16,
    LAYOUT_WRAP_AROUND = LAYOUT_WRAP_AROUND_X | LAYOUT_WRAP_AROUND_Y,
} LayoutFlags_t;

typedef struct Layout_t {
    LayoutFlags_t flags;
    double offset_x, offset_y;
    double aspect_ratio_size;
    double width, height;
    Drawable_t *relative_to_size;
    Drawable_t *relative_to;
} Layout_t;

typedef enum DrawableType_t { DRAW_TYPE_TEXT = 0, DRAW_TYPE_IMAGE, DRAW_TYPE_PROGRESS_BAR } DrawableType_t;

typedef enum ContainerFlags_t {
    CONTAINER_NONE = 0,
    CONTAINER_VERTICAL_ALIGN_CONTENT = 1,
} ContainerFlags_t;

typedef struct Container_t {
    Bounds_t bounds;
    struct Container_t *parent;
    Vector_t *child_drawables;
    Vector_t *child_containers;
    Layout_t layout;
    bool enabled;
    ContainerFlags_t flags;
    double align_content_offset_y;
    double viewport_y;
} Container_t;

typedef struct Drawable_t {
    DrawableType_t type;
    Texture_t *texture;
    Bounds_t bounds;
    void *custom_data;
    Container_t *parent;
    bool enabled, dynamic;
    Layout_t layout;
    uint8_t alpha_mod;
    Vector_t *animations;
} Drawable_t;

typedef enum AnimationType_t {
    ANIM_EASE_TRANSLATION = 0,
    ANIM_FADE_IN_OUT,
} AnimationType_t;

typedef struct Animation_t {
    double duration, elapsed;
    AnimationType_t type;
    void *custom_data;
    Drawable_t *target;
    bool active;
} Animation_t;

// Options and custom data
typedef enum DrawableAlignment_t { ALIGN_LEFT = 0, ALIGN_CENTER, ALIGN_RIGHT } DrawableAlignment_t;

typedef struct Drawable_TextData_t {
    // Regular options
    char *text;
    FontType_t font_type;
    double em;
    Color_t color;
    bool bold;
    // Wrap options
    int wrap_enabled;
    double wrap_width_threshold;
    double measure_at_em;
    int32_t line_padding;
    DrawableAlignment_t alignment;
} Drawable_TextData_t;

typedef struct Drawable_ImageData_t {
    char *file_path;
    double border_radius_em;
} Drawable_ImageData_t;

typedef struct Drawable_ProgressBarData_t {
    double progress;
    double border_radius_em;
    Color_t fg_color, bg_color;
} Drawable_ProgressBarData_t;

typedef struct Animation_EaseTranslationData_t {
    double from_x, from_y;
    double to_x, to_y;
    double duration;
    bool ease;
} Animation_EaseTranslationData_t;

typedef struct Animation_FadeInOutData_t {
    int32_t from_alpha, to_alpha;
    double duration;
} Animation_FadeInOutData_t;

// Init and lifetime functions
Ui_t *ui_init();
void ui_finish(Ui_t *ui);
void ui_begin_loop(Ui_t *ui);
void ui_end_loop();
void ui_load_font(FontType_t type, const char *path);
void ui_draw(const Ui_t *ui);
// Meta helpers
void ui_set_window_title(const char *title);
void ui_set_bg_color(uint32_t color);
void ui_set_bg_gradient(uint32_t primary, uint32_t secondary, BackgroundType_t type);
void ui_on_window_changed(Ui_t *ui);
Container_t *ui_root_container(Ui_t *ui);
void ui_get_drawable_canon_pos(Ui_t *ui, const Drawable_t *drawable, double *x, double *y);
void ui_get_container_canon_pos(Ui_t *ui, const Container_t *container, double *x, double *y);
// Drawables
Drawable_t *ui_make_text(Ui_t *ui, Drawable_TextData_t *data, Container_t *container, const Layout_t *layout);
Drawable_t *ui_make_image(Ui_t *ui, Drawable_ImageData_t *data, Container_t *container, const Layout_t *layout);
Drawable_t *ui_make_progressbar(Ui_t *ui, const Drawable_ProgressBarData_t *data, Container_t *container, const Layout_t *layout);
void ui_recompute_drawable(Ui_t *ui, Drawable_t *drawable);
void ui_reposition_drawable(Ui_t *ui, Drawable_t *drawable);
void ui_destroy_drawable(Drawable_t *drawable);
void ui_drawable_set_alpha(Drawable_t *drawable, int32_t alpha);
// Containers
Container_t *ui_make_container(Ui_t *ui, Container_t *parent, const Layout_t *layout, ContainerFlags_t flags);
void ui_recompute_container(Ui_t *ui, Container_t *container);
void ui_destroy_container(Ui_t *ui, Container_t *container);
// Animations
void ui_animate_translation(Drawable_t *target, const Animation_EaseTranslationData_t *data);
void ui_animate_fade(Drawable_t *target, const Animation_FadeInOutData_t *data);

#endif // ETSUKO_UI_H
