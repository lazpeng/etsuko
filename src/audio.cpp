#include "audio.h"

#include <SDL2/SDL_mixer.h>

int etsuko::Audio::initialize() {
    constexpr auto flags = MIX_INIT_MP3;
    if ( Mix_Init(flags) == 0 ) {
        std::puts("Mix_Init failed");
        std::puts(Mix_GetError());
        return -1;
    }

    if ( Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 2048) != 0 ) {
        std::puts("OpenAudio failed");
        std::puts(Mix_GetError());
        return -2;
    }

    return 0;
}

void etsuko::Audio::finalize() {
    Mix_FreeMusic(m_music);
    m_music = nullptr;
    Mix_Quit();
}

double etsuko::Audio::elapsed_time() const {
    return Mix_GetMusicPosition(m_music);
}

double etsuko::Audio::total_time() const {
    return m_total_time;
}

void etsuko::Audio::load_song(const std::string &path) {
    m_music = Mix_LoadMUS(path.c_str());
    if ( m_music == nullptr ) {
        std::puts(Mix_GetError());
        throw std::runtime_error("Failed to load song");
    }
    m_total_time = Mix_MusicDuration(m_music);
    Mix_PlayMusic(m_music, 1);
    Mix_PauseMusic();
}

void etsuko::Audio::pause() {
    if ( m_paused )
        return;

    Mix_PauseMusic();
    m_paused = true;
}

void etsuko::Audio::resume() {
    if ( m_paused ) {
        Mix_ResumeMusic();
        m_paused = false;
    }
}

void etsuko::Audio::seek(const double time) const {
    Mix_SetMusicPosition(time);
}

bool etsuko::Audio::is_paused() const {
    return m_paused;
}
