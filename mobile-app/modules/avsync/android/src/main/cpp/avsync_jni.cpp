// JNI bridge to avsync::PhoneAnalyzer. One handle per Kotlin PhoneAnalyzer; the caller serializes
// access (the camera view uses one capture thread, plus snapshots polled on a worker).
#include "avsync/phone_analyzer.hpp"

#include <jni.h>

using avsync::PhoneAnalyzer;

namespace {
PhoneAnalyzer* self(jlong handle) { return reinterpret_cast<PhoneAnalyzer*>(handle); }
jstring text(JNIEnv* env, const std::string& value) { return value.empty() ? nullptr : env->NewStringUTF(value.c_str()); }
} // namespace

#define AVSYNC(name) Java_io_leeo86_mxl_avsync_PhoneAnalyzer_##name

extern "C" {

JNIEXPORT jlong JNICALL AVSYNC(create)(JNIEnv*, jclass) { return reinterpret_cast<jlong>(new PhoneAnalyzer()); }
JNIEXPORT void JNICALL AVSYNC(destroy)(JNIEnv*, jclass, jlong h) { delete self(h); }
JNIEXPORT void JNICALL AVSYNC(reset)(JNIEnv*, jclass, jlong h) { self(h)->reset(); }
JNIEXPORT void JNICALL AVSYNC(setTaiUtcSeconds)(JNIEnv*, jclass, jlong h, jdouble s) { self(h)->set_tai_utc_seconds(s); }
JNIEXPORT void JNICALL AVSYNC(setDeviceUtcOffset)(JNIEnv*, jclass, jlong h, jdouble ms) { self(h)->set_device_utc_offset(ms); }

JNIEXPORT void JNICALL AVSYNC(observeQr)(JNIEnv* env, jclass, jlong h, jstring payload, jdoubleArray corners, jdouble captureMs)
{
    if (!payload || !corners || env->GetArrayLength(corners) != 8) return;
    const char* chars = env->GetStringUTFChars(payload, nullptr);
    const std::string value(chars);
    env->ReleaseStringUTFChars(payload, chars);
    jdouble c[8];
    env->GetDoubleArrayRegion(corners, 0, 8, c);
    self(h)->observe_qr(value, {c[0], c[1], c[2], c[3]}, {c[4], c[5], c[6], c[7]}, captureMs);
}
JNIEXPORT jboolean JNICALL AVSYNC(qrScanSuggested)(JNIEnv*, jclass, jlong h, jdouble ms) { return self(h)->qr_scan_suggested(ms); }

// luma: a direct buffer (the camera's Y plane), read in place.
JNIEXPORT void JNICALL AVSYNC(pushVideo)(JNIEnv* env, jclass, jlong h, jobject luma, jint width, jint height, jint stride, jdouble pts, jdouble exposure, jdouble readout)
{
    const auto* data = static_cast<const uint8_t*>(env->GetDirectBufferAddress(luma));
    if (!data || env->GetDirectBufferCapacity(luma) < jlong(stride) * (height - 1) + width) return;
    self(h)->push_video(data, width, height, stride, pts, exposure, readout);
}
JNIEXPORT void JNICALL AVSYNC(pushAudio)(JNIEnv* env, jclass, jlong h, jfloatArray samples, jint count, jdouble pts, jint rate)
{
    if (!samples || count <= 0 || count > env->GetArrayLength(samples)) return;
    jfloat* data = env->GetFloatArrayElements(samples, nullptr);
    self(h)->push_audio(data, size_t(count), pts, rate);
    env->ReleaseFloatArrayElements(samples, data, JNI_ABORT);
}

JNIEXPORT jboolean JNICALL AVSYNC(reportDue)(JNIEnv*, jclass, jlong h) { return self(h)->report_due(); }
JNIEXPORT jstring JNICALL AVSYNC(poll)(JNIEnv* env, jclass, jlong h) { return text(env, self(h)->poll()); }
JNIEXPORT jlong JNICALL AVSYNC(snapshotForPoll)(JNIEnv*, jclass, jlong h) { return reinterpret_cast<jlong>(new PhoneAnalyzer(self(h)->snapshot_for_poll())); }
JNIEXPORT void JNICALL AVSYNC(mergeAudio)(JNIEnv*, jclass, jlong h, jlong analyzed) { self(h)->merge_audio(*self(analyzed)); }

JNIEXPORT void JNICALL AVSYNC(beginCalibration)(JNIEnv*, jclass, jlong h) { self(h)->begin_calibration(); }
JNIEXPORT jboolean JNICALL AVSYNC(calibrating)(JNIEnv*, jclass, jlong h) { return self(h)->calibrating(); }
JNIEXPORT jstring JNICALL AVSYNC(calibrationError)(JNIEnv* env, jclass, jlong h)
{
    const auto error = self(h)->calibration_error();
    return error ? env->NewStringUTF(error->c_str()) : nullptr;
}

// Calibrator board take (docs/calibrator.md): camera (flash + click) or display (an external screen, microphone only).
JNIEXPORT void JNICALL AVSYNC(beginCalibrator)(JNIEnv*, jclass, jlong h, jboolean display)
{
    self(h)->begin_calibrator(display ? PhoneAnalyzer::CalibratorMode::Display : PhoneAnalyzer::CalibratorMode::Camera);
}
JNIEXPORT jboolean JNICALL AVSYNC(calibratorActive)(JNIEnv*, jclass, jlong h) { return self(h)->calibrator_active(); }
// A progress report about once a second, then the result or an error (JSON), else null.
JNIEXPORT jstring JNICALL AVSYNC(pollCalibrator)(JNIEnv* env, jclass, jlong h) { return text(env, self(h)->poll_calibrator()); }

// Returns a PhoneAnalyzer.CalibrationTake, or null when no take is ready.
JNIEXPORT jobject JNICALL AVSYNC(takeCalibration)(JNIEnv* env, jclass, jlong h)
{
    auto take = self(h)->take_calibration();
    if (!take) return nullptr;
    const auto n = jsize(take->frames.size());
    jdoubleArray times = env->NewDoubleArray(n);
    jintArray widths = env->NewIntArray(n), heights = env->NewIntArray(n);
    jobjectArray luma = env->NewObjectArray(n, env->FindClass("[B"), nullptr);
    for (jsize i = 0; i < n; ++i) {
        const auto& f = take->frames[size_t(i)];
        const jdouble t = f.time_ms;
        const jint w = f.width, hh = f.height;
        env->SetDoubleArrayRegion(times, i, 1, &t);
        env->SetIntArrayRegion(widths, i, 1, &w);
        env->SetIntArrayRegion(heights, i, 1, &hh);
        jbyteArray pixels = env->NewByteArray(jsize(f.luma.size()));
        env->SetByteArrayRegion(pixels, 0, jsize(f.luma.size()), reinterpret_cast<const jbyte*>(f.luma.data()));
        env->SetObjectArrayElement(luma, i, pixels);
        env->DeleteLocalRef(pixels);
    }
    jfloatArray audio = env->NewFloatArray(jsize(take->audio.size()));
    env->SetFloatArrayRegion(audio, 0, jsize(take->audio.size()), take->audio.data());
    jclass type = env->FindClass("io/leeo86/mxl/avsync/PhoneAnalyzer$CalibrationTake");
    jmethodID init = env->GetMethodID(type, "<init>", "([D[I[I[[B[FIDDD)V");
    return env->NewObject(type, init, times, widths, heights, luma, audio, jint(take->sample_rate), take->audio_start_ms, take->spike_ms, take->fps);
}

// Normalized corners x0..x3, y0..y3 of the detected picture, or null.
JNIEXPORT jdoubleArray JNICALL AVSYNC(picture)(JNIEnv* env, jclass, jlong h)
{
    std::array<double, 4> x{}, y{};
    if (!self(h)->picture(x, y)) return nullptr;
    jdoubleArray out = env->NewDoubleArray(8);
    env->SetDoubleArrayRegion(out, 0, 4, x.data());
    env->SetDoubleArrayRegion(out, 4, 4, y.data());
    return out;
}

} // extern "C"
