#include "karaoke.h"
#include "audio.h"
#include "config.h"
#include "error.h"
#include "events.h"
#include "repository.h"
#include "song.h"
#include "ui.h"
#include "ui_ex.h"

#define RESOURCE_INCLUDE_IMAGES
#include "resource_includes.h"
#include "str_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Karaoke_t {
    Ui_t *ui;
    struct {
        Drawable_t *song_name_text;
        Drawable_t *song_artist_album_text;
        Drawable_t *elapsed_time_text;
        Drawable_t *remaining_time_text;
        Drawable_t *album_image;
        Drawable_t *song_progressbar;
        Drawable_t *play_button;
        Drawable_t *pause_button;
        Drawable_t *back_button;
        Container_t *left_container;
        Container_t *right_container;
        Container_t *song_info_container;
        Container_t *song_controls_container;
        LyricsView_t *lyrics_view;
        Drawable_t *hint_show_hints, *hint_show_lyrics, *hint_seek, *hint_play_pause;
    } drawables;
    bool hovering_controls;
    struct {
        Resource_t *song;
        Resource_t *ui_font;
        Resource_t *lyrics_font;
        Resource_t *audio;
        Resource_t *album_art;
        ResourceBuffer_t *album_art_buffer;
    } resources;
    struct {
        bool song_loaded;
        bool ui_font_loaded, lyrics_font_loaded;
        bool audio_loaded;
        bool album_art_loaded;
        // Only exists during init
        Drawable_t *progress_bar;
        Drawable_t *loading_text;
    } loading;
};

Karaoke_t *karaoke_init(void) {
    Karaoke_t *karaoke = calloc(1, sizeof(*karaoke));
    if ( karaoke == NULL )
        error_abort("Failed to allocate memory for karaoke.");

    karaoke->ui = ui_init();
    events_init();
    audio_init();

    return karaoke;
}

static uint64_t get_total_loading_files_downloaded_bytes(const Karaoke_t *state) {
    uint64_t total = 0;
    if ( state->resources.album_art != NULL )
        total += state->resources.album_art->buffer->downloaded_bytes;

    if ( state->resources.lyrics_font != NULL )
        total += state->resources.lyrics_font->buffer->downloaded_bytes;

    if ( state->resources.ui_font != NULL )
        total += state->resources.ui_font->buffer->downloaded_bytes;

    if ( state->resources.audio != NULL )
        total += state->resources.audio->buffer->downloaded_bytes;

    return total;
}

static uint64_t get_total_loading_files_size(const Karaoke_t *state) {
    uint64_t total = 0;
    if ( state->resources.album_art != NULL )
        total += state->resources.album_art->buffer->total_bytes;

    if ( state->resources.lyrics_font != NULL )
        total += state->resources.lyrics_font->buffer->total_bytes;

    if ( state->resources.ui_font != NULL )
        total += state->resources.ui_font->buffer->total_bytes;

    if ( state->resources.audio != NULL )
        total += state->resources.audio->buffer->total_bytes;

    return total;
}

static void append_loading_file_name(StrBuffer_t *buffer, const Resource_t *res, bool *first) {
    if ( res != NULL ) {
        if ( *first ) {
            *first = false;
        } else {
            str_buf_append(buffer, ", ", NULL);
        }
        str_buf_append(buffer, res->original_filename, NULL);
    }
}

static char *get_loading_files_names(const Karaoke_t *state) {
    StrBuffer_t *buf = str_buf_init();
    str_buf_append(buf, "Loading ", NULL);

    bool first = true;
    append_loading_file_name(buf, state->resources.ui_font, &first);
    append_loading_file_name(buf, state->resources.lyrics_font, &first);
    append_loading_file_name(buf, state->resources.audio, &first);
    append_loading_file_name(buf, state->resources.album_art, &first);

    str_buf_append(buf, "...", NULL);
    char *str = strdup(buf->data);
    str_buf_destroy(buf);

    return str;
}

static void on_song_loaded(const Resource_t *res) {
    if ( res->status == LOAD_ERROR )
        error_abort("Failed to load song file resource");

    song_load(res->original_filename, (char *)res->buffer->data, (int)res->buffer->downloaded_bytes);
    if ( song_get() == NULL )
        error_abort("Failed to load song");

    BackgroundType_t bg_type = BACKGROUND_NONE;
    switch ( song_get()->bg_type ) {
    case BG_SIMPLE_GRADIENT:
        bg_type = BACKGROUND_GRADIENT;
        break;
    case BG_SANDS_GRADIENT:
        bg_type = BACKGROUND_SANDS_GRADIENT;
        break;
    case BG_RANDOM_GRADIENT:
        bg_type = BACKGROUND_RANDOM_GRADIENT;
        break;
    case BG_CLOUD_GRADIENT:
        bg_type = BACKGROUND_CLOUD_GRADIENT;
        break;
    case BG_AM_LIKE_GRADIENT:
        bg_type = BACKGROUND_AM_LIKE_GRADIENT;
        break;
    default:
        break;
    }
    ui_set_bg_gradient(song_get()->bg_color, song_get()->bg_color_secondary, bg_type);

    Karaoke_t *state = res->custom_data;
    state->loading.song_loaded = true;
}

static void on_ui_font_loaded(const Resource_t *res) {
    if ( res->status == LOAD_ERROR )
        error_abort("Failed to load UI font resource");
    ui_load_font(res->buffer->data, (int)res->buffer->downloaded_bytes, FONT_UI);

    Karaoke_t *state = res->custom_data;
    state->loading.ui_font_loaded = true;
}

static void on_lyrics_font_loaded(const Resource_t *res) {
    if ( res->status == LOAD_ERROR )
        error_abort("Failed to load lyrics font resource");
    ui_load_font(res->buffer->data, (int)res->buffer->downloaded_bytes, FONT_LYRICS);

    Karaoke_t *state = res->custom_data;
    state->loading.lyrics_font_loaded = true;
}

static void on_audio_loaded(const Resource_t *res) {
    if ( res->status == LOAD_ERROR )
        error_abort("Failed to load audio resource");
    audio_load(res->buffer->data, (int)res->buffer->downloaded_bytes);

    Karaoke_t *state = res->custom_data;
    state->loading.audio_loaded = true;
}

static void on_album_art_loaded(const Resource_t *res) {
    if ( res->status == LOAD_ERROR )
        error_abort("Failed to load album art resource");

    Color_t colors[5];
    render_sample_bg_colors_from_image(res->buffer->data, (int)res->buffer->downloaded_bytes, colors);
    render_set_bg_colors(colors);

    Karaoke_t *state = res->custom_data;
    state->loading.album_art_loaded = true;
    state->resources.album_art_buffer = res->buffer;
}

static bool load_async(Karaoke_t *state) {
    const Config_t *config = config_get();
    // UI Font
    if ( state->resources.ui_font == NULL ) {
        state->resources.ui_font = repo_load_resource(&(LoadRequest_t){.relative_path = config->ui_font,
                                                                 .sub_dir = "files/",
                                                                 .on_resource_loaded = on_ui_font_loaded,
                                                                 .custom_data = state});
    }
    // Song
    if ( state->resources.song == NULL ) {
        state->resources.song = repo_load_resource(
            &(LoadRequest_t){.relative_path = config->karaoke.song_file, .on_resource_loaded = on_song_loaded, .custom_data = state});
    }
    if ( !state->loading.song_loaded )
        return false;
    // Finish loading the song before we load the rest

    // Lyrics font
    // It's important we begin downloading this _after_ the song has been loaded so we know which is the correct font to fetch
    if ( state->resources.lyrics_font == NULL ) {
        const char *font = song_get()->font_override;
        if ( font == NULL )
            font = config->lyrics_font;
        state->resources.lyrics_font = repo_load_resource(&(LoadRequest_t){
            .relative_path = font, .sub_dir = "files/", .on_resource_loaded = on_lyrics_font_loaded, .custom_data = state});
    }

    // Song audio file
    if ( state->resources.audio == NULL ) {
        state->resources.audio = repo_load_resource(&(LoadRequest_t){
            .relative_path = song_get()->file_path, .on_resource_loaded = on_audio_loaded, .custom_data = state});
    }
    // Album art
    if ( state->resources.album_art == NULL ) {
        state->resources.album_art = repo_load_resource(&(LoadRequest_t){
            .relative_path = song_get()->album_art_path, .on_resource_loaded = on_album_art_loaded, .custom_data = state});
    }

    return state->loading.ui_font_loaded && state->loading.lyrics_font_loaded && state->loading.audio_loaded && state->loading.album_art_loaded;
}

AppStatus_t karaoke_load_loop(Karaoke_t *state) {
    events_loop();
    if ( events_has_quit() )
        return APP_STATUS_FAILURE;

    if ( state->loading.ui_font_loaded && config_get()->karaoke.show_loading_screen ) {
        if ( state->loading.progress_bar == NULL ) {
            const Drawable_ProgressBarData_t data = {
                .progress = 0,
                .border_radius_em = BORDER_RADIUS_AUTO,
                .fg_color = (Color_t){.r = 200, .g = 200, .b = 200, .a = 255},
                .bg_color = (Color_t){.r = 100, .g = 100, .b = 100, .a = 255},
            };
            const Layout_t layout = {
                .flags = LAYOUT_CENTER | LAYOUT_PROPORTIONAL_SIZE,
                .width = 0.75,
                .height = 0.02,
            };
            state->loading.progress_bar = ui_make_progressbar(state->ui, &data, ui_root_container(state->ui), &layout);
        } else {
            const uint64_t total_size = get_total_loading_files_size(state);
            const uint64_t downloaded = get_total_loading_files_downloaded_bytes(state);
            Drawable_ProgressBarData_t *data = state->loading.progress_bar->custom_data;
            data->progress = (double)downloaded / (double)total_size;
        }

        if ( state->loading.loading_text == NULL ) {
            const Drawable_TextData_t data = {
                .text = "Loading...", .em = 1.5, .color = {.r = 200, .g = 200, .b = 200, .a = 255}, .font_type = FONT_UI};
            const Layout_t layout = {.flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_PROPORTIONAL_Y |
                                              LAYOUT_ANCHOR_BOTTOM_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT,
                                     .offset_y = -0.035,
                                     .relative_to = state->loading.progress_bar};
            state->loading.loading_text = ui_make_text(state->ui, &data, ui_root_container(state->ui), &layout);
        } else {
            char *current_loading_text = get_loading_files_names(state);
            Drawable_TextData_t *text_data = state->loading.loading_text->custom_data;

            if ( strcmp(current_loading_text, text_data->text) != 0 ) {
                free(text_data->text);
                text_data->text = current_loading_text;
                ui_recompute_drawable(state->ui, state->loading.loading_text);
            } else {
                free(current_loading_text);
            }
        }
    }

    ui_begin_loop(state->ui);
    // Recalculate dynamic elements
    const bool initialized = load_async(state);

    events_frame_end();
    ui_draw(state->ui);
    ui_end_loop();

    if ( initialized ) {
        // Free loading resources
        repo_resource_destroy(state->resources.song);
        repo_resource_destroy(state->resources.ui_font);
        repo_resource_destroy(state->resources.lyrics_font);
        repo_resource_destroy(state->resources.audio);
        // We will free later, when initializing the album art drawable
        repo_resource_buffer_leak(state->resources.album_art);
        repo_resource_destroy(state->resources.album_art);

        return APP_STATUS_OK;
    }

    return APP_STATUS_LOADING;
}

static void toggle_pause(const Karaoke_t *state) {
    if ( audio_is_paused() ) {
        audio_resume();
        state->drawables.lyrics_view->container->viewport_y = 0;
    } else
        audio_pause();
}

/**
 * Toggles the screen state between having the album art on the left and the lyrics on the right,
 * to the lyrics disappearing and the album art (and song info) being centered in the screen
 */
static void toggle_show_lyrics(const Karaoke_t *state) {
    state->drawables.right_container->enabled = !state->drawables.right_container->enabled;

    Layout_t *layout = &state->drawables.left_container->layout;
    if ( state->drawables.right_container->enabled ) {
        // Left container stays on the left (duh)
        layout->flags &= ~LAYOUT_ANCHOR_CENTER_X;
        layout->offset_x = 0;
    } else {
        // Position the left container in the center of the screen (which should bring with it everything it holds)
        layout->flags |= LAYOUT_ANCHOR_CENTER_X;
        layout->offset_x = 0.5;
    }

    ui_reposition_container(state->ui, state->drawables.left_container);
}

static void on_mouse_moved(const UiEventOpts_t *, const Drawable_t *, void *custom_data) {
    const Karaoke_t *state = custom_data;
    //state->hovering_controls = ui_mouse_hovering_container(state->drawables.song_info_container, NULL, NULL, NULL);
    state->drawables.song_name_text->enabled = state->drawables.song_artist_album_text->enabled = false;
    state->drawables.song_controls_container->enabled = true;

    state->drawables.hint_play_pause->enabled = true;
    state->drawables.hint_seek->enabled = true;
    state->drawables.hint_show_hints->enabled = true;
    state->drawables.hint_show_lyrics->enabled = true;
}

static void on_mouse_stopped(const UiEventOpts_t *opt, const Drawable_t *, void *custom_data) {
    const Karaoke_t *state = custom_data;
    const bool show_controls = state->hovering_controls || audio_elapsed_time() <= 0.1;

    // Hide controls after the mouse stopped for more than 2 seconds
    // TODO: Config this
    if ( opt->mouse.duration > 2.0 ) {
        if ( !show_controls ) {
            state->drawables.song_name_text->enabled = state->drawables.song_artist_album_text->enabled = true;
            state->drawables.song_controls_container->enabled = false;
        }
        state->drawables.hint_play_pause->enabled = false;
        state->drawables.hint_seek->enabled = false;
        state->drawables.hint_show_hints->enabled = false;
        state->drawables.hint_show_lyrics->enabled = false;
    }
}

static void on_key_pressed(const UiEventOpts_t *opts, const Drawable_t *, void *custom_data) {
    const Karaoke_t *state = custom_data;

    if ( opts->keyboard.key == KEY_SPACE ) {
        toggle_pause(state);
    } else if ( opts->keyboard.key == KEY_ARROW_LEFT ) {
        audio_seek_relative(-5);
    } else if ( opts->keyboard.key == KEY_ARROW_RIGHT ) {
        audio_seek_relative(+5);
    } else if ( opts->keyboard.key == KEY_L ) {
        toggle_show_lyrics(state);
    }
}

static void on_mouse_play_button(const UiEventOpts_t *opts, const Drawable_t *, void *custom_data) {
    Karaoke_t *state = custom_data;

    if ( opts->event == UI_EVENT_MOUSE_HOVER_ENTERED ) {
        state->hovering_controls = true;
    } else if ( opts->event == UI_EVENT_MOUSE_HOVER_EXITED ) {
        state->hovering_controls = false;
    } else if ( opts->event == UI_EVENT_MOUSE_CLICK ) {
        toggle_pause(state);
    }
}

static void on_back_clicked(const UiEventOpts_t *, const Drawable_t *, void *) {
    etsuko_navigate("/", "");
    global_mode_switch(APP_MODE_MENU);
}

static void on_progress_bar_clicked(const UiEventOpts_t *opt, const Drawable_t *progress_bar, void *custom_data) {
    const Karaoke_t *state = custom_data;

    double progress_bar_x;
    ui_get_drawable_canon_pos(progress_bar, &progress_bar_x, NULL);
    const double distance_from_x = opt->mouse.x - progress_bar_x;
    const double distance = distance_from_x / state->drawables.song_progressbar->bounds.w;
    audio_seek(audio_total_time() * distance);
    // Reset viewport
    state->drawables.lyrics_view->container->viewport_y = 0;
}

void karaoke_setup(Karaoke_t *state) {
    if ( state->ui != NULL ) {
        ui_finish(state->ui);
        state->loading.progress_bar = state->loading.loading_text = NULL;
    }
    state->ui = ui_init();

    char *window_title;
    asprintf(&window_title, "%s - %s", APP_NAME, song_get()->name);
    ui_set_window_title(window_title);
    free(window_title);

    const double vertical_padding = 0.01;

    // Make the left container
    state->drawables.left_container =
        ui_make_container(state->ui, ui_root_container(state->ui),
                          &(Layout_t){.width = 0.5, .height = 1.0, .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_PROPORTIONAL_POS},
                          CONTAINER_VERTICAL_ALIGN_CONTENT);

    // Make the right container
    state->drawables.right_container = ui_make_container(state->ui, ui_root_container(state->ui),
                                               &(Layout_t){.width = 0.5,
                                                           .height = 0.7,
                                                           .offset_x = 0.5,
                                                           .offset_y = 0.35,
                                                           .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_PROPORTIONAL_POS},
                                               CONTAINER_NONE);

    // Version string
    etsuko_setup_version(state->ui);
    // Make the back (to menu) "button"
    state->drawables.back_button = ui_make_text(
        state->ui, &(Drawable_TextData_t){.text = "< Back", .color = {.r = 255, .g = 255, .b = 255, .a = 255}, .em = 0.8},
        ui_root_container(state->ui), &(Layout_t){.offset_x = 0.01, .offset_y = 0.01, .flags = LAYOUT_PROPORTIONAL_POS});
    ui_drawable_set_alpha_immediate(state->drawables.back_button, 128);

    // Album art
    state->drawables.album_image = ui_make_image(
        state->ui, state->resources.album_art_buffer->data, (int)state->resources.album_art_buffer->downloaded_bytes,
        &(Drawable_ImageData_t){
            .border_radius_em = 2.0,
            .draw_shadow = config_get()->karaoke.draw_album_art_shadow,
        },
        state->drawables.left_container,
        &(Layout_t){.height = 0.6,
                    .width = 0.6,
                    .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_CENTER_X | LAYOUT_SPECIAL_KEEP_ASPECT_RATIO,
                    .max_width = {.type = CONSTRAINT_RELATIVE, .relative_to = &state->drawables.left_container->bounds, .value = 0.6},
                    .max_height = {.type = CONSTRAINT_RELATIVE, .relative_to = &state->drawables.left_container->bounds, .value = 0.6}});
    repo_resource_buffer_destroy(state->resources.album_art_buffer);

    // Song info container
    state->drawables.song_info_container =
        ui_make_container(state->ui, state->drawables.left_container,
                          &(Layout_t){.height = 0.3,
                                      .width = 1.0,
                                      .offset_y = vertical_padding,
                                      .relative_to = state->drawables.album_image,
                                      .relative_to_size = state->drawables.album_image,
                                      .flags = LAYOUT_CENTER_X | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_RELATIVE_TO_Y |
                                               LAYOUT_RELATIVE_TO_WIDTH | LAYOUT_PROPORTIONAL_H | LAYOUT_PROPORTIONAL_Y},
                          CONTAINER_NONE);

    // Elapsed time
    state->drawables.elapsed_time_text =
        ui_make_text(state->ui,
                     &(Drawable_TextData_t){
                         .text = "00:00", .font_type = FONT_UI, .em = 0.8, .color = {255, 255, 255, 200}, .draw_shadow = true},
                     state->drawables.song_info_container, &(Layout_t){0});
    ui_drawable_set_alpha_immediate(state->drawables.elapsed_time_text, 200);

    // Remaining time
    state->drawables.remaining_time_text = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){
            .text = "-00:00", .font_type = FONT_UI, .em = 0.8, .color = {255, 255, 255, 200}, .draw_shadow = true},
        state->drawables.song_info_container, &(Layout_t){.offset_x = -1, .flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X});
    ui_drawable_set_alpha_immediate(state->drawables.remaining_time_text, 200);

    // Progress bar
    state->drawables.song_progressbar = ui_make_progressbar(state->ui,
                                                  &(Drawable_ProgressBarData_t){
                                                      .progress = 0,
                                                      .border_radius_em = BORDER_RADIUS_AUTO,
                                                      .fg_color = (Color_t){.r = 255, .g = 255, .b = 255, .a = 255},
                                                      .bg_color = (Color_t){.r = 150, .g = 150, .b = 150, .a = 50},
                                                  },
                                                  state->drawables.song_info_container,
                                                  &(Layout_t){.offset_y = 0.02,
                                                              .width = 1.0,
                                                              .height = 0.025,
                                                              .relative_to = state->drawables.elapsed_time_text,
                                                              .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_RELATIVE_TO_Y |
                                                                       LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y});

    // Song name
    state->drawables.song_name_text = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){
            .text = song_get()->name, .font_type = FONT_UI, .em = 0.9, .color = {255, 255, 255, 255}, .draw_shadow = true},
        state->drawables.song_info_container,
        &(Layout_t){.offset_y = 0.05,
                    .relative_to = state->drawables.song_progressbar,
                    .flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y});
    ui_drawable_set_alpha_immediate(state->drawables.song_name_text, 200);

    // Song artist and album
    char *artist_album_text;
    asprintf(&artist_album_text, "%s - %s", song_get()->artist, song_get()->album);

    state->drawables.song_artist_album_text = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){
            .text = artist_album_text, .font_type = FONT_UI, .em = 0.7, .color = {255, 255, 255, 255}, .draw_shadow = true},
        state->drawables.song_info_container,
        &(Layout_t){.offset_y = 0.01,
                    .relative_to = state->drawables.song_name_text,
                    .flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y});
    ui_drawable_set_alpha_immediate(state->drawables.song_artist_album_text, 100);
    free(artist_album_text);

    // Song controls container
    state->drawables.song_controls_container =
        ui_make_container(state->ui, state->drawables.song_info_container,
                          &(Layout_t){.width = 1.0,
                                      .height = 0.15,
                                      .offset_y = 0.07,
                                      .relative_to = state->drawables.song_progressbar,
                                      .flags = LAYOUT_CENTER_X | LAYOUT_PROPORTIONAL_SIZE | LAYOUT_RELATIVE_TO_Y |
                                               LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y},
                          CONTAINER_NONE);

    // Play and pause buttons
    const unsigned char *play_bytes = incbin_play_img;
    const int play_bytes_len = sizeof incbin_play_img;
    state->drawables.play_button =
        ui_make_image(state->ui, play_bytes, play_bytes_len, &(Drawable_ImageData_t){0}, state->drawables.song_controls_container,
                      &(Layout_t){.offset_x = 0,
                                  .offset_y = 0,
                                  .width = 0.05,
                                  .flags = LAYOUT_SPECIAL_KEEP_ASPECT_RATIO | LAYOUT_CENTER | LAYOUT_PROPORTIONAL_W});

    const unsigned char *pause_bytes = incbin_pause_img;
    const int pause_bytes_len = sizeof incbin_pause_img;
    state->drawables.pause_button =
        ui_make_image(state->ui, pause_bytes, pause_bytes_len, &(Drawable_ImageData_t){0}, state->drawables.song_controls_container,
                      &(Layout_t){.offset_x = 0,
                                  .offset_y = 0,
                                  .width = 0.05,
                                  .flags = LAYOUT_SPECIAL_KEEP_ASPECT_RATIO | LAYOUT_CENTER | LAYOUT_PROPORTIONAL_W});
    state->drawables.pause_button->enabled = false;

    state->drawables.lyrics_view = ui_ex_make_lyrics_view(state->ui, state->drawables.right_container, song_get());

    // Help text on the bottom left
    // About how to show reading hints
    state->drawables.hint_show_hints =
        ui_make_text(state->ui,
                     &(Drawable_TextData_t){
                         .text = "R: Show/hide reading hints", .em = 0.5, .draw_shadow = true, .color = {255, 255, 255, 255}},
                     ui_root_container(state->ui),
                     &(Layout_t){.offset_y = -0.005,
                                 .offset_x = 0.005,
                                 .flags = LAYOUT_PROPORTIONAL_POS | LAYOUT_WRAP_AROUND_Y | LAYOUT_ANCHOR_BOTTOM_Y});
    ui_drawable_set_alpha_immediate(state->drawables.hint_show_hints, 150);
    state->drawables.hint_show_hints->enabled = false;
    // About hiding lyrics
    state->drawables.hint_show_lyrics = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){.text = "L: Show/Hide lyrics", .em = 0.5, .draw_shadow = true, .color = {255, 255, 255, 255}},
        ui_root_container(state->ui),
        &(Layout_t){.offset_y = -0.001,
                    .flags = LAYOUT_PROPORTIONAL_Y | LAYOUT_ANCHOR_BOTTOM_Y | LAYOUT_RELATIVE_TO_POS,
                    .relative_to = state->drawables.hint_show_hints});
    ui_drawable_set_alpha_immediate(state->drawables.hint_show_lyrics, 150);
    state->drawables.hint_show_lyrics->enabled = false;
    // About seeking with arrow keys
    state->drawables.hint_seek = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){
            .text = "Arrow keys: Seek backward/forward", .em = 0.5, .draw_shadow = true, .color = {255, 255, 255, 255}},
        ui_root_container(state->ui),
        &(Layout_t){.offset_y = -0.001,
                    .flags = LAYOUT_PROPORTIONAL_Y | LAYOUT_ANCHOR_BOTTOM_Y | LAYOUT_RELATIVE_TO_POS,
                    .relative_to = state->drawables.hint_show_lyrics});
    ui_drawable_set_alpha_immediate(state->drawables.hint_seek, 150);
    state->drawables.hint_seek->enabled = false;
    // About using space to play/pause
    state->drawables.hint_play_pause = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){.text = "Space: Play/pause", .em = 0.5, .draw_shadow = true, .color = {255, 255, 255, 255}},
        ui_root_container(state->ui),
        &(Layout_t){.offset_y = -0.001,
                    .flags = LAYOUT_PROPORTIONAL_Y | LAYOUT_ANCHOR_BOTTOM_Y | LAYOUT_RELATIVE_TO_POS,
                    .relative_to = state->drawables.hint_seek});
    ui_drawable_set_alpha_immediate(state->drawables.hint_play_pause, 150);
    state->drawables.hint_play_pause->enabled = false;

    // Default state until mouse moves
    state->drawables.song_name_text->enabled = state->drawables.song_artist_album_text->enabled = false;
    state->drawables.song_controls_container->enabled = true;

    // Global events
    ui_add_global_event_callback(state->ui, UI_EVENT_MOUSE_MOVE, on_mouse_moved, state);
    ui_add_global_event_callback(state->ui, UI_EVENT_MOUSE_STOPPED, on_mouse_stopped, state);
    ui_add_global_event_callback(state->ui, UI_EVENT_KEY_PRESSED, on_key_pressed, state);
    // Play button events
    ui_add_event_callback(state->ui, UI_EVENT_MOUSE_HOVER_ENTERED, state->drawables.play_button, on_mouse_play_button, state);
    ui_add_event_callback(state->ui, UI_EVENT_MOUSE_HOVER_EXITED, state->drawables.play_button, on_mouse_play_button, state);
    ui_add_event_callback(state->ui, UI_EVENT_MOUSE_CLICK, state->drawables.play_button, on_mouse_play_button, state);
    // Back button events
    ui_add_event_callback(state->ui, UI_EVENT_MOUSE_CLICK, state->drawables.back_button, on_back_clicked, NULL);
    // Progress bar events
    ui_add_event_callback(state->ui, UI_EVENT_MOUSE_CLICK, state->drawables.song_progressbar, on_progress_bar_clicked, state);
}

static void update_elapsed_text(const Karaoke_t *state) {
    const double elapsed = audio_elapsed_time();
    const int32_t minutes = (int32_t)(elapsed / 60);
    const int32_t seconds = (int32_t)elapsed % 60;

    char *time_str;
    asprintf(&time_str, "%.2d:%.2d", minutes, seconds);

    Drawable_TextData_t *custom_data = state->drawables.elapsed_time_text->custom_data;
    if ( strncmp(custom_data->text, time_str, 5) != 0 ) {
        free(custom_data->text);
        custom_data->text = time_str;
        ui_recompute_drawable(state->ui, state->drawables.elapsed_time_text);
    } else {
        free(time_str);
    }
}

static void update_remaining_text(const Karaoke_t *state) {
    const double remaining = audio_total_time() - audio_elapsed_time();
    const int32_t minutes = (int32_t)(remaining / 60);
    const int32_t seconds = (int32_t)remaining % 60;
    char *time_str;
    asprintf(&time_str, "-%.2d:%.2d", minutes, seconds);

    Drawable_TextData_t *custom_data = state->drawables.remaining_time_text->custom_data;
    if ( strcmp(time_str, custom_data->text) != 0 ) {
        free(custom_data->text);
        custom_data->text = time_str;
        ui_recompute_drawable(state->ui, state->drawables.remaining_time_text);
    } else {
        free(time_str);
    }
}

static void update_song_progressbar(const Karaoke_t *state) {
    if ( state->drawables.song_progressbar != NULL ) {
        const double progress = audio_elapsed_time() / audio_total_time();
        ((Drawable_ProgressBarData_t *)state->drawables.song_progressbar->custom_data)->progress = (float)progress;
    }
}

static void update_play_pause_state(const Karaoke_t *state) {
    const bool paused = audio_is_paused();
    state->drawables.play_button->enabled = paused;
    state->drawables.pause_button->enabled = !paused;
}

static void handle_user_input(const Karaoke_t *state) {
    if ( ui_mouse_hovering_container(state->drawables.lyrics_view->container, NULL, NULL, NULL) ) {
        ui_ex_lyrics_view_on_scroll(state->drawables.lyrics_view, events_get_mouse_scrolled());
    }
}

AppStatus_t karaoke_loop(const Karaoke_t *state) {
    events_loop();
    if ( events_has_quit() )
        return APP_STATUS_FAILURE;
    audio_loop();

    // Check for user inputs
    handle_user_input(state);

    ui_begin_loop(state->ui);
    // Recalculate dynamic elements
    update_elapsed_text(state);
    update_remaining_text(state);
    update_song_progressbar(state);
    update_play_pause_state(state);
    // Update the lyrics view
    if ( events_window_changed() )
        ui_ex_lyrics_view_on_screen_change(state->ui, state->drawables.lyrics_view);
    ui_ex_lyrics_view_loop(state->ui, state->drawables.lyrics_view);

    // Clear events after all checking has been done because under emscripten the events aren't polled inside glfw
    // so we would clear all the events before we could see them
    events_frame_end();

    ui_draw(state->ui);
    ui_end_loop();

    global_update();
    return APP_STATUS_OK;
}

void karaoke_finish(const Karaoke_t *state) {
    events_finish();
    ui_finish(state->ui);
    audio_finish();
}
