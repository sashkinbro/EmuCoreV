#include <emucorev/paf/busy_indicator.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace emucorev::paf {
namespace {
constexpr float pi = std::bit_cast<float>(0x40490fdbu);
constexpr float half_pi = std::bit_cast<float>(0x3fc90fdbu);
constexpr float spacing = std::bit_cast<float>(0x3f20d97cu);
constexpr float six_pi = std::bit_cast<float>(0x4196cbe4u);
constexpr float inverse_byte = std::bit_cast<float>(0x3b808081u);
std::uint8_t quantize(float alpha) {
    return static_cast<std::uint8_t>(std::clamp(alpha * 255.f, 0.f, 255.f));
}
}
BusyIndicator::BusyIndicator(float width, float ball_size) : width_(width), ball_size_(ball_size) {
    if (!std::isfinite(width) || !std::isfinite(ball_size) || width <= 0 || ball_size <= 0)
        throw std::invalid_argument("Invalid busy indicator dimensions");
    // Plane objects begin white; their zero size hides the initial trails.
    for (auto& dot : snapshot_.dots) dot.alpha = 255;
}
void BusyIndicator::start() {
    auto& s = snapshot_;
    if (s.state == BusyState::idle) {
        s.state = BusyState::starting;
        s.phase = s.elapsed = s.last_emission = 0;
    } else if (s.state == BusyState::stopping) {
        s.state = BusyState::starting;
        s.elapsed = (1.f - s.elapsed * 2.f) * .3f;
    }
}
void BusyIndicator::stop() {
    auto& s = snapshot_;
    if (s.state == BusyState::starting) {
        s.state = BusyState::stopping;
        s.elapsed = (1.f - s.elapsed / .3f) * .5f;
    } else if (s.state == BusyState::running) {
        s.state = BusyState::stopping;
        s.elapsed = 0;
    }
}
void BusyIndicator::tick(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("Invalid busy indicator interval");
    auto& s = snapshot_;
    if (s.state == BusyState::idle) return;
    s.phase += seconds;
    if (s.phase >= pi) {
        s.phase -= pi;
        s.last_emission -= six_pi;
    }
    const float angle = s.phase * 6.f;
    float scale = 1, emission = 1, radius = (width_ - ball_size_) * .5f;
    if (s.state == BusyState::starting) {
        s.elapsed += seconds;
        float rate = s.elapsed / .3f;
        if (rate > 1) { s.state = BusyState::running; rate = 1; }
        scale = static_cast<float>(std::sin(static_cast<double>(rate * half_pi)));
        radius *= 2.f - scale;
        emission = scale * scale;
    } else if (s.state == BusyState::stopping) {
        s.elapsed += seconds;
        const float rate = s.elapsed * 2.f;
        scale = rate < 1 ? static_cast<float>(std::cos(static_cast<double>(rate * half_pi))) : 0;
        radius *= 2.f - scale;
        emission = scale;
    }
    auto& head = s.dots[0];
    head.x = radius * static_cast<float>(std::cos(static_cast<double>(angle)));
    head.y = -(radius * static_cast<float>(std::sin(static_cast<double>(angle))));
    head.size = std::max(0.f, ball_size_ * scale);
    head.alpha = 255;
    float fade = .18f;
    bool emit = false;
    if (emission > 0) {
        const float interval = spacing * emission;
        fade = interval > .001f ? ((seconds * 6.f) * .82f) / (interval * 8.f) : 0;
        emit = s.last_emission + interval < angle;
    }
    bool remaining = false;
    for (unsigned i = 1; i < s.dots.size(); ++i) {
        auto& dot = s.dots[i];
        const float alpha = static_cast<float>(dot.alpha) * inverse_byte;
        if (alpha <= .18f) {
            if (head.size > 0 && emit) {
                dot = {head.x, head.y, head.size * 1.25f, 255};
                s.last_emission = angle;
                emit = false;
            } else dot.alpha = 0;
        } else {
            if (dot.size > ball_size_) {
                dot.size = dot.size - ball_size_ > .5f ? (dot.size + ball_size_) * .5f : ball_size_;
            }
            dot.alpha = quantize(alpha - fade);
            remaining = true;
            if (s.state == BusyState::stopping) {
                // Preserve the original multiply/divide order and rounding.
                dot.x += ((dot.x * fade) * (ball_size_ / dot.size)) * .05f;
                dot.y += ((dot.y * fade) * (ball_size_ / dot.size)) * .05f;
            }
        }
    }
    if (!remaining && s.state == BusyState::stopping) s.state = BusyState::idle;
}
}
