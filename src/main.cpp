#include <cstdlib>
#include <iostream>

#include "karaoke.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static bool g_initialized = false;
static etsuko::config::Config g_config;

static etsuko::repository::LoadJob g_ui_font_job;
static etsuko::repository::LoadJob g_lyric_font_job;
static etsuko::repository::LoadJob g_song_job;
static etsuko::repository::LoadJob g_audio_job;
static etsuko::repository::LoadJob g_art_job;

static bool async_load_assets() {
    if ( g_ui_font_job.status == etsuko::repository::LoadJob::NOT_STARTED ) {
        etsuko::Repository::get_resource(g_config.ui_font_path, &g_ui_font_job);
    }
    if ( g_ui_font_job.status == etsuko::repository::LoadJob::DONE ) {
        g_config.ui_font_path = g_ui_font_job.result_path;
    }

    if ( g_lyric_font_job.status == etsuko::repository::LoadJob::NOT_STARTED ) {
        etsuko::Repository::get_resource(g_config.lyric_font_path, &g_lyric_font_job);
    }
    if ( g_lyric_font_job.status == etsuko::repository::LoadJob::DONE ) {
        g_config.lyric_font_path = g_lyric_font_job.result_path;
    }

    // Load song before we can load the album art and audio
    if ( g_song_job.status == etsuko::repository::LoadJob::NOT_STARTED ) {
        etsuko::Repository::get_resource(g_config.song_path, &g_song_job);
        return false;
    }
    if ( g_song_job.status == etsuko::repository::LoadJob::DONE ) {
        g_config.song_path = g_song_job.result_path;
        g_config.song = etsuko::Parser::parse(g_config.song_path);
    } else
        return false;

    if ( g_art_job.status == etsuko::repository::LoadJob::NOT_STARTED ) {
        etsuko::Repository::get_resource(g_config.song.album_art_path, &g_art_job);
    }
    if ( g_art_job.status == etsuko::repository::LoadJob::DONE ) {
        g_config.song.album_art_path = g_art_job.result_path;
    }

    if ( g_audio_job.status == etsuko::repository::LoadJob::NOT_STARTED ) {
        etsuko::Repository::get_resource(g_config.song.file_path, &g_audio_job);
    }
    if ( g_audio_job.status == etsuko::repository::LoadJob::DONE ) {
        g_config.song.file_path = g_audio_job.result_path;
    }

    return
        g_ui_font_job.status == etsuko::repository::LoadJob::DONE &&
        g_lyric_font_job.status == etsuko::repository::LoadJob::DONE &&
        g_song_job.status == etsuko::repository::LoadJob::DONE &&
        g_audio_job.status == etsuko::repository::LoadJob::DONE &&
        g_art_job.status == etsuko::repository::LoadJob::DONE;
}

static void main_loop(void *param) {
    const auto karaoke = static_cast<etsuko::Karaoke *>(param);

    try {
        if ( !g_initialized ) {
            if ( !async_load_assets() ) {
                return;
            }
            karaoke->initialize(g_config);
            g_initialized = true;
        }
        karaoke->loop();
    } catch ( std::exception &e ) {
        std::puts(e.what());
        std::puts("error ocurred");
    }
}

int main() {
    g_config = etsuko::config::Config::get_default();
    etsuko::Karaoke karaoke;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(main_loop, &karaoke, 0, true);
#else
    do {
    } while ( !async_load_assets() );

    karaoke.initialize(g_config);
    while ( !karaoke.loop() ) {
        karaoke.wait_vsync();
    }
#endif

    return EXIT_SUCCESS;
}
