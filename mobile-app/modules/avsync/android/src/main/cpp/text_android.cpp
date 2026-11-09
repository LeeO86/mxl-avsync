// Text for the shared picture renderer on Android: Android's own text drawing (GeneratorText.kt) with
// the bundled DejaVu Sans, as iOS uses CoreText (ios/Mobile_text.mm) and the server Blend2D
// (core/src/text.cpp). Font size 8 × scale pixels on every target.
#include "text_android.hpp"

#include "avsync/avsync.hpp"

#include <algorithm>
#include <mutex>
#include <string>

namespace avsync {
namespace {
thread_local JNIEnv* current = nullptr;
jclass text_class = nullptr;
jmethodID width_id = nullptr, height_id = nullptr, draw_id = nullptr;

JNIEnv* env()
{
    if (!current) return nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        jclass local = current->FindClass("io/leeo86/mxl/avsync/GeneratorText");
        if (!local) { current->ExceptionClear(); return; }
        text_class = jclass(current->NewGlobalRef(local));
        current->DeleteLocalRef(local);
        width_id = current->GetStaticMethodID(text_class, "width", "(Ljava/lang/String;F)I");
        height_id = current->GetStaticMethodID(text_class, "height", "(F)I");
        draw_id = current->GetStaticMethodID(text_class, "draw", "(Ljava/lang/String;FII)[B");
        if (current->ExceptionCheck()) { current->ExceptionClear(); text_class = nullptr; }
    });
    return text_class ? current : nullptr;
}
float font_px(int scale) { return float(8 * std::max(1, scale)); }
jstring java_text(JNIEnv* e, std::string_view text) { return e->NewStringUTF(std::string(text).c_str()); }
} // namespace

TextEnv::TextEnv(JNIEnv* env) : previous_(current) { current = env; }
TextEnv::~TextEnv() { current = previous_; }

int text_px_width(std::string_view text, int scale)
{
    JNIEnv* e = env();
    if (text.empty() || !e) return 0;
    jstring s = java_text(e, text);
    const int width = e->CallStaticIntMethod(text_class, width_id, s, font_px(scale));
    e->DeleteLocalRef(s);
    if (e->ExceptionCheck()) { e->ExceptionClear(); return 0; }
    return std::max(0, width);
}

int text_px_height(int scale)
{
    JNIEnv* e = env();
    if (!e) return int(font_px(scale));
    const int height = e->CallStaticIntMethod(text_class, height_id, font_px(scale));
    if (e->ExceptionCheck()) { e->ExceptionClear(); return int(font_px(scale)); }
    return std::max(1, height);
}

void draw_text(LumaView view, int x, int y, std::string_view text, int scale, uint16_t value)
{
    JNIEnv* e = env();
    if (text.empty() || !view.data || !e) return;
    const int w = std::max(1, text_px_width(text, scale)), h = text_px_height(scale);
    jstring s = java_text(e, text);
    auto alpha = jbyteArray(e->CallStaticObjectMethod(text_class, draw_id, s, font_px(scale), w, h));
    e->DeleteLocalRef(s);
    if (e->ExceptionCheck()) { e->ExceptionClear(); return; }
    if (!alpha) return;
    // Rows may be padded (Bitmap row bytes): the stride is the array length over the height.
    const jsize length = e->GetArrayLength(alpha);
    const int stride = length / h;
    if (stride >= w) {
        jbyte* coverage = e->GetByteArrayElements(alpha, nullptr);
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col) {
                if (y + row < 0 || y + row >= view.height || x + col < 0 || x + col >= view.width) continue;
                const unsigned c = uint8_t(coverage[size_t(row) * stride + col]);
                auto& pixel = view.data[size_t(y + row) * view.stride + x + col];
                pixel = uint16_t((pixel * (255 - c) + value * c + 127) / 255);
            }
        e->ReleaseByteArrayElements(alpha, coverage, JNI_ABORT);
    }
    e->DeleteLocalRef(alpha);
}
} // namespace avsync
