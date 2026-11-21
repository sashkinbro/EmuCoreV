#pragma once

// Native host for full-screen shell applications (Trophy Collection, Settings,
// Quick Saves). The host (Kotlin) lays out and rasterizes every row and the
// header at 1x; this renderer owns what the PAF list widgets own on the Vita:
// the page stack and its transitions, list scrolling with inertia and
// overscroll, touch/key focus, the plate/glow button planes and the corner
// back button. All coordinates are in the original 960x544 content space.

#include <emucorev/paf/sprite_renderer.h>

#include <GLES3/gl32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace shell_app {

struct Row {
    std::uint32_t image = 0;       // host raster, drawn at (0,0,w,h) in the row
    std::uint32_t plate = 0;       // button bg plane (stretched like PAF planeobj)
    std::uint32_t glow = 0;        // additive focus/press plane
    float x = 0.f, width = 960.f;  // row placement inside the list
    float height = 80.f;
    float image_width = 0.f, image_height = 0.f;
    bool selectable = true;
};

struct Page {
    std::string app;
    int key = 0;
    std::uint32_t background = 0;
    std::uint32_t header = 0;
    std::array<float, 4> header_rect{};  // x, y, w, h
    std::array<float, 4> list_rect{0.f, 96.f, 960.f, 384.f};
    std::vector<Row> rows;
    int focus = -1;
    float scroll = 0.f;
    float velocity = 0.f;
    float row_top(int index) const noexcept {
        float y = 0.f;
        for (int i = 0; i < index && i < int(rows.size()); ++i) y += rows[std::size_t(i)].height;
        return y;
    }
    float content_height() const noexcept { return row_top(int(rows.size())); }
    float max_scroll() const noexcept { return std::max(0.f, content_height() - list_rect[3]); }
};

enum class Transition { none = 0, push = 1, pop = 2, replace = 3, open = 4 };

struct Canvas {
    float cw = 960.f, ch = 544.f, cx0 = 0.f, cy0 = 0.f;
    float edge_left = 0.f, edge_bottom = 544.f;
};

class AppScreen {
public:
    std::function<void(const std::string&, int, int)> on_row;   // app, page key, row
    std::function<void(const std::string&, int)> on_back;       // app, page key
    std::function<void(int)> on_sound;
    std::uint32_t back_icon = 0;

    bool active() const noexcept { return !pages_.empty() || closing_ > 0.f; }
    bool visible() const noexcept { return active(); }
    const std::string& app() const noexcept { static const std::string none; return pages_.empty() ? none : pages_.back().app; }

    void show(Page page, Transition transition) {
        if (transition == Transition::open || pages_.empty()) {
            pages_.clear();
            previous_.clear();
            pages_.push_back(std::move(page));
            open_ = 0.f;
            closing_ = 0.f;
            slide_ = 1.f;
            return;
        }
        if (transition == Transition::replace) {
            // Keep the list position when a page refreshes itself in place.
            auto& current = pages_.back();
            if (current.key == page.key) {
                page.scroll = std::min(current.scroll, page.max_scroll());
                if (page.focus < 0) page.focus = current.focus;
            }
            current = std::move(page);
            return;
        }
        previous_ = {pages_.back()};
        slide_dir_ = transition == Transition::pop ? -1.f : 1.f;
        if (transition == Transition::pop && pages_.size() > 1) pages_.pop_back();
        if (transition == Transition::pop) {
            if (!pages_.empty() && pages_.back().key == page.key) {
                page.scroll = std::min(pages_.back().scroll, page.max_scroll());
                if (page.focus < 0) page.focus = pages_.back().focus;
                pages_.back() = std::move(page);
            } else {
                pages_.back() = std::move(page);
            }
        } else {
            pages_.push_back(std::move(page));
        }
        slide_ = 0.f;
    }

    void close() {
        if (pages_.empty()) return;
        closing_page_ = pages_.back();
        pages_.clear();
        previous_.clear();
        closing_ = 1.f;
    }

    void step(float dt) {
        // Provisional curves (not yet traced from SceShell): 300 ms app zoom,
        // 250 ms horizontal page slide, PAF-like quadratic easing.
        if (open_ < 1.f) open_ = std::min(1.f, open_ + dt / 0.3f);
        if (slide_ < 1.f) {
            slide_ = std::min(1.f, slide_ + dt / 0.25f);
            if (slide_ >= 1.f) previous_.clear();
        }
        if (closing_ > 0.f) closing_ = std::max(0.f, closing_ - dt / 0.25f);
        if (pages_.empty()) return;
        auto& p = pages_.back();
        if (!dragging_) {
            p.scroll += p.velocity * dt;
            p.velocity *= std::pow(0.02f, dt);  // ~98% velocity loss per second
            if (std::abs(p.velocity) < 4.f) p.velocity = 0.f;
            const float max = p.max_scroll();
            if (p.scroll < 0.f) { p.scroll += (0.f - p.scroll) * std::min(1.f, dt * 14.f); p.velocity *= 0.5f; }
            if (p.scroll > max) { p.scroll += (max - p.scroll) * std::min(1.f, dt * 14.f); p.velocity *= 0.5f; }
        }
        if (press_row_ >= 0) press_time_ += dt;
    }

    void draw(emucorev::paf::SpriteRenderer& sprites, const Canvas& c) {
        if (pages_.empty() && closing_ <= 0.f) return;
        sprites.begin(c.cw, c.ch);
        sprites.set_origin(c.cx0, c.cy0);
        const float e = ease(open_);
        if (closing_ > 0.f) {
            const float k = ease(closing_);
            draw_page(sprites, c, closing_page_, 0.f, k, 0.94f + 0.06f * k);
            return;
        }
        const float scale = 0.94f + 0.06f * e;
        if (!previous_.empty() && slide_ < 1.f) {
            const float t = ease(slide_);
            // Background stays; the list content slides horizontally.
            draw_page(sprites, c, previous_.front(), -slide_dir_ * 960.f * t, e, scale, true, 1.f - t);
            draw_page(sprites, c, pages_.back(), slide_dir_ * 960.f * (1.f - t), e, scale, false, t);
        } else {
            draw_page(sprites, c, pages_.back(), 0.f, e, scale);
        }
    }

    // ----- input (content coordinates) -----
    bool touch_down(float x, float y) {
        if (pages_.empty() || slide_ < 1.f) return true;
        down_x_ = x; down_y_ = y; last_y_ = y;
        dragging_ = false;
        moved_ = false;
        auto& p = pages_.back();
        p.velocity = 0.f;
        back_pressed_ = in_back(x, y);
        press_row_ = back_pressed_ ? -1 : row_at(x, y);
        press_time_ = 0.f;
        return true;
    }
    bool touch_move(float x, float y, float dt) {
        if (pages_.empty()) return true;
        auto& p = pages_.back();
        if (!moved_ && std::hypot(x - down_x_, y - down_y_) > 14.f) {
            moved_ = true;
            dragging_ = true;
            press_row_ = -1;
            back_pressed_ = false;
        }
        if (dragging_) {
            const float dy = y - last_y_;
            float next = p.scroll - dy;
            const float max = p.max_scroll();
            if (next < 0.f || next > max) next = p.scroll - dy * 0.4f;  // rubber band
            p.scroll = next;
            if (dt > 0.f) p.velocity = p.velocity * 0.6f + (-dy / dt) * 0.4f;
        }
        last_y_ = y;
        return true;
    }
    bool touch_up(float x, float y, bool canceled) {
        if (pages_.empty()) return true;
        auto& p = pages_.back();
        if (!canceled && !moved_) {
            if (back_pressed_ && in_back(x, y)) {
                if (on_sound) on_sound(2);
                if (on_back) on_back(p.app, p.key);
            } else if (press_row_ >= 0 && row_at(x, y) == press_row_) {
                p.focus = press_row_;
                if (on_sound) on_sound(1);
                if (on_row) on_row(p.app, p.key, press_row_);
            }
        }
        dragging_ = false;
        press_row_ = -1;
        back_pressed_ = false;
        return true;
    }

    bool key(int code_up, int code_down, int code_ok, int code_back, int key) {
        if (pages_.empty() || slide_ < 1.f) return true;
        auto& p = pages_.back();
        auto move = [&](int dir) {
            int i = p.focus;
            for (int n = 0; n < int(p.rows.size()); ++n) {
                i += dir;
                if (i < 0 || i >= int(p.rows.size())) return;
                if (p.rows[std::size_t(i)].selectable) break;
            }
            if (i < 0 || i >= int(p.rows.size()) || !p.rows[std::size_t(i)].selectable) return;
            p.focus = i;
            const float top = p.row_top(i), bottom = top + p.rows[std::size_t(i)].height;
            if (top < p.scroll) p.scroll = top;
            if (bottom > p.scroll + p.list_rect[3]) p.scroll = bottom - p.list_rect[3];
            if (on_sound) on_sound(3);
        };
        if (key == code_up) { if (p.focus < 0) p.focus = first_selectable(p) + 1; move(-1); }
        else if (key == code_down) { if (p.focus < 0) p.focus = -1; move(1); }
        else if (key == code_ok) {
            if (p.focus >= 0 && p.focus < int(p.rows.size()) && p.rows[std::size_t(p.focus)].selectable) {
                if (on_sound) on_sound(1);
                if (on_row) on_row(p.app, p.key, p.focus);
            }
        } else if (key == code_back) {
            if (on_sound) on_sound(2);
            if (on_back) on_back(p.app, p.key);
        } else return false;
        return true;
    }

private:
    static float ease(float t) noexcept { return 1.f - (1.f - t) * (1.f - t); }
    static int first_selectable(const Page& p) noexcept {
        for (int i = 0; i < int(p.rows.size()); ++i) if (p.rows[std::size_t(i)].selectable) return i - 1;
        return -1;
    }

    bool in_back(float x, float y) const noexcept {
        return x < back_x_ + 72.f && y > back_y_ - 16.f;
    }

    int row_at(float x, float y) const noexcept {
        const auto& p = pages_.back();
        const auto& l = p.list_rect;
        if (y < l[1] || y > l[1] + l[3]) return -1;
        float top = l[1] - p.scroll;
        for (int i = 0; i < int(p.rows.size()); ++i) {
            const auto& r = p.rows[std::size_t(i)];
            if (y >= top && y < top + r.height && x >= l[0] + r.x && x < l[0] + r.x + r.width)
                return r.selectable ? i : -1;
            top += r.height;
        }
        return -1;
    }

    void draw_page(emucorev::paf::SpriteRenderer& sprites, const Canvas& c, const Page& p,
        float offset_x, float alpha, float scale, bool skip_background = false, float list_alpha = 1.f) {
        const std::array<float, 4> tint{1.f, 1.f, 1.f, alpha};
        if (!skip_background && p.background) {
            // Cover the adaptive canvas with a uniform scale (never stretched).
            const float s = std::max(c.cw / 960.f, c.ch / 544.f) * (2.f - scale);
            const float w = 960.f * s, h = 544.f * s;
            sprites.draw(p.background, 480.f - w * .5f, 272.f - h * .5f, w, h, tint);
        }
        const float la = alpha * list_alpha;
        const std::array<float, 4> ltint{1.f, 1.f, 1.f, la};
        auto sx = [&](float x) { return 480.f + (x + offset_x - 480.f) * scale; };
        auto sy = [&](float y) { return 272.f + (y - 272.f) * scale; };
        if (p.header) {
            const auto& r = p.header_rect;
            sprites.draw(p.header, sx(r[0]), sy(r[1]), r[2] * scale, r[3] * scale, ltint);
        }
        // Clip rows to the list viewport (framebuffer is the canvas at 1x).
        const auto& l = p.list_rect;
        const float clip_top = sy(l[1]), clip_bottom = sy(l[1] + l[3]);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, GLint(std::floor(c.ch - (clip_bottom + c.cy0))), GLsizei(c.cw),
            GLsizei(std::ceil(clip_bottom - clip_top)));
        float top = l[1] - p.scroll;
        for (int i = 0; i < int(p.rows.size()); ++i) {
            const auto& r = p.rows[std::size_t(i)];
            const float y0 = top;
            top += r.height;
            if (y0 + r.height < l[1] - 2.f || y0 > l[1] + l[3] + 2.f) continue;
            const float x = sx(l[0] + r.x), y = sy(y0), w = r.width * scale, h = r.height * scale;
            if (r.plate) sprites.draw(r.plate, x, y, w, h, ltint);
            const bool pressed = i == press_row_ && press_time_ > 0.03f && &p == &pages_.back();
            const bool focused = i == p.focus && &p == &pages_.back();
            if (r.glow && (pressed || focused)) {
                sprites.set_additive_blend(true);
                sprites.draw(r.glow, x, y, w, h, {1.f, 1.f, 1.f, la * (pressed ? 1.f : 0.75f)});
                sprites.set_additive_blend(false);
            }
            if (r.image)
                sprites.draw(r.image, x, y, (r.image_width > 0.f ? r.image_width : r.width) * scale,
                    (r.image_height > 0.f ? r.image_height : r.height) * scale, ltint);
        }
        glDisable(GL_SCISSOR_TEST);
        // Scroll indicator while the list moves.
        const float max = p.max_scroll();
        if (max > 1.f && (dragging_ || std::abs(p.velocity) > 1.f) && &p == &pages_.back()) {
            const float bar_h = std::max(24.f, l[3] * l[3] / p.content_height());
            const float t = std::clamp(p.scroll / max, 0.f, 1.f);
            sprites.draw(0, sx(l[0] + l[2] - 8.f), sy(l[1] + (l[3] - bar_h) * t), 4.f, bar_h * scale,
                {1.f, 1.f, 1.f, 0.45f * la});
        }
        // Corner back button (bottom-left), as in Vita applications.
        back_x_ = c.edge_left;
        back_y_ = c.edge_bottom - 64.f;
        if (back_icon) {
            const float a = alpha * (back_pressed_ && &p == &pages_.back() ? 0.6f : 1.f);
            sprites.draw(back_icon, back_x_ + 8.f, back_y_ + 4.f, 56.f, 56.f, {1.f, 1.f, 1.f, a});
        }
    }

    std::vector<Page> pages_;
    std::vector<Page> previous_;
    Page closing_page_;
    float open_ = 1.f, slide_ = 1.f, slide_dir_ = 1.f, closing_ = 0.f;
    bool dragging_ = false, moved_ = false, back_pressed_ = false;
    float down_x_ = 0.f, down_y_ = 0.f, last_y_ = 0.f, press_time_ = 0.f;
    int press_row_ = -1;
    mutable float back_x_ = 0.f, back_y_ = 480.f;
};

}  // namespace shell_app
