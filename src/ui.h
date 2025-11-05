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
typedef struct etsuko_Drawable_t etsuko_Drawable_t;

// Primitives
typedef enum etsuko_LayoutFlags_t {
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
} etsuko_LayoutFlags_t;

typedef struct etsuko_Layout_t {
    etsuko_LayoutFlags_t flags;
    double offset_x, offset_y;
    double width, height;
    etsuko_Drawable_t *relative_to_size;
    etsuko_Drawable_t *relative_to;
} etsuko_Layout_t;

typedef enum etsuko_DrawableType_t { DRAW_TYPE_TEXT = 0, DRAW_TYPE_IMAGE, DRAW_TYPE_PROGRESS_BAR } etsuko_DrawableType_t;

typedef enum etsuko_ContainerFlags_t {
    CONTAINER_NONE = 0,
    CONTAINER_VERTICAL_ALIGN_CONTENT = 1,
} etsuko_ContainerFlags_t;

typedef struct etsuko_Container_t {
    etsuko_Bounds_t bounds;
    struct etsuko_Container_t *parent;
    Vector_t *child_drawables;
    Vector_t *child_containers;
    etsuko_Layout_t layout;
    bool enabled;
    etsuko_ContainerFlags_t flags;
    double align_content_offset_y;
    double viewport_y;
} etsuko_Container_t;

typedef struct etsuko_Drawable_t {
    etsuko_DrawableType_t type;
    etsuko_Texture_t *texture;
    etsuko_Bounds_t bounds;
    void *custom_data;
    etsuko_Container_t *parent;
    bool enabled, dynamic;
    etsuko_Layout_t layout;
    uint8_t alpha_mod;
    Vector_t *animations;
} etsuko_Drawable_t;

typedef enum etsuko_AnimationType_t {
    ANIM_EASE_TRANSLATION = 0,
    ANIM_FADE_IN_OUT,
} etsuko_AnimationType_t;

typedef struct etsuko_Animation_t {
    double duration, elapsed;
    etsuko_AnimationType_t type;
    void *custom_data;
    etsuko_Drawable_t *target;
    bool active;
} etsuko_Animation_t;

// Options and custom data
typedef enum etsuko_DrawableAlignment_t { ALIGN_LEFT = 0, ALIGN_CENTER, ALIGN_RIGHT } etsuko_DrawableAlignment_t;

typedef struct etsuko_Drawable_TextData_t {
    // Regular options
    char *text;
    etsuko_FontType_t font_type;
    double em;
    etsuko_Color_t color;
    bool bold;
    // Wrap options
    int wrap_enabled;
    double wrap_width_threshold;
    double measure_at_em;
    int32_t line_padding;
    etsuko_DrawableAlignment_t alignment;
} etsuko_Drawable_TextData_t;

typedef struct etsuko_Drawable_ImageData_t {
    char *file_path;
    double border_radius_em;
} etsuko_Drawable_ImageData_t;

typedef struct etsuko_Drawable_ProgressBarData_t {
    double progress;
    double border_radius_em;
    etsuko_Color_t fg_color, bg_color;
} etsuko_Drawable_ProgressBarData_t;

typedef struct etsuko_Animation_EaseTranslationData_t {
    double from_x, from_y;
    double to_x, to_y;
    double duration;
    bool ease;
} etsuko_Animation_EaseTranslationData_t;

typedef struct etsuko_Animation_FadeInOutData_t {
    int32_t from_alpha, to_alpha;
    double duration;
} etsuko_Animation_FadeInOutData_t;

// Init and lifetime functions
int ui_init();
void ui_finish();
void ui_begin_loop(double delta_time);
void ui_end_loop();
void ui_load_font(etsuko_FontType_t type, const char *path);
void ui_draw();
// Meta helpers
void ui_set_window_title(const char *title);
void ui_set_bg_color(uint32_t color);
void ui_on_window_changed();
etsuko_Container_t *ui_root_container();
void ui_get_drawable_canon_pos(const etsuko_Drawable_t *drawable, double *x, double *y);
void ui_get_container_canon_pos(const etsuko_Container_t *container, double *x, double *y);
// Drawables
etsuko_Drawable_t *ui_make_text(etsuko_Drawable_TextData_t *data, etsuko_Container_t *container, const etsuko_Layout_t *layout);
etsuko_Drawable_t *ui_make_image(etsuko_Drawable_ImageData_t *data, etsuko_Container_t *container, const etsuko_Layout_t *layout);
etsuko_Drawable_t *ui_make_progressbar(const etsuko_Drawable_ProgressBarData_t *data, etsuko_Container_t *container,
                                       const etsuko_Layout_t *layout);
void ui_recompute_drawable(etsuko_Drawable_t *drawable);
void ui_reposition_drawable(etsuko_Drawable_t *drawable);
void ui_destroy_drawable(etsuko_Drawable_t *drawable);
void ui_drawable_set_alpha(etsuko_Drawable_t *drawable, int32_t alpha);
// Containers
etsuko_Container_t *ui_make_container(etsuko_Container_t *parent, const etsuko_Layout_t *layout, etsuko_ContainerFlags_t flags);
void ui_recompute_container(etsuko_Container_t *container);
void ui_destroy_container(etsuko_Container_t *container);
// Animations
void ui_animate_translation(etsuko_Drawable_t *target, const etsuko_Animation_EaseTranslationData_t *data);
void ui_animate_fade(etsuko_Drawable_t *target, const etsuko_Animation_FadeInOutData_t *data);

#endif // ETSUKO_UI_H
