#include "karaoke.h"

#include <SDL2/SDL.h>
#include <format>
#include <iostream>

etsuko::config::Config etsuko::config::Config::get_default() {
    return {
        .ui_font_path = DEFAULT_UI_FONT,
        .lyric_font_path = DEFAULT_LYRIC_FONT,
        .font_index = 0,
        .song_path = "all these things that ive done.txt"
    };
}

int etsuko::Karaoke::initialize() {
    if ( m_audio.initialize() != 0 ) {
        std::puts("Failed to initialize audio");
        return -1;
    }

    if ( m_renderer.initialize() != 0 ) {
        std::puts("Failed to initialize renderer");
        return -2;
    }

    // Begin initialization
    async_initialize_loop();
    return 0;
}

void etsuko::Karaoke::initialize_lyrics_container() {
    constexpr renderer::ScrollingLyricsContainerOpts scroll_opts = {
        .margin_top_percent = 0.4,
        .vertical_padding_percent = 0.025,
        .active_padding_percent = 0.1,
        .alignment = renderer::ScrollingLyricsContainerOpts::ALIGN_CENTER
    };
    m_lyrics_container = renderer::BakedDrawableScrollingLyricsContainer(&m_renderer, m_song, false, m_renderer.root_container(), scroll_opts);
}

void etsuko::Karaoke::async_initialize_loop() {
    // Check loading of the ui font
    switch ( m_load_ui_font.status ) {
    case repository::LoadJob::NOT_STARTED:
        Repository::get_resource(m_config.ui_font_path, &m_load_ui_font);
        break;
    case repository::LoadJob::DONE:
        if ( m_load_ui_font.error.empty() ) {
            m_renderer.load_font(m_load_ui_font.result_path, renderer::TextOpts::FONT_UI);
        } else {
            std::puts("Failed to load font");
            std::puts(m_load_ui_font.error.c_str());
            // TODO: Find a way to quit, or just keep running forever?
        }

        m_load_ui_font.status = repository::LoadJob::NONE;
    default:
        break;
    }

    // lyrics font
    switch ( m_load_lyric_font.status ) {
    case repository::LoadJob::NOT_STARTED:
        Repository::get_resource(m_config.lyric_font_path, &m_load_lyric_font);
        break;
    case repository::LoadJob::DONE:
        if ( m_load_lyric_font.error.empty() ) {
            m_renderer.load_font(m_load_lyric_font.result_path, renderer::TextOpts::FONT_LYRIC);
        } else {
            std::puts("Failed to load font");
            std::puts(m_load_lyric_font.error.c_str());
        }

        m_load_lyric_font.status = repository::LoadJob::NONE;
    default:
        break;
    }

    // TODO: Treat errors better
    // Load the base song definition
    switch ( m_load_song.status ) {
    case repository::LoadJob::NOT_STARTED:
        Repository::get_resource(m_config.song_path, &m_load_song);
        break;
    case repository::LoadJob::DONE:
        m_song = std::make_shared<parser::Song>(Parser::parse(m_load_song.result_path));
        initialize_song_opts();
        m_load_song.status = repository::LoadJob::NONE;
        break;
    default:
        break;
    }

    // Load other parts of the song
    if ( m_load_song.status == repository::LoadJob::NONE ) {
        // Load the actual song file
        if ( m_load_audio.status == repository::LoadJob::NOT_STARTED ) {
            Repository::get_resource(m_song->file_path, &m_load_audio);
        } else if ( m_load_audio.status == repository::LoadJob::DONE ) {
            m_audio.load_song(m_load_audio.result_path);
            m_load_audio.status = repository::LoadJob::NONE;
        }

        // Load the album art
        if ( m_load_art.status == repository::LoadJob::NOT_STARTED ) {
            Repository::get_resource(m_song->album_art_path, &m_load_art);
        } else if ( m_load_art.status == repository::LoadJob::DONE ) {
            m_song->album_art_path = m_load_art.result_path;
        }

        if ( m_load_audio.status == repository::LoadJob::NONE ) {
            m_initialized = m_load_ui_font.status == repository::LoadJob::NONE && m_load_lyric_font.status == repository::LoadJob::NONE;

            if ( m_initialized && !m_lyrics_container.has_value() ) {
                initialize_lyrics_container();
            }
        }
    }
}

void etsuko::Karaoke::initialize_song_opts() {
    if ( !m_song->bg_color.empty() ) {
        uint8_t r, g, b;
        if ( m_song->bg_color.size() == 6 ) {
            const auto color = static_cast<int32_t>(std::strtol(m_song->bg_color.data(), nullptr, 16));
            b = color & 0xFF;
            g = (color >> 8) & 0xFF;
            r = (color >> 16) & 0xFF;
        } else {
            throw std::runtime_error("Invalid bg color");
        }
        const renderer::Color color = {.r = r, .g = g, .b = b, .a = 255};
        m_renderer.set_bg_color(color);
    }
}

void etsuko::Karaoke::draw_version() {
    if ( m_text_version == nullptr || !m_text_version->is_valid() ) {
        constexpr renderer::Color color = {.r = 255, .g = 255, .b = 255, .a = 128};
        const renderer::TextOpts opts = {
            .text = std::format("etsuko v{}", VERSION_STRING),
            .position = {
                .x = -1,
                .y = 0,
                .flags = renderer::Point::ANCHOR_RIGHT_X
            },
            .em_size = 0.8,
            .bold = false,
            .color = color
        };
        m_text_version = m_renderer.draw_text_baked(opts, *m_renderer.root_container());
    }

    m_renderer.render_baked(*m_text_version);
}

void etsuko::Karaoke::draw_song_album_art() {
    if ( !m_left_container.has_value() ) {
        m_left_container = renderer::VerticalSplitContainer(true, m_renderer.root_container());
    }

    if ( m_album_art == nullptr || !m_album_art->is_valid() ) {
        const renderer::ImageOpts opts = {
            .position = {.x = 0, .y = -50, .flags = renderer::Point::CENTERED},
            .h = static_cast<int32_t>(m_left_container->get_bounds().h * 0.7),
            .resource_path = m_song->album_art_path,
        };

        m_album_art = m_renderer.draw_image_baked(opts, *m_left_container);
    }

    m_renderer.render_baked(*m_album_art, *m_left_container);
}

void etsuko::Karaoke::draw_song_info() {
    if ( m_album_art != nullptr && m_album_art->is_valid() && m_progress_bar_box.has_value() ) {
        if ( !m_song_info_container.has_value() ) {
            const auto y = m_album_art->bounds().y + m_album_art->bounds().h + 10;
            const auto height = m_left_container->get_bounds().h - y;
            const BoundingBox box = {.x = m_album_art->bounds().x, .y = y, .w = m_album_art->bounds().w, .h = height};
            m_song_info_container = renderer::VirtualContainer(*m_left_container, box);
        }

        int32_t mouse_x, mouse_y;
        m_events.get_mouse_position(&mouse_x, &mouse_y);

        if ( m_song_info_container->get_bounds().is_inside_of(mouse_x, mouse_y) || m_audio.is_paused() ) {
            return;
        }

        auto y = m_progress_bar_box->y + m_progress_bar_box->h - m_song_info_container->get_bounds().y + 20;
        if ( m_song_name == nullptr || !m_song_name->is_valid() ) {
            const renderer::TextOpts name_opt = {
                .text = m_song->name,
                .position = {.x = 0, .y = y, .flags = renderer::Point::CENTERED_X},
                .em_size = 0.9,
                .bold = false,
                .color = renderer::Color::white().darken()
            };
            m_song_name = m_renderer.draw_text_baked(name_opt, *m_song_info_container);
        }
        m_renderer.render_baked(*m_song_name, *m_song_info_container);

        constexpr renderer::Color grey = {.r = 150, .g = 150, .b = 150, .a = 200};
        y += m_song_name->bounds().h + 10;
        const auto artist_album_text = std::format("{} - {}", m_song->artist, m_song->album);
        const renderer::TextOpts artist_opt = {
            .text = artist_album_text,
            .position = {.x = 0, .y = y, .flags = renderer::Point::CENTERED_X},
            .em_size = 0.8,
            .bold = false,
            .color = grey
        };
        m_artist_name = m_renderer.draw_text_baked(artist_opt, *m_song_info_container);
        m_renderer.render_baked(*m_artist_name, *m_song_info_container);
    }
}

void etsuko::Karaoke::draw_lyrics() {
    if ( m_lyrics_container.has_value() ) {
        m_lyrics_container->loop(m_events, m_delta_time, m_audio.elapsed_time());
        m_lyrics_container->draw(m_renderer, !m_audio.is_paused());
    }
}

void etsuko::Karaoke::draw_song_controls() {
    if ( m_album_art != nullptr && m_album_art->is_valid() ) {
        // Time texts
        const auto elapsed = m_audio.elapsed_time();
        const auto remaining = m_audio.total_time() - elapsed;
        int32_t minutes = static_cast<int32_t>(remaining) / 60;
        int32_t seconds = static_cast<int32_t>(remaining) % 60;
        const auto remaining_time_text = std::format("-{:02d}:{:02d}", minutes, seconds);
        minutes = static_cast<int32_t>(elapsed) / 60;
        seconds = static_cast<int32_t>(elapsed) % 60;
        const auto elapsed_time_text = std::format("{:02d}:{:02d}", minutes, seconds);

        int32_t x = m_album_art->bounds().x, y = m_album_art->bounds().y + m_album_art->bounds().h + 10;

        const renderer::TextOpts elapsed_text_opt = {
            .text = elapsed_time_text,
            .position = {
                .x = x,
                .y = y
            },
            .em_size = 0.8,
            .bold = false
        };
        m_elapsed_time = m_renderer.draw_text_baked(elapsed_text_opt, *m_renderer.root_container());
        m_renderer.render_baked(*m_elapsed_time);

        x = m_album_art->bounds().x + m_album_art->bounds().w;
        const renderer::TextOpts remaining_text_opt = {
            .text = remaining_time_text,
            .position = {
                .x = x,
                .y = y,
                .flags = renderer::Point::ANCHOR_RIGHT_X
            },
            .em_size = 0.8,
            .bold = false
        };
        m_elapsed_time = m_renderer.draw_text_baked(remaining_text_opt, *m_renderer.root_container());
        m_renderer.render_baked(*m_elapsed_time);
    }

    // Play button
    if ( m_album_art == nullptr || !m_album_art->is_valid() ) {
        return;
    }

    const auto x = m_album_art->bounds().x, y = m_elapsed_time->bounds().y + m_elapsed_time->bounds().h + 45;

    int32_t mouse_x, mouse_y;
    m_events.get_mouse_position(&mouse_x, &mouse_y);

    if ( m_song_info_container.has_value() && (m_audio.is_paused() || m_song_info_container->get_bounds().is_inside_of(mouse_x, mouse_y)) ) {
        const auto button = renderer::ButtonOpts{
            .label_icon = m_audio.is_paused() ? renderer::ButtonOpts::LABEL_ICON_PLAY : renderer::ButtonOpts::LABEL_ICON_PAUSE,
            .icon_size = 32,
            .position = {.x = x, .y = y, .flags = renderer::Point::CENTERED_X},
            .draw_border = false,
            .vertical_padding = 10,
            .horizontal_padding = 10,
            .border_color = std::optional<renderer::Color>({.r = 200, .g = 200, .b = 200, .a = 255}),
        };
        const auto play_button = m_renderer.draw_button_baked(button, *m_song_info_container);
        m_renderer.render_baked(*play_button);

        if ( m_events.area_was_clicked(play_button->bounds(), nullptr, nullptr) ) {
            toggle_play_pause();
        }
    }

    // Progress bar
    if ( m_album_art != nullptr && m_album_art->is_valid() ) {
        constexpr auto thickness = 10;
        const auto progress_y = m_elapsed_time->bounds().y + m_elapsed_time->bounds().h + 10;
        const auto end_x = m_album_art->bounds().x + m_album_art->bounds().w;

        const auto progress_bar = renderer::ProgressBarOpts{
            .position = {.x = m_album_art->bounds().x, .y = progress_y},
            .end_x = end_x,
            .color = {.r = 200, .g = 200, .b = 200, .a = 255},
            .bg_color = std::optional<renderer::Color>({.r = 100, .g = 100, .b = 100, .a = 255}),
            .thickness = thickness,
            .progress = m_audio.elapsed_time() / m_audio.total_time()
        };
        BoundingBox progress_box = {};
        m_renderer.draw_horiz_progress_simple(progress_bar, &progress_box);
        m_progress_bar_box = progress_box;

        // "fatten" the box so we can more easily seek on the bar by clicking
        progress_box.y -= 5;
        progress_box.h += 10;

        int32_t progress_click_x;
        if ( m_events.area_was_clicked(progress_box, &progress_click_x, nullptr) ) {
            const auto distance_clicked = progress_click_x - progress_box.x;
            const auto progress_percentage = static_cast<double>(distance_clicked) / progress_box.w;
            seek_to_time(progress_percentage * m_audio.total_time());
        }
    }
}

void etsuko::Karaoke::finalize() {
    m_renderer.finalize();
    m_audio.finalize();
}

void etsuko::Karaoke::handle_input() {
    if ( m_events.is_key_down(events::Key::SPACE) ) {
        toggle_play_pause();
    }

    constexpr auto seek_amount = 5.0;
    const auto elapsed = m_audio.elapsed_time();
    if ( m_events.is_key_down(events::Key::LEFT_ARROW) ) {
        seek_to_time(elapsed - seek_amount);
    } else if ( m_events.is_key_down(events::Key::RIGHT_ARROW) ) {
        seek_to_time(elapsed + seek_amount);
    }
}

void etsuko::Karaoke::toggle_play_pause() {
    m_audio.is_paused()
        ? m_audio.resume()
        : m_audio.pause();
}

void etsuko::Karaoke::seek_to_time(const double time) const {
    if ( time > m_audio.total_time() ) {
        throw std::runtime_error("Time is out of range");
    }
    m_audio.seek(time);
}

bool etsuko::Karaoke::loop() {
    const auto current_ticks = SDL_GetTicks64();
    m_delta_time = static_cast<double>(current_ticks - m_current_ticks) / 1000.0;
    m_current_ticks = current_ticks;

    m_events.loop();
    handle_input();

    if ( m_events.window_was_resized() ) {
        m_renderer.notify_window_changed();

        if ( m_lyrics_container.has_value() ) {
            m_lyrics_container->notify_window_resized();
        }

        if ( m_left_container.has_value() ) {
            m_left_container->notify_window_resized();
        }

        if ( m_album_art != nullptr && m_album_art->is_valid() ) {
            m_album_art->invalidate();
        }

        if ( m_song_info_container.has_value() ) {
            m_song_info_container = std::nullopt;
        }

        if ( m_artist_name != nullptr && m_artist_name->is_valid() ) {
            m_artist_name->invalidate();
        }

        if ( m_song_name != nullptr && m_song_name->is_valid() ) {
            m_song_name->invalidate();
        }

        if ( m_text_version != nullptr && m_text_version->is_valid() ) {
            m_text_version->invalidate();
        }
    }

    m_renderer.begin_loop();

    if ( !m_initialized ) {
        async_initialize_loop();
    } else {
        draw_song_album_art();
        draw_song_controls();
        draw_song_info();
        draw_lyrics();
        draw_version();
    }

    m_renderer.end_loop();

    return m_events.has_quit();
}

void etsuko::Karaoke::wait_vsync() const {
    constexpr int64_t target_vsync_interval = 16;
    const auto diff = SDL_GetTicks64() - m_current_ticks;
    const auto remaining = target_vsync_interval - static_cast<int64_t>(diff);
    if ( remaining > 0 ) {
        //SDL_Delay(remaining);
    }
}
