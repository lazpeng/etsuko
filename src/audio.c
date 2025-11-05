#include "audio.h"

#include <SDL_mixer.h>

#include "error.h"

static Mix_Music *g_music = nullptr;
static double g_music_total_time = 0;
static bool g_paused = true;
static bool g_stopped = true;

void audio_init() {
    constexpr int32_t flags = MIX_INIT_MP3;
    if ( Mix_Init(flags) == 0 ) {
        puts(Mix_GetError());
        error_abort("Mix_Init failed");
    }

    if ( Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 2048) != 0 ) {
        puts(Mix_GetError());
        error_abort("OpenAudio failed");
    }
}

void audio_finish() {
    if ( g_music != nullptr ) {
        Mix_FreeMusic(g_music);
        g_music = nullptr;
    }
    Mix_Quit();
}

static void reset() {
    audio_resume();
    audio_pause();
}

void audio_load(const char *file) {
    g_music = Mix_LoadMUS(file);
    if ( g_music == nullptr ) {
        puts(Mix_GetError());
        error_abort("Failed to load song");
    }
    g_music_total_time = Mix_MusicDuration(g_music);
    reset();
}

void audio_resume() {
    if ( g_stopped ) {
        Mix_PlayMusic(g_music, 0);
        g_stopped = g_paused = false;
    } else if ( g_paused ) {
        if ( audio_elapsed_time() >= audio_total_time() ) {
            Mix_RewindMusic();
        } else {
            Mix_ResumeMusic();
        }
        g_paused = false;
    }
}

void audio_pause() {
    if ( g_paused )
        return;

    Mix_PauseMusic();
    g_paused = true;
}

void audio_seek(const double time) {
    if ( g_stopped ) {
        reset();
    }
    Mix_SetMusicPosition(time);
}

void audio_seek_relative(const double diff) {
    const double new_time = audio_elapsed_time() + diff;
    audio_seek(new_time);
}

double audio_elapsed_time() { return Mix_GetMusicPosition(g_music); }

double audio_total_time() { return g_music_total_time; }

bool audio_is_paused() { return g_paused || g_stopped; }

void audio_loop() {
    if ( audio_elapsed_time() >= audio_total_time() && !g_stopped ) {
        g_stopped = true;
    }
}
