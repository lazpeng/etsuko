#include "karaoke.h"
#include "audio.h"
#include "config.h"
#include "error.h"
#include "events.h"
#include "repository.h"
#include "song.h"
#include "ui.h"
#include "ui_ex.h"
#include <SDL2/SDL.h>
#include <stdlib.h>

struct Karaoke_t {
    Ui_t *ui;
    Load_t load_song;
    Load_t load_ui_font;
    Load_t load_lyrics_font;
    Load_t load_audio;
    Load_t load_album_art;
    bool loaded_override;
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
};

Karaoke_t *karaoke_init() {
    Karaoke_t *karaoke = calloc(1, sizeof(*karaoke));
    if ( karaoke == nullptr )
        error_abort("Failed to allocate memory for karaoke.");

    karaoke->ui = ui_init();
    if ( karaoke->ui == nullptr )
        error_abort("Failed to initialize UI.");
    return karaoke;
}

int karaoke_load_async(Karaoke_t *state) {
    Config_t *config = config_get();
    // UI Font
    if ( state->load_ui_font.status == LOAD_NOT_STARTED ) {
        repository_get_resource(config->ui_font, "files", &state->load_ui_font);
    }
    if ( state->load_ui_font.status == LOAD_DONE ) {
        free(config->ui_font);
        config->ui_font = state->load_ui_font.destination;
        state->load_ui_font.status = LOAD_FINISHED;

        ui_load_font(FONT_UI, state->load_ui_font.destination);
    }
    // Lyrics font
    if ( state->load_lyrics_font.status == LOAD_NOT_STARTED ) {
        repository_get_resource(config->lyrics_font, "files", &state->load_lyrics_font);
    }
    if ( state->load_lyrics_font.status == LOAD_DONE ) {
        free(config->lyrics_font);
        config->lyrics_font = state->load_lyrics_font.destination;
        state->load_lyrics_font.status = LOAD_FINISHED;

        ui_load_font(FONT_LYRICS, state->load_lyrics_font.destination);
    }
    // Song
    if ( state->load_song.status == LOAD_NOT_STARTED ) {
        repository_get_resource(config->song_file, nullptr, &state->load_song);
    }
    if ( state->load_song.status == LOAD_DONE ) {
        free(config->song_file);
        config->song_file = state->load_song.destination;

        song_load(config->song_file);
        if ( song_get() == nullptr )
            error_abort("Failed to load song");
        state->load_song.status = LOAD_FINISHED;

        if ( song_get()->bg_type == BG_SOLID ) {
            ui_set_bg_color(song_get()->bg_color);
        } else if ( song_get()->bg_type == BG_SIMPLE_GRADIENT ) {
            ui_set_bg_gradient(song_get()->bg_color, song_get()->bg_color_secondary);
        }
    } else if ( state->load_song.status != LOAD_FINISHED ) {
        return 0;
    }
    // Finish loading the song before we load the rest

    if ( song_get()->font_override != nullptr && !state->loaded_override ) {
        config->lyrics_font = strdup(song_get()->font_override);
        state->loaded_override = true;
        repository_get_resource(config->lyrics_font, "files", &state->load_lyrics_font);
    }

    // Song audio file
    if ( state->load_audio.status == LOAD_NOT_STARTED ) {
        repository_get_resource(song_get()->file_path, nullptr, &state->load_audio);
    }
    if ( state->load_audio.status == LOAD_DONE ) {
        free(song_get()->file_path);
        song_get()->file_path = state->load_audio.destination;
        state->load_audio.status = LOAD_FINISHED;
    }
    // Album art
    if ( state->load_album_art.status == LOAD_NOT_STARTED ) {
        repository_get_resource(song_get()->album_art_path, nullptr, &state->load_album_art);
    }
    if ( state->load_album_art.status == LOAD_DONE ) {
        free(song_get()->album_art_path);
        song_get()->album_art_path = state->load_album_art.destination;
        state->load_album_art.status = LOAD_FINISHED;
    }

    return state->load_ui_font.status == LOAD_FINISHED && state->load_lyrics_font.status == LOAD_FINISHED &&
           state->load_song.status == LOAD_FINISHED && state->load_audio.status == LOAD_FINISHED &&
           state->load_album_art.status == LOAD_FINISHED;
}

void karaoke_setup(Karaoke_t *state) {
    events_init();
    ui_init();
    audio_init();

    audio_load(song_get()->file_path);

    char *window_title;
    asprintf(&window_title, "%s - %s", APP_NAME, song_get()->name);
    ui_set_window_title(window_title);
    free(window_title);

    constexpr double vertical_padding = 0.01;

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
                                           .bold = false,
                                           .color = {255, 255, 255, 128},
                                       },
                                       ui_root_container(state->ui),
                                       &(Layout_t){.offset_x = -1, .flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X});

    // Album art
    state->album_image = ui_make_image(
        state->ui,
        &(Drawable_ImageData_t){
            .file_path = song_get()->album_art_path,
            .border_radius_em = 4.0,
        },
        state->left_container,
        &(Layout_t){.height = 0.6, .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_CENTER_X | LAYOUT_SPECIAL_KEEP_ASPECT_RATIO});

    // Song info container
    state->song_info_container =
        ui_make_container(state->ui, state->left_container,
                          &(Layout_t){.height = 0.3,
                                      .offset_y = vertical_padding,
                                      .relative_to = state->album_image,
                                      .relative_to_size = state->album_image,
                                      .flags = LAYOUT_CENTER_X | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_RELATIVE_TO_Y |
                                               LAYOUT_RELATIVE_TO_WIDTH | LAYOUT_PROPORTIONAL_H | LAYOUT_PROPORTIONAL_Y},
                          CONTAINER_NONE);

    // Elapsed time
    state->elapsed_time_text = ui_make_text(state->ui,
                                            &(Drawable_TextData_t){
                                                .text = "00:00",
                                                .font_type = FONT_UI,
                                                .em = 0.8,
                                                .bold = false,
                                                .color = {200, 200, 200, 255},
                                            },
                                            state->song_info_container, &(Layout_t){0});

    // Remaining time
    state->remaining_time_text = ui_make_text(state->ui,
                                              &(Drawable_TextData_t){
                                                  .text = "-00:00",
                                                  .font_type = FONT_UI,
                                                  .em = 0.8,
                                                  .bold = false,
                                                  .color = {200, 200, 200, 255},
                                              },
                                              state->song_info_container,
                                              &(Layout_t){.offset_x = -1, .flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X});

    // Progress bar
    state->song_progressbar = ui_make_progressbar(state->ui,
                                                  &(Drawable_ProgressBarData_t){
                                                      .progress = 0,
                                                      .border_radius_em = 0.4,
                                                      .fg_color = (Color_t){.r = 255, .g = 255, .b = 255, .a = 255},
                                                      .bg_color = (Color_t){.r = 100, .g = 100, .b = 100, .a = 255},
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
            .text = song_get()->name,
            .font_type = FONT_UI,
            .em = 0.9,
            .bold = false,
            .color = {200, 200, 200, 255},
        },
        state->song_info_container,
        &(Layout_t){.offset_y = 0.05,
                    .relative_to = state->song_progressbar,
                    .flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y});

    // Song artist and album
    char *artist_album_text;
    asprintf(&artist_album_text, "%s - %s", song_get()->artist, song_get()->album);

    state->song_artist_album_text = ui_make_text(
        state->ui,
        &(Drawable_TextData_t){
            .text = artist_album_text,
            .font_type = FONT_UI,
            .em = 0.7,
            .bold = false,
            .color = {150, 150, 150, 255},
        },
        state->song_info_container,
        &(Layout_t){.offset_y = 0.01,
                    .relative_to = state->song_name_text,
                    .flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y});
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
    state->play_button =
        ui_make_image(state->ui, &(Drawable_ImageData_t){.file_path = "assets/play.png"}, state->song_controls_container,
                      &(Layout_t){.offset_x = 0,
                                  .offset_y = 0,
                                  .width = 0.05,
                                  .flags = LAYOUT_SPECIAL_KEEP_ASPECT_RATIO | LAYOUT_CENTER | LAYOUT_PROPORTIONAL_W});

    state->pause_button =
        ui_make_image(state->ui, &(Drawable_ImageData_t){.file_path = "assets/pause.png"}, state->song_controls_container,
                      &(Layout_t){.offset_x = 0,
                                  .offset_y = 0,
                                  .width = 0.05,
                                  .flags = LAYOUT_SPECIAL_KEEP_ASPECT_RATIO | LAYOUT_CENTER | LAYOUT_PROPORTIONAL_W});
    state->pause_button->enabled = false;

    state->lyrics_view = ui_ex_make_lyrics_view(state->ui, state->right_container, song_get());
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
    if ( strncmp(time_str, custom_data->text, MAX_TEXT_SIZE) != 0 ) {
        free(custom_data->text);
        custom_data->text = time_str;
        ui_recompute_drawable(state->ui, state->remaining_time_text);
    } else {
        free(time_str);
    }
}

static void update_song_progressbar(const Karaoke_t *state) {
    if ( state->song_progressbar != nullptr ) {
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
    int32_t mouse_x, mouse_y;
    // Handle mouse clicks
    if ( events_get_mouse_click(&mouse_x, &mouse_y) ) {
        // Check if the user clicked on the progress bar
        {
            double progress_bar_x, progress_bar_y;
            ui_get_drawable_canon_pos(state->ui, state->song_progressbar, &progress_bar_x, &progress_bar_y);

            constexpr int32_t padding_amount = 10;
            const double base_y = progress_bar_y - padding_amount;
            const double base_h = state->song_progressbar->bounds.h + padding_amount;

            const bool clicked_x = mouse_x >= progress_bar_x && mouse_x <= progress_bar_x + state->song_progressbar->bounds.w;
            const bool clicked_y = mouse_y >= base_y && mouse_y <= base_y + base_h;
            if ( clicked_x && clicked_y ) {
                const double distance_from_x = mouse_x - progress_bar_x;
                const double distance = distance_from_x / state->song_progressbar->bounds.w;
                audio_seek(audio_total_time() * distance);
            }
        }
        // Check if clicked on the play/pause button
        {
            double play_button_x, play_button_y;
            ui_get_drawable_canon_pos(state->ui, state->play_button, &play_button_x, &play_button_y);
            const int32_t width = (int32_t)state->play_button->bounds.w, height = (int32_t)state->play_button->bounds.h;
            if ( events_mouse_was_clicked_inside_area((int32_t)play_button_x, (int32_t)play_button_y, width, height) ) {
                toggle_pause(state);
            }
        }
    }

    events_get_mouse_position(&mouse_x, &mouse_y);
    {
        // Check if the mouse is inside the song name area
        double can_y;
        ui_get_drawable_canon_pos(state->ui, state->song_name_text, nullptr, &can_y);

        const double begin_x = state->song_info_container->bounds.x, begin_y = can_y;
        ui_get_drawable_canon_pos(state->ui, state->song_artist_album_text, nullptr, &can_y);
        const double end_x = begin_x + state->song_info_container->bounds.w,
                     end_y = can_y + state->song_artist_album_text->bounds.h;

        const bool is_not_played = audio_elapsed_time() < 0.1 && audio_is_paused();
        const bool inside_area =
            is_not_played || (mouse_x >= begin_x && mouse_x <= end_x && mouse_y >= begin_y && mouse_y <= end_y);

        state->song_name_text->enabled = state->song_artist_album_text->enabled = !inside_area;
        state->song_controls_container->enabled = inside_area;
    }
    {
        // Check if the mouse is inside the lyric container
        double can_x, can_y;
        ui_get_container_canon_pos(state->ui, state->lyrics_view->container, &can_x, &can_y);

        if ( mouse_x >= can_x && mouse_x <= can_x + state->lyrics_view->container->bounds.w && mouse_y >= can_y &&
             mouse_y <= can_y + state->lyrics_view->container->bounds.h ) {
            const double scrolled = events_get_mouse_scrolled();
            ui_ex_lyrics_view_on_scroll(state->lyrics_view, scrolled);
        }
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
    ui_ex_lyrics_view_loop(state->ui, state->lyrics_view);

    ui_draw(state->ui);
    ui_end_loop();

    return 0;
}

void karaoke_finish(const Karaoke_t *state) {
    events_finish();
    ui_finish(state->ui);
    render_finish();
    audio_finish();
}
