#include "avsync/picture_reader.hpp"
#include <algorithm>
#include <cmath>

namespace avsync {
void PictureReader::reset() { pose_ = {}; last_search_ms_ = -1e30; width_ = height_ = 0; }
LocatedMarker PictureReader::read(const uint16_t* pixels, int width, int height, int stride, double time_ms) {
    if (width != width_ || height != height_) { reset(); width_ = width; height_ = height; }
    LocatedMarker out;
    if (!pixels || width < 160 || height < 90 || stride < width || !std::isfinite(time_ms)) return out;
    const auto valid = [](const LocatedMarker& p) {
        return p.located && p.reading.geometry_ok && p.reading.strip_ok && p.reading.state != MarkerReading::State::Invalid;
    };
    out.reading = read_marker(pixels, width, height, stride);
    if (out.reading.geometry_ok && out.reading.strip_ok) {
        out.located = true; out.width = width; out.height = height;
        return out;
    }
    if (pose_.found) out = read_located_marker_at_pose(pixels, width, height, stride, pose_);
    const bool cached_valid = valid(out);
    const double since = time_ms - last_search_ms_;
    if (since >= (cached_valid ? 500 : 100) || since < 0) {
        last_search_ms_ = time_ms;
        // A 4K camera must not force a 4K fiducial search every retry. Preserve
        // original pixels for final rectification and QR decoding.
        const double scale = std::min(1.0, 960.0 / std::max(width, height));
        const int sw = std::max(1, int(std::lround(width * scale)));
        const int sh = std::max(1, int(std::lround(height * scale)));
        std::vector<uint16_t> small;
        PicturePose next;
        if (scale == 1) next = locate_picture(pixels, width, height, stride);
        else {
            small.resize(size_t(sw) * sh);
            for (int y=0; y<sh; ++y) for (int x=0; x<sw; ++x)
                small[size_t(y)*sw+x] = pixels[size_t(std::min(height-1, int(y/scale)))*stride + std::min(width-1,int(x/scale))];
            next = locate_picture(small.data(),sw,sh,sw);
            if (next.found) {
                next.origin_x/=scale; next.origin_y/=scale; next.width/=scale; next.height/=scale; next.fiducial_size/=scale;
                for (int i=0;i<4;++i) { next.corner_x[i]/=scale; next.corner_y[i]/=scale; }
            }
        }
        if (next.found) {
            auto fresh = read_located_marker_at_pose(pixels,width,height,stride,next);
            if (valid(fresh)) { pose_=next; return fresh; }
            // The bounded search quantizes fiducial edges. Correct a small
            // border bias on original pixels before rejecting the pose; keep
            // the same geometry, parity and marker validity requirements.
            const int radius = std::min(8, std::max(1, int(std::ceil(2.0 / scale))));
            for (int distance = 1; distance <= radius; ++distance) {
                for (int direction : {1, -1}) {
                    const double du = direction * distance / next.width;
                    const double dv = direction * distance / next.height;
                    auto refined = next;
                    for (int corner = 0; corner < 4; ++corner) {
                        const double u = (corner == 1 || corner == 2) ? 1 - du : du;
                        const double v = corner >= 2 ? 1 - dv : dv;
                        const double weights[4] = {(1-u)*(1-v), u*(1-v), u*v, (1-u)*v};
                        refined.corner_x[corner] = refined.corner_y[corner] = 0;
                        for (int source = 0; source < 4; ++source) {
                            refined.corner_x[corner] += weights[source] * next.corner_x[source];
                            refined.corner_y[corner] += weights[source] * next.corner_y[source];
                        }
                    }
                    refined.origin_x = refined.corner_x[0]; refined.origin_y = refined.corner_y[0];
                    refined.width = ((refined.corner_x[1]-refined.corner_x[0])+(refined.corner_x[2]-refined.corner_x[3]))/2;
                    refined.height = ((refined.corner_y[3]-refined.corner_y[0])+(refined.corner_y[2]-refined.corner_y[1]))/2;
                    auto candidate = read_located_marker_at_pose(pixels,width,height,stride,refined);
                    if (fresh.reading.strip_ok && candidate.reading.seq_low24 != fresh.reading.seq_low24) continue;
                    if (valid(candidate)) { pose_=refined; return candidate; }
                }
            }
        }
    }
    if (cached_valid) return out;
    out.located = false; out.canonical.clear();
    return out;
}
} // namespace avsync
