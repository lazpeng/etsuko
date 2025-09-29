/**
 * audio.h - Controls initialization, playback and queries information about the audio device
 */

#pragma once

#include <SDL_mixer.h>
#include <string>

namespace etsuko {
    class Audio {
        Mix_Music *m_music = nullptr;
        bool m_paused = true;

    public:
        int initialize();

        void finalize();

        void load_song(const std::string& path);
        void pause();
        void resume();
        void seek(double time) const;

        [[nodiscard]] double elapsed_time() const;
        [[nodiscard]] double total_time() const;
        [[nodiscard]] bool is_paused() const;
    };
} // namespace etsuko
