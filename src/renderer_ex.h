/**
 * renderer_ex.h - Things not explicitly part of the regular renderer part
 */

#pragma once

#include "renderer.h"

namespace etsuko::renderer {

    template <typename T>
    class BakedDrawableScrollingContainer final : public VerticalSplitContainer {
    public:
        using BuildFn = std::function<std::shared_ptr<BakedDrawable> (const T &)>;
        using IsEnabledFn = std::function<bool (const T &, size_t)>;

    private:
        std::shared_ptr<std::vector<T>> m_source_list;
        std::vector<std::shared_ptr<BakedDrawable>> m_drawables;
        BoundingBox m_bounds = {};
        BoundingBox m_viewport = {};
        BuildFn m_build_fn;
        IsEnabledFn m_is_enabled_fn;
        ScrollingContainerOpts m_opts;

    public:
        explicit BakedDrawableScrollingContainer(const bool left, const ContainerLike &parent, BuildFn build_fn, const ScrollingContainerOpts &opts) :
            VerticalSplitContainer(left, parent), m_build_fn(build_fn), m_opts(opts) {
            m_is_enabled_fn = [](const T &, size_t) {
                return true;
            };

            const auto parent_bounds = parent.get_bounds();
            if ( !left ) {
                m_bounds.x = parent_bounds.w / 2;
            }
            m_bounds.y = 0;
            m_bounds.w = parent_bounds.w / 2;
            m_bounds.h = parent_bounds.h;

            m_viewport = m_bounds;
        }

        [[nodiscard]] const std::vector<std::shared_ptr<BakedDrawable>> &drawables() const {
            return m_drawables;
        }

        [[nodiscard]] const ScrollingContainerOpts &opts() const {
            return m_opts;
        }

        void set_is_enabled_fn(IsEnabledFn is_enabled_fn) {
            m_is_enabled_fn = is_enabled_fn;
        }

        void set_item_enabled(const size_t index, const bool enabled) const {
            if ( index >= m_drawables.size() ) {
                throw std::runtime_error("Invalid index passed to set_item_enabled");
            }

            m_drawables[index]->set_enabled(enabled);
        }

        void notify_item_changed(size_t index) {
            if ( m_drawables.size() != m_source_list.size() ) {
                notify_item_list_changed();
                return;
            }

            if ( index >= m_source_list.size() ) {
                throw std::runtime_error("Invalid index passed to notify_item_changed");
            }

            const auto &elem = m_source_list->at(index);
            const auto baked = m_build_fn(elem);
            baked->set_enabled(m_is_enabled_fn(elem, index));
            m_drawables[index] = baked;;
        }

        void notify_item_list_changed() {
            m_drawables.clear();
            size_t index = 0;
            for ( const auto &elem : *m_source_list ) {
                const auto baked = m_build_fn(elem);
                baked->set_enabled(m_is_enabled_fn(elem, index++));
                m_drawables.emplace_back(std::move(baked));
            }
        }

        void set_item_list(std::shared_ptr<std::vector<T>> list) {
            m_source_list = std::move(list);
            notify_item_list_changed();
        }

        void draw(const Renderer &renderer) {
            CoordinateType y = m_opts.margin_top - m_viewport.y;
            for ( auto &drawable : m_drawables ) {
                if ( !drawable->is_enabled() )
                    continue;

                if ( y + drawable->bounds().h >= m_bounds.y + m_bounds.h )
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

                drawable->set_bounds({.x = x, .y = y, .w = drawable->bounds().w, .h = drawable->bounds().h});

                if ( y + drawable->bounds().h >= 0 ) {
                    renderer.render_baked(*drawable, *this);
                }

                y += drawable->bounds().h + m_opts.vertical_padding;
            }
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

        void loop(const EventManager &events) {
            const auto scrolled = events.amount_scrolled();

            if ( scrolled != 0.0 ) {
#ifdef __EMSCRIPTEN__
                constexpr auto scroll_speed = 50;
#else
                constexpr auto scroll_speed = 10;
#endif

                m_viewport.y += scrolled * scroll_speed;

                m_viewport.y = std::max(0, m_viewport.y);
                m_viewport.y = std::min(m_viewport.y, total_height());
            }

            size_t index = 0;
            for ( const auto &baked : m_drawables ) {
                const auto &elem = m_source_list->at(index);
                baked->set_enabled(m_is_enabled_fn(elem, index++));
            }
        }

        [[nodiscard]] const BoundingBox &get_bounds() const override {
            return m_bounds;
        }
    };
}
