/**
 * ui.h - Takes care of drawing stuff to the screen, managing the scene graph, computing layouts and rendering effects.
 * Defines primitives and basic functions that can be reused as building blocks for making up the UI.
 */

#ifndef ETSUKO_UI_H
#define ETSUKO_UI_H

#include <stdint.h>

#include "constants.h"
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
    double width, height;
    // TODO: Converge into only one?
    WEAK Drawable_t *relative_to_size;
    WEAK Drawable_t *relative_to;
} Layout_t;

typedef enum DrawableType_t {
    DRAW_TYPE_TEXT = 0,
    DRAW_TYPE_IMAGE,
    DRAW_TYPE_PROGRESS_BAR,
    DRAW_TYPE_RECTANGLE,
    DRAW_TYPE_CUSTOM_TEXTURE
} DrawableType_t;

typedef enum ContainerFlags_t {
    CONTAINER_NONE = 0,
    CONTAINER_VERTICAL_ALIGN_CONTENT = 1,
} ContainerFlags_t;

typedef struct Container_t {
    Bounds_t bounds;
    WEAK struct Container_t *parent;
    OWNING Vector_t *child_drawables;
    OWNING Vector_t *child_containers;
    Layout_t layout;
    bool enabled;
    ContainerFlags_t flags;
    double align_content_offset_y;
    double viewport_y;
} Container_t;

typedef struct Drawable_t {
    DrawableType_t type;
    OWNING Texture_t *texture;
    Bounds_t bounds;
    OWNING void *custom_data;
    WEAK Container_t *parent;
    bool enabled, dynamic;
    Layout_t layout;
    uint8_t alpha_mod;
    OWNING Vector_t *animations;
    float color_mod;
    OWNING Shadow_t *shadow;
    DrawRegionOptSet_t draw_regions;
    uint8_t underlay_alpha;
    bool draw_underlay;
    bool pending_recompute;
} Drawable_t;

typedef enum AnimationType_t {
    ANIM_EASE_TRANSLATION = 0,
    ANIM_FADE_IN_OUT,
    ANIM_SCALE,
    ANIM_DRAW_REGION,
} AnimationType_t;

typedef enum AnimationEaseType_t {
    ANIM_EASE_NONE = 0,
    ANIM_EASE_OUT_CUBIC,
    ANIM_EASE_OUT_SINE,
    ANIM_EASE_OUT_QUAD,
    ANIM_EASE_OUT_CIRC
} AnimationEaseType_t;

typedef struct Animation_t {
    double duration, elapsed;
    AnimationType_t type;
    OWNING void *custom_data;
    WEAK Drawable_t *target;
    bool active;
    AnimationEaseType_t ease_func;
} Animation_t;

// Options and custom data
typedef enum DrawableAlignment_t {
    ALIGN_LEFT = 0,
    ALIGN_CENTER,
    ALIGN_RIGHT
} DrawableAlignment_t;

typedef struct CharOffsetInfo_t {
    int32_t char_idx;
    int32_t start_byte_offset, end_byte_offset;
    double x, y;
    double width, height;
} CharOffsetInfo_t;

typedef struct TextOffsetInfo_t {
    int32_t num_chars, start_char_idx;
    int32_t start_byte_offset, end_byte_offset;
    double start_x;
    double start_y;
    double width, height;
    OWNING Vector_t *char_offsets; // of CharOffsetInfo_t
} TextOffsetInfo_t;

typedef struct Drawable_TextData_t {
    OWNING char *text;
    FontType_t font_type;
    double em;
    Color_t color;
    int wrap_enabled;
    double wrap_width_threshold;
    double measure_at_em;
    double line_padding_em;
    DrawableAlignment_t alignment;
    bool draw_shadow;
    OWNING Vector_t *line_offsets; // of TextOffsetInfo_t
    bool compute_offsets;
    bool increased_line_padding;
} Drawable_TextData_t;

typedef struct Drawable_ImageData_t {
    double border_radius_em;
    bool draw_shadow;
} Drawable_ImageData_t;

typedef struct Drawable_ProgressBarData_t {
    double progress;
    double border_radius_em;
    Color_t fg_color, bg_color;
} Drawable_ProgressBarData_t;

typedef struct Drawable_RectangleData_t {
    double border_radius_em;
    Color_t color;
} Drawable_RectangleData_t;

typedef struct Animation_EaseTranslationData_t {
    double from_x, from_y;
    double to_x, to_y;
    double duration;
    AnimationEaseType_t ease_func;
} Animation_EaseTranslationData_t;

typedef struct Animation_FadeInOutData_t {
    int32_t from_alpha, to_alpha;
    double duration;
    AnimationEaseType_t ease_func;
} Animation_FadeInOutData_t;

typedef struct Animation_ScaleData_t {
    double from_scale, to_scale;
    double duration;
} Animation_ScaleData_t;

typedef struct Animation_DrawRegionData_t {
    DrawRegionOptSet_t draw_regions;
    double duration;
    AnimationEaseType_t ease_func;
} Animation_DrawRegionData_t;

// Init and lifetime functions
Ui_t *ui_init(void);
void ui_finish(Ui_t *ui);
void ui_begin_loop(Ui_t *ui);
void ui_end_loop(void);
void ui_load_font(const unsigned char *data, int data_size, FontType_t type);
void ui_draw(const Ui_t *ui);
// Meta helpers
void ui_set_window_title(const char *title);
void ui_set_bg_color(uint32_t color);
void ui_set_bg_gradient(uint32_t primary, uint32_t secondary, BackgroundType_t type);
void ui_sample_bg_colors_from_image(const unsigned char *bytes, int length);
void ui_on_window_changed(Ui_t *ui);
Container_t *ui_root_container(Ui_t *ui);
void ui_get_drawable_canon_pos(const Drawable_t *drawable, double *x, double *y);
void ui_get_container_canon_pos(const Container_t *container, double *x, double *y, bool include_viewport_offset);
bool ui_mouse_hovering_container(const Container_t *container, Bounds_t *out_canon_bounds, int32_t *out_mouse_x,
                                 int32_t *out_mouse_y);
// Drawables
Drawable_t *ui_make_text(Ui_t *ui, Drawable_TextData_t *data, Container_t *container, const Layout_t *layout);
Drawable_t *ui_make_image(Ui_t *ui, const unsigned char *bytes, int length, Drawable_ImageData_t *data, Container_t *container,
                          const Layout_t *layout);
Drawable_t *ui_make_progressbar(Ui_t *ui, const Drawable_ProgressBarData_t *data, Container_t *container, const Layout_t *layout);
Drawable_t *ui_make_rectangle(Ui_t *ui, const Drawable_RectangleData_t *data, Container_t *container, const Layout_t *layout);
Drawable_t *ui_make_custom(Ui_t *ui, Container_t *container, const Layout_t *layout);
void ui_recompute_drawable(Ui_t *ui, Drawable_t *drawable);
void ui_reposition_drawable(Ui_t *ui, Drawable_t *drawable);
void ui_destroy_drawable(Drawable_t *drawable);
double ui_compute_relative_horizontal(Ui_t *ui, double value, Container_t *parent);
// Change drawable properties
void ui_drawable_set_alpha(Drawable_t *drawable, int32_t alpha);
void ui_drawable_set_alpha_immediate(Drawable_t *drawable, int32_t alpha);
void ui_drawable_set_scale_factor(Drawable_t *drawable, float scale);
void ui_drawable_set_scale_factor_immediate(Drawable_t *drawable, float scale);
void ui_drawable_set_scale_factor_dur(Drawable_t *drawable, float scale, double duration);
void ui_drawable_set_color_mod(Drawable_t *drawable, float color_mod);
void ui_drawable_set_draw_region(Drawable_t *drawable, const DrawRegionOptSet_t *draw_regions);
void ui_drawable_set_draw_region_immediate(Drawable_t *drawable, const DrawRegionOptSet_t *draw_regions);
void ui_drawable_set_draw_region_dur(Drawable_t *drawable, const DrawRegionOptSet_t *draw_regions, double duration);
void ui_drawable_disable_draw_region(Drawable_t *drawable);
void ui_drawable_set_draw_underlay(Drawable_t *drawable, bool draw, uint8_t alpha);
// User-interaction checks
bool ui_mouse_hovering_drawable(const Drawable_t *drawable, int padding, Bounds_t *out_canon_bounds, int32_t *out_mouse_x,
                                int32_t *out_mouse_y);
bool ui_mouse_clicked_drawable(const Drawable_t *drawable, int padding, Bounds_t *out_canon_bounds, int32_t *out_mouse_x,
                               int32_t *out_mouse_y);
// Containers
Container_t *ui_make_container(Ui_t *ui, Container_t *parent, const Layout_t *layout, ContainerFlags_t flags);
void ui_recompute_container(Ui_t *ui, Container_t *container);
void ui_destroy_container(Ui_t *ui, Container_t *container);
// Animations
void ui_animate_translation(Drawable_t *target, const Animation_EaseTranslationData_t *data);
void ui_animate_fade(Drawable_t *target, const Animation_FadeInOutData_t *data);
void ui_animate_scale(Drawable_t *target, const Animation_ScaleData_t *data);
void ui_animate_draw_region(Drawable_t *target, const Animation_DrawRegionData_t *data);

#endif // ETSUKO_UI_H
