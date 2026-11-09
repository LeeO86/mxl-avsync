// JNI bridge to avsync::PhoneGeneratorPlan, the shared picture renderer and the calibration pattern,
// like ios/AVSyncGenerator.mm. One handle per Kotlin PhoneGenerator. image() runs on a worker while
// frame() runs on the main thread: both only read the plan, and image() renders from a copy of the info.
#include "avsync/calibrator.hpp"
#include "avsync/phone_generator.hpp"
#include "picture.hpp"
#include "text_android.hpp"

#include <jni.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace {
struct Generator {
    std::unique_ptr<avsync::PhoneGeneratorPlan> plan;
    avsync::PictureInfo info;
    avsync::SourceStill source_image;
    avsync::SourceStill app_icon;
    avsync::SenderCalibration calibration;
};
Generator* self(jlong handle) { return reinterpret_cast<Generator*>(handle); }

std::string utf8(JNIEnv* env, jstring value)
{
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string out(chars ? chars : "");
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

// 10-bit BT.709 limited range to 8-bit RGB, the same as the iOS bridge.
void rgb(uint16_t y, uint16_t cb, uint16_t cr, uint8_t* out)
{
    const double l = (double(y) - 64) / 876, u = (double(cb) - 512) / 896, v = (double(cr) - 512) / 896;
    out[0] = uint8_t(std::lround(std::clamp(l + 1.5748 * v, 0.0, 1.0) * 255));
    out[1] = uint8_t(std::lround(std::clamp(l - .187324 * u - .468124 * v, 0.0, 1.0) * 255));
    out[2] = uint8_t(std::lround(std::clamp(l + 1.8556 * u, 0.0, 1.0) * 255));
}

// ARGB ints for Bitmap.createBitmap. white_only: the sync foreground mask (white or transparent).
jintArray pixels(JNIEnv* env, const avsync::Yuv422p10& frame, bool white_only = false)
{
    const jsize n = jsize(frame.width) * frame.height;
    std::vector<jint> argb(size_t(n), 0);
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x) {
            const size_t i = size_t(y) * frame.width + x, ci = size_t(y) * (frame.width / 2) + x / 2;
            if (white_only) { argb[i] = frame.y[i] == avsync::kYWhite ? jint(0xFFFFFFFF) : 0; continue; }
            uint8_t c[3];
            rgb(frame.y[i], frame.cb[ci], frame.cr[ci], c);
            argb[i] = jint(0xFF000000u | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2]);
        }
    jintArray out = env->NewIntArray(n);
    env->SetIntArrayRegion(out, 0, n, argb.data());
    return out;
}

// A still from ARGB ints (straight alpha, already scaled to at most 512 px by the caller).
void still(JNIEnv* env, jintArray argb, jint width, jint height, avsync::SourceStill& out)
{
    out = {};
    if (!argb || width <= 0 || height <= 0 || env->GetArrayLength(argb) < width * height) return;
    std::vector<jint> in(size_t(width) * height);
    env->GetIntArrayRegion(argb, 0, width * height, in.data());
    out.width = width; out.height = height; out.rgba.resize(in.size() * 4);
    for (size_t i = 0; i < in.size(); ++i) {
        const auto p = uint32_t(in[i]);
        out.rgba[i * 4] = uint8_t(p >> 16); out.rgba[i * 4 + 1] = uint8_t(p >> 8); out.rgba[i * 4 + 2] = uint8_t(p); out.rgba[i * 4 + 3] = uint8_t(p >> 24);
    }
}
} // namespace

#define GENERATOR(name) Java_io_leeo86_mxl_avsync_PhoneGenerator_##name

extern "C" {

JNIEXPORT jstring JNICALL GENERATOR(testId)(JNIEnv* env, jclass, jstring prefix, jstring install, jstring version, jobjectArray settings)
{
    std::vector<std::string> values;
    for (jsize i = 0, n = settings ? env->GetArrayLength(settings) : 0; i < n; ++i) {
        auto item = jstring(env->GetObjectArrayElement(settings, i));
        values.push_back(utf8(env, item));
        env->DeleteLocalRef(item);
    }
    return env->NewStringUTF(avsync::generator_test_id(utf8(env, prefix), utf8(env, install), utf8(env, version), values).c_str());
}

// Returns 0 when the plan is invalid (the core throws).
JNIEXPORT jlong JNICALL GENERATOR(create)(JNIEnv* env, jclass, jstring testId, jint rate, jlong epochTaiNs, jdouble epochHost, jstring source)
{
    try {
        auto g = std::make_unique<Generator>();
        g->plan = std::make_unique<avsync::PhoneGeneratorPlan>(utf8(env, testId), int(rate), int64_t(epochTaiNs), double(epochHost));
        g->info.title = "AVsync"; g->info.source = utf8(env, source);
        g->info.format_line = "720p60 / 1200 baud / " + std::to_string(rate) + " Hz";
        g->info.notes = "Estimated phone clock/output"; g->info.offset_line = "Set AV offset 0 ms";
        g->info.period_ms = 1000.0 / rate; g->info.show_sync_indicator = false;
        return reinterpret_cast<jlong>(g.release());
    } catch (const std::exception&) { return 0; }
}
JNIEXPORT void JNICALL GENERATOR(destroy)(JNIEnv*, jclass, jlong h) { delete self(h); }

JNIEXPORT void JNICALL GENERATOR(setSource)(JNIEnv* env, jclass, jlong h, jstring name, jstring subtitle, jintArray argb, jint width, jint height)
{
    auto* g = self(h);
    const auto shown = utf8(env, name);
    if (!shown.empty()) g->info.source = shown;
    g->info.subtitle = utf8(env, subtitle);
    still(env, argb, width, height, g->source_image);
    g->info.source_image = g->source_image.width > 0 ? &g->source_image : nullptr;
}
JNIEXPORT void JNICALL GENERATOR(setIcon)(JNIEnv* env, jclass, jlong h, jintArray argb, jint width, jint height)
{
    auto* g = self(h);
    still(env, argb, width, height, g->app_icon);
    g->info.app_icon = g->app_icon.width > 0 ? &g->app_icon : nullptr;
}

// AVS4 sender calibration (this phone's screen vs speaker); method none unless valid.
JNIEXPORT void JNICALL GENERATOR(setSenderCalibration)(JNIEnv* env, jclass, jlong h, jstring method, jdouble offset, jdouble uncertainty, jint day)
{
    auto& c = self(h)->calibration;
    c = {};
    const auto m = utf8(env, method);
    // The QR encoder throws on values it cannot carry: keep method none then.
    if ((m != "clap" && m != "transponder" && m != "pulse-train") || !std::isfinite(offset) || std::abs(offset) > 3276 || !std::isfinite(uncertainty) ||
        uncertainty < 0 || uncertainty > 127 || day < 0 || day > 0xFFFF)
        return;
    c.method = m; c.offset_ms = offset; c.uncertainty_ms = uncertainty; c.day = int(day);
}

// [markerX, markerY, markerSide] in the 1280 × 720 raster.
JNIEXPORT jintArray JNICALL GENERATOR(geometry)(JNIEnv* env, jclass, jlong)
{
    const auto g = avsync::qr_flash_geometry(1280, 720);
    const jint values[3] = {jint(g.marker_x), jint(g.marker_y), jint(g.marker_side)};
    jintArray out = env->NewIntArray(3);
    env->SetIntArrayRegion(out, 0, 3, values);
    return out;
}

JNIEXPORT jintArray JNICALL GENERATOR(syncForeground)(JNIEnv* env, jclass, jlong h)
{
    auto* g = self(h);
    avsync::TextEnv text(env);
    avsync::Yuv422p10 frame;
    frame.resize(1280, 720);
    avsync::render_sync_foreground(frame, g->plan->period_frames(), g->info.period_ms, true);
    return pixels(env, frame, true);
}

// The frame shown at this host time: [sequence, position, aWhite, named, syncX, syncY, syncSize, syncR, syncG, syncB].
JNIEXPORT jdoubleArray JNICALL GENERATOR(frame)(JNIEnv* env, jclass, jlong h, jdouble target)
{
    auto* g = self(h);
    const auto frame = g->plan->frame_at(target);
    const int period = g->plan->period_frames();
    const uint64_t seq = 1 + frame / period;
    const int pos = int(frame % period);
    const auto dot = avsync::sync_indicator(1280, 720, pos, period, g->info.period_ms, true);
    uint8_t c[3];
    rgb(dot.y10, dot.cb10, dot.cr10, c);
    const jdouble values[10] = {double(seq), double(pos), g->plan->a_white(frame) ? 1.0 : 0.0, double(g->plan->named_seq(frame)),
        double(dot.x), double(dot.y), double(dot.side), double(c[0]), double(c[1]), double(c[2])};
    jdoubleArray out = env->NewDoubleArray(10);
    env->SetDoubleArrayRegion(out, 0, 10, values);
    return out;
}

JNIEXPORT jdouble JNICALL GENERATOR(hostTime)(JNIEnv*, jclass, jlong h, jlong seq)
{
    try { return self(h)->plan->host_seconds(uint64_t(seq)); } catch (const std::exception&) { return NAN; }
}
JNIEXPORT jfloatArray JNICALL GENERATOR(audio)(JNIEnv* env, jclass, jlong h, jlong seq)
{
    std::vector<float> samples;
    try { samples = self(h)->plan->audio(uint64_t(seq)); } catch (const std::exception&) {}
    jfloatArray out = env->NewFloatArray(jsize(samples.size()));
    env->SetFloatArrayRegion(out, 0, jsize(samples.size()), samples.data());
    return out;
}
JNIEXPORT jboolean JNICALL GENERATOR(retime)(JNIEnv*, jclass, jlong h, jlong offsetNs, jlong fromSeq, jlong maxStepNs)
{
    return fromSeq > 0 && self(h)->plan->retime(int64_t(offsetNs), uint64_t(fromSeq), int64_t(maxStepNs));
}
JNIEXPORT jdouble JNICALL GENERATOR(clockOffsetMs)(JNIEnv*, jclass, jlong h, jlong seq)
{
    return seq > 0 ? double(self(h)->plan->clock_offset_ns(uint64_t(seq))) / 1e6 : 0.0;
}
JNIEXPORT jboolean JNICALL GENERATOR(clockReferenceExpired)(JNIEnv*, jclass, jdouble ageMs, jdouble radiusMs, jdouble limitMs)
{
    return avsync::clock_reference_expired(ageMs, radiusMs, limitMs);
}
// The test picture for this event: AVS4 QR with the shown name and the sender calibration.
JNIEXPORT jintArray JNICALL GENERATOR(image)(JNIEnv* env, jclass, jlong h, jlong seq)
{
    auto* g = self(h);
    avsync::TextEnv text(env);
    try {
        auto qr = g->plan->qr(uint64_t(seq));
        qr.name = g->info.source; qr.calibration = g->calibration;
        auto info = g->info;
        info.qr_json = avsync::qr_payload_text(qr);
        info.counter = "Event " + std::to_string(seq); info.time_line = "Scheduled source output";
        avsync::Yuv422p10 frame;
        frame.resize(1280, 720);
        avsync::render_picture(frame, info, uint64_t(seq), 0, g->plan->period_frames(), false);
        return pixels(env, frame);
    } catch (const std::exception&) { return nullptr; }
}

// ---- Calibration pattern (docs/calibrator.md display mode; core/include/avsync/calibrator.hpp) ----

JNIEXPORT jboolean JNICALL GENERATOR(calibrationLeftWhite)(JNIEnv*, jclass, jlong frame, jint fps) { return avsync::calibration_left_white(uint64_t(frame), fps, 1); }
JNIEXPORT jlong JNICALL GENERATOR(calibrationSwapFrame)(JNIEnv*, jclass, jlong swap, jint fps) { return jlong(avsync::calibration_swap_frame(uint64_t(swap), fps, 1)); }
JNIEXPORT jfloatArray JNICALL GENERATOR(calibrationBeep)(JNIEnv* env, jclass, jint rate)
{
    const auto beep = avsync::calibration_beep(rate);
    jfloatArray out = env->NewFloatArray(jsize(beep.size()));
    env->SetFloatArrayRegion(out, 0, jsize(beep.size()), beep.data());
    return out;
}
// Clicks and beeps in mono float samples: pairs, clicks and, with enough pairs, the result (JSON text).
JNIEXPORT jstring JNICALL GENERATOR(displayCalibration)(JNIEnv* env, jclass, jfloatArray samples, jint count, jint rate, jint minimumPairs)
{
    std::vector<float> mono(size_t(std::max(0, std::min(count, samples ? env->GetArrayLength(samples) : 0))));
    if (!mono.empty()) env->GetFloatArrayRegion(samples, 0, jsize(mono.size()), mono.data());
    const auto sounds = avsync::find_sound_onsets(mono.data(), mono.size(), rate, 0);
    avsync::DisplayCalibrationOptions options;
    options.min_pairs = std::max(1, int(minimumPairs));
    const auto r = avsync::display_calibration(sounds, options);
    long clicks = 0;
    for (const auto& sound : sounds) clicks += sound.click();
    char text[512];
    if (!r.ok) {
        std::string error;
        for (char c : r.error) if (c != '"' && c != '\\') error += c;
        std::snprintf(text, sizeof text, R"({"ok":false,"error":"%s","clicks":%ld,"sounds":%zu,"pairs":0})", error.c_str(), clicks, sounds.size());
    } else {
        std::snprintf(text, sizeof text,
            R"({"ok":true,"offsetMs":%.4f,"uncertaintyMs":%.4f,"spreadMs":%.4f,"pairs":%d,"rejected":%d,"directionMs":%.4f,"clicks":%ld,"sounds":%zu})",
            r.offset_ms, r.uncertainty_ms, r.spread_ms, r.pairs, r.rejected, r.direction_ms, clicks, sounds.size());
    }
    return env->NewStringUTF(text);
}

} // extern "C"
