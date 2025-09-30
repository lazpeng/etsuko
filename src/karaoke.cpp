#include "karaoke.h"

#include <SDL2/SDL.h>
#include <format>

etsuko::config::Config etsuko::config::Config::get_default() {
    return {
        .font_path = DEFAULT_FONT,
        .font_index = 0,
        .song_path = "sidewalks.txt"
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
    constexpr auto active_pts = 26;
    const auto build_fn = [&](const parser::TimedLyric &line) {
        const auto time = m_audio.elapsed_time();
        const auto active = time >= line.base_start_time && time <= line.base_start_time + line.base_duration;
        constexpr renderer::Color inactive_color = {.r = 100, .g = 100, .b = 100, .a = 255};
        const auto active_color = renderer::Color::white();

        const auto pts = active ? active_pts : 20;
        const auto color = active ? active_color : inactive_color;
        std::string line_text = line.full_line;
        if ( line_text.empty() ) {
            if ( line.base_duration > 1.5 || line.base_duration < 0 )
                line_text = "...";
            else
                line_text = " ";
        }
        const renderer::TextOpts opts = {
            .text = line_text,
            .position = {},
            .pt_size = pts,
            .bold = false,
            .color = color,
            .layout_opts = {
                .wrap = true,
                .wrap_opts = {
                    .measure_at_pts = active_pts,
                    .wrap_width_threshold = 0.95,
                    .line_padding = 10,
                }
            }
        };
        return m_renderer.draw_text_baked(opts, m_lyrics_container.value());
    };
    const auto is_enabled_fn = [&](const parser::TimedLyric &line) {
        const auto time = m_audio.elapsed_time();
        const auto active = time >= line.base_start_time;
        return time <= line.base_start_time + line.base_duration && (!line.full_line.empty() || active);
    };
    const renderer::ScrollingContainerOpts scroll_opts = {
        .margin_top = m_renderer.get_bounds().h / 2 - 100,
        .vertical_padding = 40,
        .alignment = renderer::ScrollingContainerOpts::ALIGN_CENTER
    };
    m_lyrics_container = renderer::BakedDrawableScrollingContainer<parser::TimedLyric>(false, m_renderer.root_container(), build_fn, scroll_opts);
    m_lyrics_container->set_item_list(std::make_shared<std::vector<parser::TimedLyric>>(m_song.lyrics));
    m_lyrics_container->set_is_enabled_fn(is_enabled_fn);
}

void etsuko::Karaoke::async_initialize_loop() {
    // Check loading of the font
    switch ( m_load_font.status ) {
    case repository::LoadJob::NOT_STARTED:
        Repository::get_resource(m_config.font_path, &m_load_font);
        break;
    case repository::LoadJob::DONE:
        if ( m_load_font.error.empty() ) {
            m_renderer.load_font(m_load_font.result_path, m_config.font_index);
        } else {
            std::puts("Failed to load font");
            std::puts(m_load_font.error.c_str());
            // TODO: Find a way to quit, or just keep running forever?
        }

        m_load_font.status = repository::LoadJob::NONE;
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
        m_song = Parser::parse(m_load_song.result_path);
        m_load_song.status = repository::LoadJob::NONE;
        break;
    default:
        break;
    }

    // Load other parts of the song
    if ( m_load_song.status == repository::LoadJob::NONE ) {
        // Load the actual song file
        if ( m_load_audio.status == repository::LoadJob::NOT_STARTED ) {
            Repository::get_resource(m_song.file_path, &m_load_audio);
        } else if ( m_load_audio.status == repository::LoadJob::DONE ) {
            m_audio.load_song(m_load_audio.result_path);
            m_load_audio.status = repository::LoadJob::NONE;
        }

        // Load the album art
        if ( m_load_art.status == repository::LoadJob::NOT_STARTED ) {
            Repository::get_resource(m_song.album_art_path, &m_load_art);
        } else if ( m_load_art.status == repository::LoadJob::DONE ) {
            m_song.album_art_path = m_load_art.result_path;
        }

        if ( m_load_audio.status == repository::LoadJob::NONE ) {
            m_initialized = m_load_font.status == repository::LoadJob::NONE;

            if ( m_initialized && !m_lyrics_container.has_value() ) {
                initialize_lyrics_container();
            }
        }
    }
}

void etsuko::Karaoke::draw_version() {
    if ( m_text_version == nullptr || !m_text_version->is_valid() ) {
        const renderer::TextOpts opts = {
            .text = std::format("etsuko v{}", VERSION_STRING),
            .position = {
                .x = -1,
                .y = 0,
                .flags = renderer::Point::ANCHOR_RIGHT_X
            },
            .pt_size = 12,
            .bold = true,
            .color = renderer::Color::white().darken()
        };
        m_text_version = m_renderer.draw_text_baked(opts, m_renderer.root_container());
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
            .w = 800,
            .resource_path = m_song.album_art_path,
        };

        m_album_art = m_renderer.draw_image_baked(opts, *m_left_container);
    }

    m_renderer.render_baked(*m_album_art, *m_left_container);
}

void etsuko::Karaoke::draw_song_info() {
    if ( m_album_art != nullptr && m_album_art->is_valid() ) {
        const BoundingBox box = {.x = m_album_art->bounds().x, .y = m_play_button->bounds().y + m_play_button->bounds().h + 10, .w = m_album_art->bounds().w, .h = 1000};
        const auto container = renderer::VirtualContainer(*m_left_container, box);

        auto y = 10;
        const renderer::TextOpts name_opt = {
            .text = m_song.name,
            .position = {.x = 0, .y = y, .flags = renderer::Point::CENTERED_X},
            .pt_size = 11,
            .bold = true,
            .color = renderer::Color::white().darken()
        };
        m_song_name = m_renderer.draw_text_baked(name_opt, container);
        m_renderer.render_baked(*m_song_name, container);

        constexpr renderer::Color grey = {.r = 150, .g = 150, .b = 150, .a = 255};
        y += m_song_name->bounds().h + 10;
        const auto artist_album_text = std::format("{} - {}", m_song.artist, m_song.album);
        const renderer::TextOpts artist_opt = {
            .text = artist_album_text,
            .position = {.x = 0, .y = y, .flags = renderer::Point::CENTERED_X},
            .pt_size = 10,
            .bold = false,
            .color = grey
        };
        m_artist_name = m_renderer.draw_text_baked(artist_opt, container);
        m_renderer.render_baked(*m_artist_name, container);
    }
}

void etsuko::Karaoke::draw_lyrics() {
    if ( m_lyrics_container.has_value() ) {
        const auto time = m_audio.elapsed_time();
        size_t idx = 0;
        for ( const auto &line : m_song.lyrics ) {
            if ( time >= line.base_start_time && time < line.base_start_time + line.base_duration ) {
                break;
            }
            idx += 1;
        }
        if ( m_current_active_index != idx ) {
            m_lyrics_container->notify_item_list_changed();
            m_current_active_index = idx;
        }

        m_lyrics_container->loop(m_events);
        m_lyrics_container->draw(m_renderer);
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

        int32_t x = m_album_art->bounds().x + 10, y = m_album_art->bounds().y + m_album_art->bounds().h + 10;

        if ( m_play_button != nullptr && m_play_button->is_valid() ) {
            x += m_play_button->bounds().w;
        }

        const renderer::TextOpts elapsed_text_opt = {
            .text = elapsed_time_text,
            .position = {
                .x = x,
                .y = y
            },
            .pt_size = 10,
            .bold = false
        };
        m_elapsed_time = m_renderer.draw_text_baked(elapsed_text_opt, m_renderer.root_container());
        m_renderer.render_baked(*m_elapsed_time);

        x = m_album_art->bounds().x + m_album_art->bounds().w;
        const renderer::TextOpts remaining_text_opt = {
            .text = remaining_time_text,
            .position = {
                .x = x,
                .y = y,
                .flags = renderer::Point::ANCHOR_RIGHT_X
            },
            .pt_size = 10,
            .bold = false
        };
        m_elapsed_time = m_renderer.draw_text_baked(remaining_text_opt, m_renderer.root_container());
        m_renderer.render_baked(*m_elapsed_time);
    }

    // Play button
    if ( m_play_button == nullptr || !m_play_button->is_valid() ) {
        if ( m_album_art == nullptr || !m_album_art->is_valid() ) {
            return;
        }

        const auto x = m_album_art->bounds().x, y = m_elapsed_time->bounds().y + m_elapsed_time->bounds().h + 10;

        const auto button = renderer::ButtonOpts{
            .label_icon = m_audio.is_paused() ? renderer::ButtonOpts::LABEL_ICON_PLAY : renderer::ButtonOpts::LABEL_ICON_PAUSE,
            .position = {.x = x, .y = y},
            .draw_border = true,
            .vertical_padding = 10,
            .horizontal_padding = 10,
            .border_color = std::optional<renderer::Color>({.r = 200, .g = 200, .b = 200, .a = 255}),
        };
        m_play_button = m_renderer.draw_button_baked(button, m_renderer.root_container());
    }
    m_renderer.render_baked(*m_play_button);

    if ( m_events.area_was_clicked(m_play_button->bounds(), nullptr, nullptr) ) {
        m_audio.is_paused()
            ? m_audio.resume()
            : m_audio.pause();
        // Redraw the button
        m_play_button->invalidate();
    }

    // Progress bar
    if ( m_album_art != nullptr && m_album_art->is_valid() ) {
        constexpr auto thickness = 30;
        constexpr auto horizontal_padding = 10;
        const auto progress_x = m_play_button->bounds().x + m_play_button->bounds().w + horizontal_padding;
        const auto progress_y = m_play_button->bounds().y + m_play_button->bounds().h / 2 - thickness / 2;
        const auto end_x = m_album_art->bounds().x + m_album_art->bounds().w;

        const auto progress_bar = renderer::ProgressBarOpts{
            .position = {.x = progress_x, .y = progress_y},
            .end_x = end_x,
            .color = {.r = 200, .g = 200, .b = 200, .a = 255},
            .bg_color = std::optional<renderer::Color>({.r = 100, .g = 100, .b = 100, .a = 255}),
            .thickness = thickness,
            .progress = m_audio.elapsed_time() / m_audio.total_time()
        };
        BoundingBox progress_box = {};
        m_renderer.draw_horiz_progress_simple(progress_bar, &progress_box);

        int32_t progress_click_x;
        if ( m_events.area_was_clicked(progress_box, &progress_click_x, nullptr) ) {
            const auto distance_clicked = progress_click_x - progress_box.x;
            const auto progress_percentage = distance_clicked / static_cast<double>(progress_box.w);
            m_audio.seek(progress_percentage * m_audio.total_time());
        }
    }
}

void etsuko::Karaoke::finalize() {
    m_renderer.finalize();
    m_audio.finalize();
}

bool etsuko::Karaoke::loop() {
    m_current_ticks = SDL_GetTicks64();

    m_events.loop();

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
        SDL_Delay(remaining);
    }
}
