#include "avsync/avsync.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace avsync {
namespace {

struct Hit {
    int x = 0;
    int y = 0;
    int size = 0;
    double score = 0;
};

int at_px(const uint16_t* luma, int stride, int w, int h, int x, int y)
{
    x = std::max(0, std::min(w - 1, x));
    y = std::max(0, std::min(h - 1, y));
    return luma[static_cast<size_t>(y) * static_cast<size_t>(stride) + x];
}

double score_fiducial(const uint16_t* luma, int stride, int w, int h, int x, int y, int size)
{
    if (size < 8 || x < 0 || y < 0 || x + size > w || y + size > h) {
        return -1;
    }
    const int t = std::max(1, size / 5);
    const int mid = size / 2;
    auto sample = [&](int px, int py) { return at_px(luma, stride, w, h, px, py); };
    const int white_l = sample(x + t / 2, y + mid);
    const int white_r = sample(x + size - 1 - t / 2, y + mid);
    const int white_t = sample(x + mid, y + t / 2);
    const int white_b = sample(x + mid, y + size - 1 - t / 2);
    const int black_l = sample(x + t + t / 2, y + mid);
    const int black_r = sample(x + size - 1 - (t + t / 2), y + mid);
    const int black_t = sample(x + mid, y + t + t / 2);
    const int black_b = sample(x + mid, y + size - 1 - (t + t / 2));
    const int center = sample(x + mid, y + mid);
    const int borders[4] = {white_l, white_r, white_t, white_b};
    const int rings[4] = {black_l, black_r, black_t, black_b};
    // Camera exposure, monitor glare and scaling lift the black ring well above
    // the digital test signal's black level. Use local contrast, not absolute Y.
    double border_sum = 0, ring_sum = 0;
    for (int value : borders) border_sum += value;
    for (int value : rings) ring_sum += value;
    const double white = border_sum / 4.0, black = ring_sum / 4.0;
    int bright = 0, dark = 0;
    for (int value : borders) if (value > black + 100) ++bright;
    for (int value : rings) if (value < white - 100) ++dark;
    if (bright < 3 || dark < 2 || white - black < 140 || center < black + 100) return -1;
    return white - black + center - black;

}

bool solve_homography(const double src[4][2], const double dst[4][2], double H[9])
{
    double A[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double x = src[i][0];
        const double y = src[i][1];
        const double u = dst[i][0];
        const double v = dst[i][1];
        A[i * 2][0] = x;
        A[i * 2][1] = y;
        A[i * 2][2] = 1;
        A[i * 2][6] = -u * x;
        A[i * 2][7] = -u * y;
        A[i * 2][8] = u;
        A[i * 2 + 1][3] = x;
        A[i * 2 + 1][4] = y;
        A[i * 2 + 1][5] = 1;
        A[i * 2 + 1][6] = -v * x;
        A[i * 2 + 1][7] = -v * y;
        A[i * 2 + 1][8] = v;
    }
    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 8; ++row) {
            if (std::abs(A[row][col]) > std::abs(A[pivot][col])) {
                pivot = row;
            }
        }
        if (std::abs(A[pivot][col]) < 1e-10) {
            return false;
        }
        if (pivot != col) {
            for (int k = col; k < 9; ++k) {
                std::swap(A[col][k], A[pivot][k]);
            }
        }
        const double div = A[col][col];
        for (int k = col; k < 9; ++k) {
            A[col][k] /= div;
        }
        for (int row = 0; row < 8; ++row) {
            if (row == col) {
                continue;
            }
            const double f = A[row][col];
            for (int k = col; k < 9; ++k) {
                A[row][k] -= f * A[col][k];
            }
        }
    }
    for (int i = 0; i < 8; ++i) {
        H[i] = A[i][8];
    }
    H[8] = 1;
    return true;
}

uint16_t sample_bilinear(const uint16_t* luma, int stride, int w, int h, double x, double y)
{
    if (x < 0 || y < 0 || x > w - 1 || y > h - 1) {
        return kYBackground;
    }
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(w - 1, x0 + 1);
    const int y1 = std::min(h - 1, y0 + 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const double v00 = luma[static_cast<size_t>(y0) * stride + x0];
    const double v10 = luma[static_cast<size_t>(y0) * stride + x1];
    const double v01 = luma[static_cast<size_t>(y1) * stride + x0];
    const double v11 = luma[static_cast<size_t>(y1) * stride + x1];
    const double v = v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy) + v01 * (1 - fx) * fy + v11 * fx * fy;
    return static_cast<uint16_t>(std::lround(std::clamp(v, 0.0, 1023.0)));
}

bool pose_from_hits(const Hit hits[4], int image_w, int image_h, PicturePose& pose)
{
    Hit corner[4];
    for (int i = 0; i < 4; ++i) {
        corner[i] = hits[i];
    }
    std::sort(corner, corner + 4, [](const Hit& a, const Hit& b) { return a.y < b.y; });
    std::sort(corner, corner + 2, [](const Hit& a, const Hit& b) { return a.x < b.x; });
    std::sort(corner + 2, corner + 4, [](const Hit& a, const Hit& b) { return a.x < b.x; });
    const Hit& tl = corner[0];
    const Hit& tr = corner[1];
    const Hit& bl = corner[2];
    const Hit& br = corner[3];
    if (!(tl.x < tr.x && bl.x < br.x && tl.y < bl.y && tr.y < br.y)) {
        return false;
    }
    const int sizes[4] = {tl.size, tr.size, bl.size, br.size};
    const int smin = *std::min_element(sizes, sizes + 4);
    const int smax = *std::max_element(sizes, sizes + 4);
    if (smin < 8 || smax > smin * 2) {
        return false;
    }
    pose.corner_x[0] = tl.x;
    pose.corner_y[0] = tl.y;
    pose.corner_x[1] = tr.x + tr.size;
    pose.corner_y[1] = tr.y;
    pose.corner_x[2] = br.x + br.size;
    pose.corner_y[2] = br.y + br.size;
    pose.corner_x[3] = bl.x;
    pose.corner_y[3] = bl.y + bl.size;
    pose.origin_x = pose.corner_x[0];
    pose.origin_y = pose.corner_y[0];
    pose.width = ((pose.corner_x[1] - pose.corner_x[0]) + (pose.corner_x[2] - pose.corner_x[3])) * 0.5;
    pose.height = ((pose.corner_y[3] - pose.corner_y[0]) + (pose.corner_y[2] - pose.corner_y[1])) * 0.5;
    pose.fiducial_size = (smin + smax) * 0.5;
    if (pose.width < 32 || pose.height < 32 || pose.width > image_w * 1.05 || pose.height > image_h * 1.05) {
        return false;
    }
    // Fiducials occupy 6% of the test-picture height. Small QR/strip squares
    // can score as nested squares too; reject quartets with impossible scale.
    const double relative_fid = pose.fiducial_size / pose.height;
    const double top = pose.corner_x[1] - pose.corner_x[0];
    const double bottom = pose.corner_x[2] - pose.corner_x[3];
    const double left = pose.corner_y[3] - pose.corner_y[0];
    const double right = pose.corner_y[2] - pose.corner_y[1];
    if (relative_fid < 0.035 || relative_fid > 0.10 || top < bottom * 0.8 || bottom < top * 0.8
        || left < right * 0.8 || right < left * 0.8) return false;
    if (std::abs(pose.corner_x[3] - pose.corner_x[0]) > pose.height * 0.35
        || std::abs(pose.corner_x[2] - pose.corner_x[1]) > pose.height * 0.35
        || std::abs(pose.corner_y[1] - pose.corner_y[0]) > pose.width * 0.25
        || std::abs(pose.corner_y[2] - pose.corner_y[3]) > pose.width * 0.25) return false;
    const double aspect = pose.width / pose.height;
    if (aspect < 1.2 || aspect > 2.4) {
        return false;
    }
    pose.found = true;
    return true;
}

} // namespace

double marker_row_time_ms(double frame_start_ms, double readout_ms, int row, int height)
{
    if (readout_ms <= 0 || height <= 1) {
        return frame_start_ms;
    }
    const double frac = std::clamp(static_cast<double>(row) / static_cast<double>(height - 1), 0.0, 1.0);
    return frame_start_ms + readout_ms * frac;
}

PicturePose locate_picture(const uint16_t* luma, int width, int height, int stride)
{
    PicturePose pose;
    if (luma == nullptr || width < 32 || height < 32) {
        return pose;
    }
    std::vector<Hit> raw;
    const int sizes[] = {8, 12, 16, 20, 24, 32, 40, 48, 64, 80, 96};
    for (int size : sizes) {
        if (size > height / 3 || size > width / 3) {
            continue;
        }
        const int step = std::max(2, size / 5);
        for (int y = 0; y + size <= height; y += step) {
            for (int x = 0; x + size <= width; x += step) {
                const double score = score_fiducial(luma, stride, width, height, x, y, size);
                if (score > 0) {
                    raw.push_back(Hit{x, y, size, score});
                }
            }
        }
    }
    std::sort(raw.begin(), raw.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    std::vector<Hit> kept;
    for (const auto& hit : raw) {
        bool near = false;
        for (const auto& prev : kept) {
            const int dx = hit.x - prev.x;
            const int dy = hit.y - prev.y;
            if (dx * dx + dy * dy < (hit.size * hit.size) / 2) {
                near = true;
                break;
            }
        }
        if (!near) {
            kept.push_back(hit);
        }
        if (kept.size() >= 128) {
            break;
        }
    }
    if (kept.size() < 4) {
        return pose;
    }
    PicturePose best;
    double best_score = -1;
    // Build corner constellations spatially rather than trying every quartet.
    // This lets real, slightly blurred screen corners survive QR/text clutter.
    for (const Hit& tl : kept) for (const Hit& tr : kept) {
        const double horizontal = tr.x - tl.x;
        if (horizontal < std::max(tl.size, tr.size) * 10 || std::abs(tr.y - tl.y) > horizontal * 0.25) continue;
        for (const Hit& bl : kept) {
            const double vertical = bl.y - tl.y;
            if (vertical < tl.size * 8 || std::abs(bl.x - tl.x) > vertical * 0.35) continue;
            if (horizontal / vertical < 1.2 || horizontal / vertical > 2.5) continue;
            const double bx = tr.x + bl.x - tl.x, by = tr.y + bl.y - tl.y;
            for (const Hit& br : kept) {
                if (std::abs(br.x - bx) > horizontal * 0.15 || std::abs(br.y - by) > vertical * 0.15) continue;
                Hit four[4] = {tl, tr, bl, br}; PicturePose candidate;
                if (!pose_from_hits(four, width, height, candidate)) continue;
                const double canonical[4][2]={{0,0},{1919,0},{1919,1079},{0,1079}};
                const double corners[4][2]={{candidate.corner_x[0],candidate.corner_y[0]},{candidate.corner_x[1],candidate.corner_y[1]},{candidate.corner_x[2],candidate.corner_y[2]},{candidate.corner_x[3],candidate.corner_y[3]}};
                double H[9]; if (!solve_homography(canonical,corners,H)) continue;
                const auto geometry=frame_geometry(1920,1080);
                auto strip_sample=[&](int index){double x=geometry.strip_x+(index+0.5)*geometry.strip_w,y=geometry.strip_y+geometry.strip_h*0.5;double den=H[6]*x+H[7]*y+1;return sample_bilinear(luma,stride,width,height,(H[0]*x+H[1]*y+H[2])/den,(H[3]*x+H[4]*y+H[5])/den);};
                const double contrast=(strip_sample(29)+strip_sample(31))*0.5-strip_sample(30);
                const double aspect_penalty=std::abs(std::log((candidate.width/candidate.height)/(16.0/9.0)));
                const double score = tl.score + tr.score + bl.score + br.score + std::sqrt(candidate.width * candidate.height) * 10 + contrast*8 - aspect_penalty*3000;
                if (score > best_score) { best_score = score; best = candidate; }
            }
        }
    }
    return best;
}

LocatedMarker read_located_marker(const uint16_t* luma, int width, int height, int stride)
{
    return read_located_marker_at_pose(luma, width, height, stride, locate_picture(luma, width, height, stride));
}

LocatedMarker read_located_marker_at_pose(const uint16_t* luma, int width, int height, int stride, const PicturePose& pose, int canonical_width)
{
    LocatedMarker located;
    located.pose = pose;
    if (!located.pose.found) {
        located.reading.quality = "invalid";
        return located;
    }
    located.located = true;
    located.width = canonical_width > 0 ? canonical_width : std::max(32, static_cast<int>(std::lround(located.pose.width)));
    located.height = canonical_width > 0 ? canonical_width * 9 / 16 : std::max(32, static_cast<int>(std::lround(located.pose.height)));
    located.canonical.assign(static_cast<size_t>(located.width * located.height), kYBackground);
    const double src[4][2] = {{0, 0}, {static_cast<double>(located.width - 1), 0},
        {static_cast<double>(located.width - 1), static_cast<double>(located.height - 1)},
        {0, static_cast<double>(located.height - 1)}};
    const double dst[4][2] = {{located.pose.corner_x[0], located.pose.corner_y[0]},
        {located.pose.corner_x[1], located.pose.corner_y[1]}, {located.pose.corner_x[2], located.pose.corner_y[2]},
        {located.pose.corner_x[3], located.pose.corner_y[3]}};
    double H[9] = {};
    if (!solve_homography(src, dst, H)) {
        located.located = false;
        located.reading.quality = "invalid";
        return located;
    }
    for (int y = 0; y < located.height; ++y) {
        for (int x = 0; x < located.width; ++x) {
            const double den = H[6] * x + H[7] * y + H[8];
            if (std::abs(den) < 1e-9) {
                continue;
            }
            const double ix = (H[0] * x + H[1] * y + H[2]) / den;
            const double iy = (H[3] * x + H[4] * y + H[5]) / den;
            located.canonical[static_cast<size_t>(y) * located.width + x] = sample_bilinear(luma, stride, width, height, ix, iy);
        }
    }
    located.reading = read_marker(located.canonical.data(), located.width, located.height, located.width);
    return located;
}

} // namespace avsync
