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

static etsuko_Load_t g_load_song;
static etsuko_Load_t g_load_ui_font;
static etsuko_Load_t g_load_lyrics_font;
static etsuko_Load_t g_load_audio;
static etsuko_Load_t g_load_album_art;

static bool g_loaded_override = false;

static uint64_t g_prev_ticks = 0;

static etsuko_Drawable_t *g_version_text = NULL;
static etsuko_Drawable_t *g_song_name_text = NULL;
static etsuko_Drawable_t *g_song_artist_album_text = NULL;
static etsuko_Drawable_t *g_elapsed_time_text = NULL;
static etsuko_Drawable_t *g_remaining_time_text = NULL;
static etsuko_Drawable_t *g_album_image = NULL;
static etsuko_Drawable_t *g_song_progressbar = NULL;
static etsuko_Drawable_t *g_play_button = NULL;
static etsuko_Drawable_t *g_pause_button = NULL;

static etsuko_Container_t *g_left_container = NULL;
static etsuko_Container_t *g_right_container = NULL;
static etsuko_Container_t *g_song_info_container = NULL;
static etsuko_Container_t *g_song_controls_container = NULL;

static etsuko_LyricsView_t *g_lyrics_view = NULL;

int karaoke_load_async(void) {
    etsuko_Config_t *config = config_get();
    // UI Font
    if ( g_load_ui_font.status == LOAD_NOT_STARTED ) {
        repository_get_resource(config->ui_font, "files", &g_load_ui_font);
    }
    if ( g_load_ui_font.status == LOAD_DONE ) {
        free(config->ui_font);
        config->ui_font = g_load_ui_font.destination;
        g_load_ui_font.status = LOAD_FINISHED;
    }
    // Lyrics font
    if ( g_load_lyrics_font.status == LOAD_NOT_STARTED ) {
        repository_get_resource(config->lyrics_font, "files", &g_load_lyrics_font);
    }
    if ( g_load_lyrics_font.status == LOAD_DONE ) {
        free(config->lyrics_font);
        config->lyrics_font = g_load_lyrics_font.destination;
        g_load_lyrics_font.status = LOAD_FINISHED;
    }
    // Song
    if ( g_load_song.status == LOAD_NOT_STARTED ) {
        repository_get_resource(config->song_file, NULL, &g_load_song);
    }
    if ( g_load_song.status == LOAD_DONE ) {
        free(config->song_file);
        config->song_file = g_load_song.destination;

        song_load(config->song_file);
        if ( song_get() == NULL )
            error_abort("Failed to load song");
        g_load_song.status = LOAD_FINISHED;
    } else if ( g_load_song.status != LOAD_FINISHED ) {
        return 0;
    }
    // Finish loading the song before we load the rest

    if ( song_get()->font_override != NULL && !g_loaded_override ) {
        config->lyrics_font = strdup(song_get()->font_override);
        g_loaded_override = true;
        repository_get_resource(config->lyrics_font, "files", &g_load_lyrics_font);
    }

    // Song audio file
    if ( g_load_audio.status == LOAD_NOT_STARTED ) {
        repository_get_resource(song_get()->file_path, NULL, &g_load_audio);
    }
    if ( g_load_audio.status == LOAD_DONE ) {
        free(song_get()->file_path);
        song_get()->file_path = g_load_audio.destination;
        g_load_audio.status = LOAD_FINISHED;
    }
    // Album art
    if ( g_load_album_art.status == LOAD_NOT_STARTED ) {
        repository_get_resource(song_get()->album_art_path, NULL, &g_load_album_art);
    }
    if ( g_load_album_art.status == LOAD_DONE ) {
        free(song_get()->album_art_path);
        song_get()->album_art_path = g_load_album_art.destination;
        g_load_album_art.status = LOAD_FINISHED;
    }

    return g_load_ui_font.status == LOAD_FINISHED && g_load_lyrics_font.status == LOAD_FINISHED &&
           g_load_song.status == LOAD_FINISHED && g_load_audio.status == LOAD_FINISHED &&
           g_load_album_art.status == LOAD_FINISHED;
}

void karaoke_init(void) {
    events_init();
    ui_init();
    audio_init();

    audio_load(song_get()->file_path);

    ui_load_font(FONT_UI, config_get()->ui_font);
    ui_load_font(FONT_LYRICS, config_get()->lyrics_font);
    ui_set_bg_color(song_get()->bg_color);

    char *window_title;
    asprintf(&window_title, "%s - %s", APP_NAME, song_get()->name);
    ui_set_window_title(window_title);
    free(window_title);

    const double vertical_padding = 0.01;

    // Make the left container
    g_left_container =
        ui_make_container(ui_root_container(), &(etsuko_Layout_t){.width = 0.5, .height = 1.0, .flags = LAYOUT_PROPORTIONAL_SIZE},
                          CONTAINER_VERTICAL_ALIGN_CONTENT);

    // Make the right container
    g_right_container = ui_make_container(ui_root_container(),
                                          &(etsuko_Layout_t){.width = 0.5,
                                                             .height = 0.7,
                                                             .offset_x = 0.5,
                                                             .offset_y = 0.35,
                                                             .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_PROPORTIONAL_POS},
                                          CONTAINER_NONE);

    // Version string
    g_version_text = ui_make_text(
        &(etsuko_Drawable_TextData_t){
            .text = "etsuko v" VERSION,
            .font_type = FONT_UI,
            .em = 0.8,
            .bold = false,
            .color = {255, 255, 255, 128},
        },
        ui_root_container(), &(etsuko_Layout_t){.offset_x = -1, .flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X});

    // Album art
    g_album_image = ui_make_image(
        &(etsuko_Drawable_ImageData_t){
            .file_path = song_get()->album_art_path,
            .border_radius_em = 4.0,
        },
        g_left_container,
        &(etsuko_Layout_t){.height = 0.6,
                           .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_CENTER_X | LAYOUT_SPECIAL_KEEP_ASPECT_RATIO});

    // Song info container
    g_song_info_container =
        ui_make_container(g_left_container,
                          &(etsuko_Layout_t){.height = 0.3,
                                             .offset_y = vertical_padding,
                                             .relative_to = g_album_image,
                                             .relative_to_size = g_album_image,
                                             .flags = LAYOUT_CENTER_X | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_RELATIVE_TO_Y |
                                                      LAYOUT_RELATIVE_TO_WIDTH | LAYOUT_PROPORTIONAL_H | LAYOUT_PROPORTIONAL_Y},
                          CONTAINER_NONE);

    // Elapsed time
    g_elapsed_time_text = ui_make_text(
        &(etsuko_Drawable_TextData_t){
            .text = "00:00",
            .font_type = FONT_UI,
            .em = 0.8,
            .bold = false,
            .color = {200, 200, 200, 255},
        },
        g_song_info_container, &(etsuko_Layout_t){0});

    // Remaining time
    g_remaining_time_text = ui_make_text(
        &(etsuko_Drawable_TextData_t){
            .text = "-00:00",
            .font_type = FONT_UI,
            .em = 0.8,
            .bold = false,
            .color = {200, 200, 200, 255},
        },
        g_song_info_container, &(etsuko_Layout_t){.offset_x = -1, .flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X});

    // Progress bar
    g_song_progressbar = ui_make_progressbar(
        &(etsuko_Drawable_ProgressBarData_t){
            .progress = 0,
            .fg_color = (etsuko_Color_t){.r = 255, .g = 255, .b = 255, .a = 255},
            .bg_color = (etsuko_Color_t){.r = 100, .g = 100, .b = 100, .a = 255},
        },
        g_song_info_container,
        &(etsuko_Layout_t){.offset_y = 0.02,
                           .width = 1.0,
                           .height = 0.025,
                           .relative_to = g_elapsed_time_text,
                           .flags = LAYOUT_PROPORTIONAL_SIZE | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT |
                                    LAYOUT_PROPORTIONAL_Y});

    // Song name
    g_song_name_text = ui_make_text(
        &(etsuko_Drawable_TextData_t){
            .text = song_get()->name,
            .font_type = FONT_UI,
            .em = 0.9,
            .bold = false,
            .color = {200, 200, 200, 255},
        },
        g_song_info_container,
        &(etsuko_Layout_t){.offset_y = 0.05,
                           .relative_to = g_song_progressbar,
                           .flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT |
                                    LAYOUT_PROPORTIONAL_Y});

    // Song artist and album
    char *artist_album_text;
    asprintf(&artist_album_text, "%s - %s", song_get()->artist, song_get()->album);

    g_song_artist_album_text = ui_make_text(
        &(etsuko_Drawable_TextData_t){
            .text = artist_album_text,
            .font_type = FONT_UI,
            .em = 0.7,
            .bold = false,
            .color = {150, 150, 150, 255},
        },
        g_song_info_container,
        &(etsuko_Layout_t){.offset_y = 0.01,
                           .relative_to = g_song_name_text,
                           .flags = LAYOUT_CENTER_X | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT |
                                    LAYOUT_PROPORTIONAL_Y});
    free(artist_album_text);

    // Song controls container
    g_song_controls_container =
        ui_make_container(g_song_info_container,
                          &(etsuko_Layout_t){.width = 1.0,
                                             .height = 0.15,
                                             .offset_y = 0.07,
                                             .relative_to = g_song_progressbar,
                                             .flags = LAYOUT_CENTER_X | LAYOUT_PROPORTIONAL_SIZE | LAYOUT_RELATIVE_TO_Y |
                                                      LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y},
                          CONTAINER_NONE);

    // Play and pause buttons
    g_play_button =
        ui_make_image(&(etsuko_Drawable_ImageData_t){.file_path = "assets/play.png"}, g_song_controls_container,
                      &(etsuko_Layout_t){.offset_x = 0,
                                         .offset_y = 0,
                                         .width = 0.05,
                                         .flags = LAYOUT_SPECIAL_KEEP_ASPECT_RATIO | LAYOUT_CENTER | LAYOUT_PROPORTIONAL_W});

    g_pause_button =
        ui_make_image(&(etsuko_Drawable_ImageData_t){.file_path = "assets/pause.png"}, g_song_controls_container,
                      &(etsuko_Layout_t){.offset_x = 0,
                                         .offset_y = 0,
                                         .width = 0.01,
                                         .flags = LAYOUT_SPECIAL_KEEP_ASPECT_RATIO | LAYOUT_CENTER | LAYOUT_PROPORTIONAL_W});
    g_pause_button->enabled = false;

    g_lyrics_view = ui_ex_make_lyrics_view(g_right_container, song_get());
}

static void update_elapsed_text(void) {
    const double elapsed = audio_elapsed_time();
    const int32_t minutes = (int32_t)(elapsed / 60);
    const int32_t seconds = (int32_t)elapsed % 60;

    char *time_str;
    asprintf(&time_str, "%.2d:%.2d", minutes, seconds);

    etsuko_Drawable_TextData_t *custom_data = g_elapsed_time_text->custom_data;
    if ( strncmp(custom_data->text, time_str, 5) != 0 ) {
        free(custom_data->text);
        custom_data->text = time_str;
        ui_recompute_drawable(g_elapsed_time_text);
    } else {
        free(time_str);
    }
}

static void update_remaining_text(void) {
    const double remaining = audio_total_time() - audio_elapsed_time();
    const int32_t minutes = (int32_t)(remaining / 60);
    const int32_t seconds = (int32_t)remaining % 60;
    char *time_str;
    asprintf(&time_str, "-%.2d:%.2d", minutes, seconds);

    etsuko_Drawable_TextData_t *custom_data = g_remaining_time_text->custom_data;
    if ( strncmp(time_str, custom_data->text, MAX_TEXT_SIZE) != 0 ) {
        free(custom_data->text);
        custom_data->text = time_str;
        ui_recompute_drawable(g_remaining_time_text);
    } else {
        free(time_str);
    }
}

static void update_song_progressbar(void) {
    if ( g_song_progressbar != NULL ) {
        const double progress = audio_elapsed_time() / audio_total_time();
        ((etsuko_Drawable_ProgressBarData_t *)g_song_progressbar->custom_data)->progress = (float)progress;
    }
}

static void toggle_pause(void) {
    if ( audio_is_paused() ) {
        audio_resume();
        g_lyrics_view->container->viewport_y = 0;
    } else
        audio_pause();
}

static void update_play_pause_state(void) {
    const bool paused = audio_is_paused();
    g_play_button->enabled = paused;
    g_pause_button->enabled = !paused;
}

static void check_user_input(void) {
    if ( events_key_was_pressed(KEY_SPACE) ) {
        toggle_pause();
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
            ui_get_drawable_canon_pos(g_song_progressbar, &progress_bar_x, &progress_bar_y);

            const int32_t padding_amount = 10;
            const double base_y = progress_bar_y - padding_amount;
            const double base_h = g_song_progressbar->bounds.h + padding_amount;

            const bool clicked_x = mouse_x >= progress_bar_x && mouse_x <= progress_bar_x + g_song_progressbar->bounds.w;
            const bool clicked_y = mouse_y >= base_y && mouse_y <= base_y + base_h;
            if ( clicked_x && clicked_y ) {
                const double distance_from_x = mouse_x - progress_bar_x;
                const double distance = distance_from_x / g_song_progressbar->bounds.w;
                audio_seek(audio_total_time() * distance);
            }
        }
        // Check if clicked on the play/pause button
        {
            double play_button_x, play_button_y;
            ui_get_drawable_canon_pos(g_play_button, &play_button_x, &play_button_y);
            const int32_t width = (int32_t)g_play_button->bounds.w, height = (int32_t)g_play_button->bounds.h;
            if ( events_mouse_was_clicked_inside_area((int32_t)play_button_x, (int32_t)play_button_y, width, height) ) {
                toggle_pause();
            }
        }
    }

    // TODO: Animate this fade-in/out
    events_get_mouse_position(&mouse_x, &mouse_y);
    {
        // Check if the mouse is inside the song name area
        double can_y;
        ui_get_drawable_canon_pos(g_song_name_text, NULL, &can_y);

        const double begin_x = g_song_info_container->bounds.x, begin_y = can_y;
        ui_get_drawable_canon_pos(g_song_artist_album_text, NULL, &can_y);
        const double end_x = begin_x + g_song_info_container->bounds.w, end_y = can_y + g_song_artist_album_text->bounds.h;

        const bool is_not_played = audio_elapsed_time() < 0.1 && audio_is_paused();
        const bool inside_area =
            is_not_played || (mouse_x >= begin_x && mouse_x <= end_x && mouse_y >= begin_y && mouse_y <= end_y);

        g_song_name_text->enabled = g_song_artist_album_text->enabled = !inside_area;
        g_song_controls_container->enabled = inside_area;
    }
    {
        // Check if the mouse is inside the lyric container
        double can_x, can_y;
        ui_get_container_canon_pos(g_lyrics_view->container, &can_x, &can_y);

        if ( mouse_x >= can_x && mouse_x <= can_x + g_lyrics_view->container->bounds.w && mouse_y >= can_y &&
             mouse_y <= can_y + g_lyrics_view->container->bounds.h ) {
            const double scrolled = events_get_mouse_scrolled();
            ui_ex_lyrics_view_on_scroll(g_lyrics_view, scrolled);
        }
    }
}

int karaoke_loop(void) {
    const uint64_t ticks = SDL_GetTicks64();
    const double delta = g_prev_ticks != 0 ? (double)(ticks - g_prev_ticks) / 1000.0 : 0;
    g_prev_ticks = ticks;

    events_set_window_pixel_scale(render_get_pixel_scale());

    events_loop();
    if ( events_has_quit() )
        return -1;
    audio_loop();

    if ( events_window_changed() )
        ui_on_window_changed();

    // Check for user inputs
    check_user_input();

    // Recalculate dynamic elements
    update_elapsed_text();
    update_remaining_text();
    update_song_progressbar();
    update_play_pause_state();
    // Update the lyrics view
    ui_ex_lyrics_view_loop(g_lyrics_view);

    ui_begin_loop(delta);
    ui_draw();
    ui_end_loop();

    return 0;
}

void karaoke_finish(void) {
    events_finish();
    ui_finish();
    audio_finish();
}
