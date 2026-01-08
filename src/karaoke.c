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
    Drawable_t *version_text;
    Drawable_t *song_name_text;
    Drawable_t *song_artist_album_text;
    Drawable_t *elapsed_time_text;
    Drawable_t *remaining_time_text;
    Drawable_t *album_image;
    Drawable_t *song_progressbar;
    Drawable_t *play_button;
    Drawable_t *pause_button;
    Container_t *left_container;
    Container_t *right_container;
    Container_t *song_info_container;
    Container_t *song_controls_container;
    LyricsView_t *lyrics_view;
    // Only exists during init
    Drawable_t *loading_progress_bar;
    Drawable_t *loading_text;
    // Loading stuff
    Resource_t *res_song;
    Resource_t *res_ui_font;
    Resource_t *res_lyrics_font;
    Resource_t *res_audio;
    Resource_t *res_album_art;
    ResourceBuffer_t *res_album_art_buffer;
    bool song_loaded;
    bool ui_font_loaded, lyrics_font_loaded;
    bool audio_loaded;
    bool album_art_loaded;
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
    if ( state->res_album_art != NULL )
        total += state->res_album_art->buffer->downloaded_bytes;

    if ( state->res_lyrics_font != NULL )
        total += state->res_lyrics_font->buffer->downloaded_bytes;

    if ( state->res_ui_font != NULL )
        total += state->res_ui_font->buffer->downloaded_bytes;

    if ( state->res_audio != NULL )
        total += state->res_audio->buffer->downloaded_bytes;

    return total;
}

static uint64_t get_total_loading_files_size(const Karaoke_t *state) {
    uint64_t total = 0;
    if ( state->res_album_art != NULL )
        total += state->res_album_art->buffer->total_bytes;

    if ( state->res_lyrics_font != NULL )
        total += state->res_lyrics_font->buffer->total_bytes;

    if ( state->res_ui_font != NULL )
        total += state->res_ui_font->buffer->total_bytes;

    if ( state->res_audio != NULL )
        total += state->res_audio->buffer->total_bytes;

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
    append_loading_file_name(buf, state->res_ui_font, &first);
    append_loading_file_name(buf, state->res_lyrics_font, &first);
    append_loading_file_name(buf, state->res_audio, &first);
    append_loading_file_name(buf, state->res_album_art, &first);

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
    state->song_loaded = true;
}

static void on_ui_font_loaded(const Resource_t *res) {
    if ( res->status == LOAD_ERROR )
        error_abort("Failed to load UI font resource");
    ui_load_font(res->buffer->data, (int)res->buffer->downloaded_bytes, FONT_UI);

    Karaoke_t *state = res->custom_data;
    state->ui_font_loaded = true;
}

static void on_lyrics_font_loaded(const Resource_t *res) {
    if ( res->status == LOAD_ERROR )
        error_abort("Failed to load lyrics font resource");
    ui_load_font(res->buffer->data, (int)res->buffer->downloaded_bytes, FONT_LYRICS);

    Karaoke_t *state = res->custom_data;
    state->lyrics_font_loaded = true;
}

static void on_audio_loaded(const Resource_t *res) {
    if ( res->status == LOAD_ERROR )
        error_abort("Failed to load audio resource");
    audio_load(res->buffer->data, (int)res->buffer->downloaded_bytes);

    Karaoke_t *state = res->custom_data;
    state->audio_loaded = true;
}

static void on_album_art_loaded(const Resource_t *res) {
    if ( res->status == LOAD_ERROR )
        error_abort("Failed to load album art resource");

    ui_sample_bg_colors_from_image(res->buffer->data, (int)res->buffer->downloaded_bytes);

    Karaoke_t *state = res->custom_data;
    state->album_art_loaded = true;
    state->res_album_art_buffer = res->buffer;
}

static bool load_async(Karaoke_t *state) {
    const Config_t *config = config_get();
    // UI Font
    if ( state->res_ui_font == NULL ) {
        state->res_ui_font = repo_load_resource(&(LoadRequest_t){.relative_path = config->ui_font,
                                                                 .sub_dir = "files/",
                                                                 .on_resource_loaded = on_ui_font_loaded,
                                                                 .custom_data = state});
    }
    // Song
    if ( state->res_song == NULL ) {
        state->res_song = repo_load_resource(
            &(LoadRequest_t){.relative_path = config->song_file, .on_resource_loaded = on_song_loaded, .custom_data = state});
    }
    if ( !state->song_loaded )
        return false;
    // Finish loading the song before we load the rest

    // Lyrics font
    // It's important we begin downloading this _after_ the song has been loaded so we know which is the correct font to fetch
    if ( state->res_lyrics_font == NULL ) {
        const char *font = song_get()->font_override;
        if ( font == NULL )
            font = config->lyrics_font;
        state->res_lyrics_font = repo_load_resource(&(LoadRequest_t){
            .relative_path = font, .sub_dir = "files/", .on_resource_loaded = on_lyrics_font_loaded, .custom_data = state});
    }

    // Song audio file
    if ( state->res_audio == NULL ) {
        state->res_audio = repo_load_resource(&(LoadRequest_t){
            .relative_path = song_get()->file_path, .on_resource_loaded = on_audio_loaded, .custom_data = state});
    }
    // Album art
    if ( state->res_album_art == NULL ) {
        state->res_album_art = repo_load_resource(&(LoadRequest_t){
            .relative_path = song_get()->album_art_path, .on_resource_loaded = on_album_art_loaded, .custom_data = state});
    }

    return state->ui_font_loaded && state->lyrics_font_loaded && state->audio_loaded && state->album_art_loaded;
}

int karaoke_load_loop(Karaoke_t *state) {
    events_loop();
    if ( events_has_quit() )
        return -1;

    if ( state->ui_font_loaded && config_get()->show_loading_screen ) {
        if ( state->loading_progress_bar == NULL ) {
            const Drawable_ProgressBarData_t data = {
                .progress = 0,
                .border_radius_em = 0.8,
                .fg_color = (Color_t){.r = 200, .g = 200, .b = 200, .a = 255},
                .bg_color = (Color_t){.r = 100, .g = 100, .b = 100, .a = 255},
            };
            const Layout_t layout = {
                .flags = LAYOUT_CENTER | LAYOUT_PROPORTIONAL_SIZE,
                .width = 0.75,
                .height = 0.02,
            };
            state->loading_progress_bar = ui_make_progressbar(state->ui, &data, ui_root_container(state->ui), &layout);
        } else {
            const uint64_t total_size = get_total_loading_files_size(state);
            const uint64_t downloaded = get_total_loading_files_downloaded_bytes(state);
            Drawable_ProgressBarData_t *data = state->loading_progress_bar->custom_data;
            data->progress = (double)downloaded / (double)total_size;
        }

        if ( state->loading_text == NULL ) {
            const Drawable_TextData_t data = {
                .text = "Loading...", .em = 1.5, .color = {.r = 200, .g = 200, .b = 200, .a = 255}, .font_type = FONT_UI};
            const Layout_t layout = {.flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_PROPORTIONAL_Y |
                                              LAYOUT_ANCHOR_BOTTOM_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT,
                                     .offset_y = -0.035,
                                     .relative_to = state->loading_progress_bar};
            state->loading_text = ui_make_text(state->ui, &data, ui_root_container(state->ui), &layout);
        } else {
            char *current_loading_text = get_loading_files_names(state);
            Drawable_TextData_t *text_data = state->loading_text->custom_data;

            if ( strcmp(current_loading_text, text_data->text) != 0 ) {
                free(text_data->text);
                text_data->text = current_loading_text;
                ui_recompute_drawable(state->ui, state->loading_text);
            } else {
                free(current_loading_text);
            }
        }
    }

    ui_begin_loop(state->ui);
    // Recalculate dynamic elements
    const int initialized = load_async(state);

    events_frame_end();
    ui_draw(state->ui);
    ui_end_loop();

    if ( initialized ) {
        // Free loading resources
        repo_resource_destroy(state->res_song);
        repo_resource_destroy(state->res_ui_font);
        repo_resource_destroy(state->res_lyrics_font);
        repo_resource_destroy(state->res_audio);
        // We will free later, when initializing the album art drawable
        repo_resource_buffer_leak(state->res_album_art);
        repo_resource_destroy(state->res_album_art);
    }

    return initialized;
}

void karaoke_setup(Karaoke_t *state) {
    if ( state->ui != NULL ) {
        ui_finish(state->ui);
        state->loading_progress_bar = state->loading_text = NULL;
    }
    state->ui = ui_init();

    char *window_title;
    asprintf(&window_title, "%s - %s", APP_NAME, song_get()->name);
    ui_set_window_title(window_title);
    free(window_title);

    const double vertical_padding = 0.01;

    // Make the left container
    state->left_container = ui_make_container(state->ui, ui_root_container(state->ui),
                                              &(Layout_t){.width = 0.5, .height = 1.0, .flags = LAYOUT_PROPORTIONAL_SIZE},
                                              CONTAINER_VERTICAL_ALIGN_CONTENT);

    // Make the right container
    state->right_container = ui_make_container(state->ui, ui_root_container(state->ui),
                                               &(Layout_t){.width = 0.5,
                                                           .height = 0.7,
                                                           .offset_x = 0.5,
                                                           .offset_y = 0.35,
                                                           .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_PROPORTIONAL_POS},
                                               CONTAINER_NONE);

    // Version string
    state->version_text = ui_make_text(state->ui,
                                       &(Drawable_TextData_t){
                                           .text = "etsuko v" VERSION,
                                           .font_type = FONT_UI,
                                           .em = 0.8,
                                           .color = {255, 255, 255, 255},
                                       },
                                       ui_root_container(state->ui),
                                       &(Layout_t){.offset_x = -1, .flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X});
    ui_drawable_set_alpha_immediate(state->version_text, 128);

    // Album art
    state->album_image = ui_make_image(
        state->ui, state->res_album_art_buffer->data, (int)state->res_album_art_buffer->downloaded_bytes,
        &(Drawable_ImageData_t){
            .border_radius_em = 2.0,
            .draw_shadow = config_get()->draw_album_art_shadow,
        },
        state->left_container,
        &(Layout_t){
            .height = 0.6, .width = 0.6, .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_CENTER_X | LAYOUT_SPECIAL_KEEP_ASPECT_RATIO});
    repo_resource_buffer_destroy(state->res_album_art_buffer);

    // Song info container
    state->song_info_container =
        ui_make_container(state->ui, state->left_container,
                          &(Layout_t){.height = 0.3,
                                      .width = 1.0,
                                      .offset_y = vertical_padding,
                                      .relative_to = state->album_image,
                                      .relative_to_size = state->album_image,
                                      .flags = LAYOUT_CENTER_X | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_RELATIVE_TO_Y |
                                               LAYOUT_RELATIVE_TO_WIDTH | LAYOUT_PROPORTIONAL_H | LAYOUT_PROPORTIONAL_Y},
                          CONTAINER_NONE);

    // Elapsed time
    state->elapsed_time_text =
        ui_make_text(state->ui,
                     &(Drawable_TextData_t){
                         .text = "00:00", .font_type = FONT_UI, .em = 0.8, .color = {255, 255, 255, 200}, .draw_shadow = true},
                     state->song_info_container, &(Layout_t){0});
    ui_drawable_set_alpha_immediate(state->elapsed_time_text, 200);

    // Remaining time
    state->remaining_time_text = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){
            .text = "-00:00", .font_type = FONT_UI, .em = 0.8, .color = {255, 255, 255, 200}, .draw_shadow = true},
        state->song_info_container, &(Layout_t){.offset_x = -1, .flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X});
    ui_drawable_set_alpha_immediate(state->remaining_time_text, 200);

    // Progress bar
    state->song_progressbar = ui_make_progressbar(state->ui,
                                                  &(Drawable_ProgressBarData_t){
                                                      .progress = 0,
                                                      .border_radius_em = 0.3,
                                                      .fg_color = (Color_t){.r = 255, .g = 255, .b = 255, .a = 255},
                                                      .bg_color = (Color_t){.r = 150, .g = 150, .b = 150, .a = 50},
                                                  },
                                                  state->song_info_container,
                                                  &(Layout_t){.offset_y = 0.02,
                                                              .width = 1.0,
                                                              .height = 0.025,
                                                              .relative_to = state->elapsed_time_text,
                                                              .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_RELATIVE_TO_Y |
                                                                       LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y});

    // Song name
    state->song_name_text = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){
            .text = song_get()->name, .font_type = FONT_UI, .em = 0.9, .color = {255, 255, 255, 255}, .draw_shadow = true},
        state->song_info_container,
        &(Layout_t){.offset_y = 0.05,
                    .relative_to = state->song_progressbar,
                    .flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y});
    ui_drawable_set_alpha_immediate(state->song_name_text, 200);

    // Song artist and album
    char *artist_album_text;
    asprintf(&artist_album_text, "%s - %s", song_get()->artist, song_get()->album);

    state->song_artist_album_text = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){
            .text = artist_album_text, .font_type = FONT_UI, .em = 0.7, .color = {255, 255, 255, 255}, .draw_shadow = true},
        state->song_info_container,
        &(Layout_t){.offset_y = 0.01,
                    .relative_to = state->song_name_text,
                    .flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y});
    ui_drawable_set_alpha_immediate(state->song_artist_album_text, 100);
    free(artist_album_text);

    // Song controls container
    state->song_controls_container =
        ui_make_container(state->ui, state->song_info_container,
                          &(Layout_t){.width = 1.0,
                                      .height = 0.15,
                                      .offset_y = 0.07,
                                      .relative_to = state->song_progressbar,
                                      .flags = LAYOUT_CENTER_X | LAYOUT_PROPORTIONAL_SIZE | LAYOUT_RELATIVE_TO_Y |
                                               LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y},
                          CONTAINER_NONE);

    // Play and pause buttons
    const unsigned char *play_bytes = incbin_play_img;
    const int play_bytes_len = sizeof incbin_play_img;
    state->play_button =
        ui_make_image(state->ui, play_bytes, play_bytes_len, &(Drawable_ImageData_t){0}, state->song_controls_container,
                      &(Layout_t){.offset_x = 0,
                                  .offset_y = 0,
                                  .width = 0.05,
                                  .flags = LAYOUT_SPECIAL_KEEP_ASPECT_RATIO | LAYOUT_CENTER | LAYOUT_PROPORTIONAL_W});

    const unsigned char *pause_bytes = incbin_pause_img;
    const int pause_bytes_len = sizeof incbin_pause_img;
    state->pause_button =
        ui_make_image(state->ui, pause_bytes, pause_bytes_len, &(Drawable_ImageData_t){0}, state->song_controls_container,
                      &(Layout_t){.offset_x = 0,
                                  .offset_y = 0,
                                  .width = 0.05,
                                  .flags = LAYOUT_SPECIAL_KEEP_ASPECT_RATIO | LAYOUT_CENTER | LAYOUT_PROPORTIONAL_W});
    state->pause_button->enabled = false;

    state->lyrics_view = ui_ex_make_lyrics_view(state->ui, state->right_container, song_get());

    // Help text on the bottom left
    // About how to show reading hints
    Drawable_t *temp_text =
        ui_make_text(state->ui,
                     &(Drawable_TextData_t){
                         .text = "R: Show/hide reading hints", .em = 0.5, .draw_shadow = true, .color = {255, 255, 255, 255}},
                     ui_root_container(state->ui),
                     &(Layout_t){.offset_y = -0.005,
                                 .offset_x = 0.005,
                                 .flags = LAYOUT_PROPORTIONAL_POS | LAYOUT_WRAP_AROUND_Y | LAYOUT_ANCHOR_BOTTOM_Y});
    ui_drawable_set_alpha_immediate(temp_text, 150);
    // About seeking with arrow keys
    temp_text = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){
            .text = "Arrow keys: Seek backward/forward", .em = 0.5, .draw_shadow = true, .color = {255, 255, 255, 255}},
        ui_root_container(state->ui),
        &(Layout_t){.offset_y = -0.001,
                    .flags = LAYOUT_PROPORTIONAL_Y | LAYOUT_ANCHOR_BOTTOM_Y | LAYOUT_RELATIVE_TO_POS,
                    .relative_to = temp_text});
    ui_drawable_set_alpha_immediate(temp_text, 150);
    // About using space to play/pause
    temp_text = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){.text = "Space: Play/pause", .em = 0.5, .draw_shadow = true, .color = {255, 255, 255, 255}},
        ui_root_container(state->ui),
        &(Layout_t){.offset_y = -0.001,
                    .flags = LAYOUT_PROPORTIONAL_Y | LAYOUT_ANCHOR_BOTTOM_Y | LAYOUT_RELATIVE_TO_POS,
                    .relative_to = temp_text});
    ui_drawable_set_alpha_immediate(temp_text, 150);
}

static void update_elapsed_text(const Karaoke_t *state) {
    const double elapsed = audio_elapsed_time();
    const int32_t minutes = (int32_t)(elapsed / 60);
    const int32_t seconds = (int32_t)elapsed % 60;

    char *time_str;
    asprintf(&time_str, "%.2d:%.2d", minutes, seconds);

    Drawable_TextData_t *custom_data = state->elapsed_time_text->custom_data;
    if ( strncmp(custom_data->text, time_str, 5) != 0 ) {
        free(custom_data->text);
        custom_data->text = time_str;
        ui_recompute_drawable(state->ui, state->elapsed_time_text);
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

    Drawable_TextData_t *custom_data = state->remaining_time_text->custom_data;
    if ( strcmp(time_str, custom_data->text) != 0 ) {
        free(custom_data->text);
        custom_data->text = time_str;
        ui_recompute_drawable(state->ui, state->remaining_time_text);
    } else {
        free(time_str);
    }
}

static void update_song_progressbar(const Karaoke_t *state) {
    if ( state->song_progressbar != NULL ) {
        const double progress = audio_elapsed_time() / audio_total_time();
        ((Drawable_ProgressBarData_t *)state->song_progressbar->custom_data)->progress = (float)progress;
    }
}

static void toggle_pause(const Karaoke_t *state) {
    if ( audio_is_paused() ) {
        audio_resume();
        state->lyrics_view->container->viewport_y = 0;
    } else
        audio_pause();
}

static void update_play_pause_state(const Karaoke_t *state) {
    const bool paused = audio_is_paused();
    state->play_button->enabled = paused;
    state->pause_button->enabled = !paused;
}

static void check_user_input(const Karaoke_t *state) {
    if ( events_key_was_pressed(KEY_SPACE) ) {
        toggle_pause(state);
    }
    if ( events_key_was_pressed(KEY_ARROW_LEFT) ) {
        audio_seek_relative(-5);
    } else if ( events_key_was_pressed(KEY_ARROW_RIGHT) ) {
        audio_seek_relative(+5);
    }

    int32_t mouse_x;
    // Check if the user clicked the progress bar
    Bounds_t progress_bar_bounds;
    if ( ui_mouse_clicked_drawable(state->song_progressbar, 10, &progress_bar_bounds, &mouse_x, NULL) ) {
        const double distance_from_x = mouse_x - progress_bar_bounds.x;
        const double distance = distance_from_x / state->song_progressbar->bounds.w;
        audio_seek(audio_total_time() * distance);
        // Reset viewport
        state->lyrics_view->container->viewport_y = 0;
    }
    // Check if clicked on the play/pause button
    // it doesn't matter which we choose because they're both at the same position with the same size
    if ( ui_mouse_clicked_drawable(state->play_button, 0, NULL, NULL, NULL) ) {
        toggle_pause(state);
    }

    if ( ui_mouse_hovering_container(state->song_info_container, NULL, NULL, NULL) ) {
        state->song_name_text->enabled = state->song_artist_album_text->enabled = false;
        state->song_controls_container->enabled = true;
    } else {
        const bool is_not_played = audio_elapsed_time() < 0.1 && audio_is_paused();
        state->song_name_text->enabled = state->song_artist_album_text->enabled = !is_not_played;
        state->song_controls_container->enabled = is_not_played;
    }

    if ( ui_mouse_hovering_container(state->lyrics_view->container, NULL, NULL, NULL) ) {
        ui_ex_lyrics_view_on_scroll(state->lyrics_view, events_get_mouse_scrolled());
    }
}

int karaoke_loop(const Karaoke_t *state) {
    events_loop();
    if ( events_has_quit() )
        return -1;
    audio_loop();

    // Check for user inputs
    check_user_input(state);

    ui_begin_loop(state->ui);
    // Recalculate dynamic elements
    update_elapsed_text(state);
    update_remaining_text(state);
    update_song_progressbar(state);
    update_play_pause_state(state);
    // Update the lyrics view
    if ( events_window_changed() )
        ui_ex_lyrics_view_on_screen_change(state->ui, state->lyrics_view);
    ui_ex_lyrics_view_loop(state->ui, state->lyrics_view);

    // Clear events after all checking has been done because under emscripten the events aren't polled inside glfw
    // so we would clear all the events before we could see them
    events_frame_end();

    ui_draw(state->ui);
    ui_end_loop();

    return 0;
}

void karaoke_finish(const Karaoke_t *state) {
    events_finish();
    ui_finish(state->ui);
    audio_finish();
}
