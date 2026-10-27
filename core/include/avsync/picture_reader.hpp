#pragma once
#include "avsync/avsync.hpp"

namespace avsync {
// Fast full-raster path, then a cached optical/tile pose. Search is bounded in
// time and size; no failed pose is allowed to keep publishing old video events.
class PictureReader {
public:
    LocatedMarker read(const uint16_t* pixels, int width, int height, int stride, double time_ms);
    void reset();
private:
    PicturePose pose_;
    double last_search_ms_ = -1e30;
    int width_ = 0, height_ = 0;
};
} // namespace avsync
