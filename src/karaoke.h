/**
 * Karaoke.h - Main loop and orchestration of the application
 */

#pragma once

#include <SDL2/SDL.h>

#include "audio.h"
#include "events.h"
#include "parser.h"
#include "renderer.h"
#include "renderer_ex.h"
#include "repository.h"

namespace etsuko {
    constexpr auto DEFAULT_FONT = "files/NotoSansJP-VF.ttf";

    namespace config {
        class Config {
        public:
            std::string font_path;
            int font_index;
            std::string song_path;

            static Config get_default();
        };

        enum class TaskStatusEnum {
            NONE = 0,
            WORKING,
            DONE,
            ERROR
        };
    }

    class Karaoke {
        Renderer m_renderer;
        parser::Song m_song = {};
        Audio m_audio;
        EventManager m_events;
        uint64_t m_current_ticks = 0;
        config::Config m_config = config::Config::get_default();
        size_t m_current_active_index = 0;
        bool m_initialized = false;

        // Version text
        std::shared_ptr<renderer::BakedDrawable> m_text_version = {};
        // Music controls
        std::shared_ptr<renderer::BakedDrawable> m_play_button = {};
        std::shared_ptr<renderer::BakedDrawable> m_elapsed_time = {};
        // Lyrics
        std::optional<renderer::BakedDrawableScrollingContainer<parser::TimedLyric>> m_lyrics_container;
        // Left panel
        std::optional<renderer::VerticalSplitContainer> m_left_container;
        std::shared_ptr<renderer::BakedDrawable> m_album_art;
        std::shared_ptr<renderer::BakedDrawable> m_song_name;
        std::shared_ptr<renderer::BakedDrawable> m_song_name_translated;
        std::shared_ptr<renderer::BakedDrawable> m_artist_name;
        std::shared_ptr<renderer::BakedDrawable> m_album_name;
        std::shared_ptr<renderer::BakedDrawable> m_song_year;

        /** Tasks */
        repository::LoadJob m_load_font = {}, m_load_song = {}, m_load_audio = {}, m_load_art = {};

        void async_initialize_loop();
        void initialize_lyrics_container();

        void draw_song_album_art();
        void draw_song_info();
        void draw_lyrics();
        void draw_song_controls();
        void draw_version();

    public:
        int initialize();
        void finalize();
        bool loop();
        void wait_vsync() const;
    };
} // namespace etsuko
