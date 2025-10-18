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
    constexpr auto DEFAULT_UI_FONT = "files/NotoSans-Regular.ttf";
    constexpr auto DEFAULT_LYRIC_FONT = "files/NotoSans_ExtraCondensed-Bold.ttf";

    namespace config {
        class Config {
        public:
            std::string ui_font_path, lyric_font_path, song_path;
            parser::Song song;

            static Config get_default();
        };
    }

    class Karaoke {
        Renderer m_renderer;
        std::shared_ptr<parser::Song> m_song;
        Audio m_audio;
        EventManager m_events;
        uint64_t m_current_ticks = 0;
        double m_delta_time = 0.0;
        config::Config m_config = config::Config::get_default();
        size_t m_current_active_index = 0;

        // Version text
        std::shared_ptr<renderer::BakedDrawable> m_text_version = {};
        // Music controls
        std::shared_ptr<renderer::BakedDrawable> m_elapsed_time = {};
        std::optional<renderer::VirtualContainer> m_song_info_container;
        std::optional<BoundingBox> m_progress_bar_box;
        // Lyrics
        std::optional<renderer::BakedDrawableScrollingLyricsContainer> m_lyrics_container;
        // Left panel
        std::optional<renderer::VerticalSplitContainer> m_left_container;
        std::shared_ptr<renderer::BakedDrawable> m_album_art;
        std::shared_ptr<renderer::BakedDrawable> m_song_name;
        std::shared_ptr<renderer::BakedDrawable> m_artist_name;

        void initialize_lyrics_container();

        void handle_input();

        void toggle_play_pause();
        void seek_to_time(double time) const;

        void draw_song_album_art();
        void draw_song_info();
        void draw_lyrics();
        void draw_song_controls();
        void draw_version();

    public:
        void initialize(const config::Config &config);
        void finalize();
        bool loop();
        void wait_vsync() const;
    };
} // namespace etsuko
