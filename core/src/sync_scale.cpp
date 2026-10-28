#include "avsync/avsync.hpp"
#include <algorithm>
#include <cmath>

namespace avsync {
SyncDot sync_dot_kind(double position_ms)
{
    if (position_ms < -1e-6) {
        return SyncDot::Early;
    }
    if (position_ms > 1e-6) {
        return SyncDot::Late;
    }
    return SyncDot::Zero;
}

SyncPlace sync_place(int frame_in_period, int period_frames, double period_ms)
{
    SyncPlace place;
    if (period_frames < 1) {
        period_frames = 1;
    }
    if (period_ms <= 0) {
        period_ms = 500;
    }
    int frame = frame_in_period % period_frames;
    if (frame < 0) {
        frame += period_frames;
    }
    const double frame_ms = period_ms / static_cast<double>(period_frames);
    const int half = std::max(1, period_frames / 2);
    place.scale_ms = half * frame_ms;
    if (frame == 0) {
        place.position_ms = 0;
    } else if (frame <= half) {
        place.position_ms = frame * frame_ms;
    } else {
        place.position_ms = (frame - period_frames) * frame_ms;
    }
    place.position_ms = std::clamp(place.position_ms, -place.scale_ms, place.scale_ms);
    return place;
}

SyncIndicator sync_indicator(int width, int height, int frame, int period_frames, double period_ms) {
    const auto place = sync_place(frame, period_frames, period_ms);
    const auto g = frame_geometry(width, height);
    const int span = std::max(1, g.sweep_x1 - g.sweep_x0);
    const int radius = std::max(12, height / 40);
    const double along = (place.position_ms + place.scale_ms) / (2 * place.scale_ms);
    SyncIndicator out;
    out.x = g.sweep_x0 + int(std::lround(std::clamp(along,0.0,1.0)*(span-1))) - radius;
    out.y = g.sweep_y - radius; out.side = 2 * radius;
    out.kind = sync_dot_kind(place.position_ms);
    if (out.kind == SyncDot::Early) { out.y10=81*4; out.cb10=90*4; out.cr10=240*4; }
    else if (out.kind == SyncDot::Late) { out.y10=145*4; out.cb10=54*4; out.cr10=34*4; }
    return out;
}
} // namespace avsync
