/**
 * renderer_ex.h - Things not explicitly part of the regular renderer part
 */

#pragma once

#include "renderer.h"

namespace etsuko::renderer {

    class BakedDrawableScrollingLyricsContainer final : public VerticalSplitContainer {
        std::shared_ptr<std::vector<parser::TimedLyric>> m_source_list;
        std::vector<std::shared_ptr<BakedDrawable>> m_drawables;
        BoundingBox m_bounds = {};
        BoundingBox m_viewport = {};
        ScrollingContainerOpts m_opts;
        double m_elapsed_time = 0.0;
        double m_delta_time = 0.0;
        size_t m_active_index = 0, m_draw_active_index = 0;
        Renderer *m_renderer = nullptr;
        bool m_first_draw = false;
        // Animations
        double m_active_y_offset_delta = 0.0, m_previous_active_y_offset = 0.0;

        [[nodiscard]] size_t find_last_visible_index() const {
            if ( m_source_list == nullptr ) {
                throw std::runtime_error("Source list is null");
            }

            for ( size_t i = 0; i < m_source_list->size() - 1; ++i ) {
                const auto &elem = m_source_list->at(i);
                const auto &next = m_source_list->at(i + 1);
                if ( m_elapsed_time >= elem.base_start_time && m_elapsed_time < next.base_start_time ) {
                    return i;
                }
            }
            return -1;
        }

        [[nodiscard]] size_t find_active_index() const {
            if ( m_source_list == nullptr ) {
                throw std::runtime_error("Source list is null");
            }

            for ( size_t i = 0; i < m_source_list->size(); ++i ) {
                const auto &elem = m_source_list->at(i);
                if ( m_elapsed_time >= elem.base_start_time && m_elapsed_time < elem.base_start_time + elem.base_duration ) {
                    return i;
                }
            }

            return 0;
        }

        [[nodiscard]] static TextOpts build_text_opts(const parser::TimedLyric &elem, const bool active) {
            constexpr auto active_pts = 26;
            constexpr Color active_color = {.r = 255, .g = 255, .b = 255, .a = 255};
            constexpr Color inactive_color = {.r = 100, .g = 100, .b = 100, .a = 255};
            std::string line_text = elem.full_line;
            if ( line_text.empty() ) {
                if ( elem.base_duration > 1.5 ) {
                    line_text = "...";
                } else {
                    line_text = " ";
                }
            }
            const auto pts = active ? active_pts : 20;
            return {
                .text = line_text,
                .position = {},
                .pt_size = pts,
                .bold = false,
                .color = active ? active_color : inactive_color,
                .layout_opts = {
                    .wrap = true,
                    .wrap_opts = {
                        .measure_at_pts = active_pts,
                        .wrap_width_threshold = 0.95,
                        .line_padding = 10,
                    }
                }
            };
        }

    public:
        explicit BakedDrawableScrollingLyricsContainer(Renderer *renderer, const bool left, const ContainerLike &parent, const ScrollingContainerOpts &opts) :
            VerticalSplitContainer(left, parent), m_opts(opts), m_renderer(renderer) {
            if ( m_renderer == nullptr ) {
                throw std::runtime_error("Renderer is null");
            }

            const auto parent_bounds = parent.get_bounds();
            if ( !left ) {
                m_bounds.x = parent_bounds.w / 2;
            }
            m_bounds.y = 0;
            m_bounds.w = parent_bounds.w / 2;
            m_bounds.h = parent_bounds.h;

            m_viewport = m_bounds;
        }

        void set_item_list(const std::shared_ptr<std::vector<parser::TimedLyric>> &source_list) {
            m_source_list = source_list;
            m_drawables.clear();
            m_drawables.reserve(source_list->size());
            for ( size_t i = 0; i < source_list->size(); ++i ) {
                const auto &elem = source_list->at(i);
                const auto opts = build_text_opts(elem, i == m_active_index);
                m_drawables.push_back(m_renderer->draw_text_baked(opts, *this));
            }
        }

        [[nodiscard]] const std::vector<std::shared_ptr<BakedDrawable>> &drawables() const {
            return m_drawables;
        }

        [[nodiscard]] const ScrollingContainerOpts &opts() const {
            return m_opts;
        }

        void set_item_enabled(const size_t index, const bool enabled) const {
            if ( index >= m_drawables.size() ) {
                throw std::runtime_error("Invalid index passed to set_item_enabled");
            }

            m_drawables[index]->set_enabled(enabled);
        }

        void draw(const Renderer &renderer, const bool animate) {
            if ( m_source_list->size() != m_drawables.size() ) {
                throw std::runtime_error("Invalid state: Not all lyric elements have generated drawables");
            }

            const auto last_visible_index = find_last_visible_index();
            if ( m_active_index < last_visible_index ) {
                throw std::runtime_error("Impossible state: active_index is less than last_visible_index");
            }

            CoordinateType y = m_opts.margin_top - m_viewport.y;
            for ( size_t i = last_visible_index; i < m_source_list->size(); i++ ) {
                if ( m_active_index != m_draw_active_index && i <= m_active_index ) {
                    // Invalidate the drawable
                    m_drawables[i]->invalidate();

                    if ( i != m_draw_active_index ) {
                        m_drawables[m_draw_active_index]->invalidate();
                    }
                }

                if ( !m_drawables.at(i)->is_valid() ) {
                    const auto old_bounds = m_drawables.at(i)->bounds();
                    m_drawables[i] = m_renderer->draw_text_baked(build_text_opts(m_source_list->at(i), i == m_active_index), *this);
                    const auto bounds = m_drawables.at(i)->bounds();
                    m_drawables.at(i)->set_bounds({.x = old_bounds.x, .y = old_bounds.y, .w = bounds.w, .h = bounds.h});
                }

                const auto &drawable = m_drawables.at(i);
                if ( !drawable->is_enabled() )
                    continue;

                if ( i == m_active_index && m_active_index != m_draw_active_index ) {
                    constexpr auto translate_duration = 0.4;
                    m_active_y_offset_delta = drawable->bounds().y > 0 ? (y - drawable->bounds().y) / translate_duration : 1.0;
                    if ( m_active_y_offset_delta < -2000 ) {
                        //std::puts(std::format("bounds {}, y {}", drawable->bounds().y, y).c_str());
                    }
                }

                auto animated_y = y;

                const auto frame_delta = m_active_y_offset_delta * m_delta_time;
                const auto frame_target = drawable->bounds().y + frame_delta;
                if ( !m_first_draw && animate && y < frame_target ) {
                    if ( frame_target - y > 100 ) {
                        // TODO: Fix fucking error that makes the lyric jump to the bottom of the screen on seek
                        //std::puts(std::format("Frame delta: {}, target: {}, y {}, delta_time {}, offset_delta {}", frame_delta, frame_target, y, m_delta_time, m_active_y_offset_delta).c_str());
                    }
                    animated_y = std::max(y, static_cast<CoordinateType>(frame_target));
                }

                if ( animate ) {
                    //std::puts(std::format("animated_y {} y {}", animated_y, y).c_str());
                    //animated_y = std::min(animated_y, y);
                }
                if ( animated_y + drawable->bounds().h >= m_bounds.y + m_bounds.h )
                    break;

                CoordinateType x;
                if ( m_opts.alignment == ScrollingContainerOpts::ALIGN_LEFT ) {
                    x = 0;
                } else if ( m_opts.alignment == ScrollingContainerOpts::ALIGN_CENTER ) {
                    x = m_bounds.w / 2 - drawable->bounds().w / 2;
                } else if ( m_opts.alignment == ScrollingContainerOpts::ALIGN_RIGHT ) {
                    x = m_bounds.w - drawable->bounds().w;
                } else
                    throw std::runtime_error("Invalid alignment option");

                drawable->set_bounds({.x = x, .y = animated_y, .w = drawable->bounds().w, .h = drawable->bounds().h});

                if ( y + drawable->bounds().h >= 0 ) {
                    auto alpha = 255;
                    for ( int distance = 1; distance < std::min(static_cast<int>(i - m_active_index), 5); distance++ ) {
                        alpha = static_cast<uint8_t>(alpha / 1.5);
                    }
                    renderer.render_baked(*drawable, *this, alpha);
                }

                y += drawable->bounds().h + m_opts.vertical_padding;

                if ( m_first_draw ) {
                    m_first_draw = false;
                }
            }

            m_draw_active_index = m_active_index;
        }

        [[nodiscard]] CoordinateType total_height() const {
            CoordinateType h = 0;
            for ( const auto &drawable : m_drawables ) {
                if ( drawable != nullptr && drawable->is_enabled() ) {
                    h += drawable->bounds().h + m_opts.vertical_padding;
                }
            }

            return h;
        }

        void loop(const EventManager &events, const double delta_time, const double audio_elapsed_time) {
            const auto scrolled = events.amount_scrolled();
            m_elapsed_time = audio_elapsed_time;
            m_delta_time = delta_time;
            m_active_index = find_active_index();

            if ( scrolled != 0.0 ) {
#ifdef __EMSCRIPTEN__
                constexpr auto scroll_speed = 50.0;
#else
                constexpr auto scroll_speed = 10.0;
#endif

                m_viewport.y += static_cast<CoordinateType>(scrolled * scroll_speed);

                m_viewport.y = std::max(0, m_viewport.y);
                m_viewport.y = std::min(m_viewport.y, total_height());
            }
        }

        [[nodiscard]] const BoundingBox &get_bounds() const override {
            return m_bounds;
        }
    };
}
