#pragma once
#include <jni.h>

namespace avsync {
// The shared renderer's text on Android goes through JNI to GeneratorText.kt (text_android.cpp).
// Every JNI call that renders text sets the calling thread's JNIEnv with this guard first.
class TextEnv {
public:
    explicit TextEnv(JNIEnv* env);
    ~TextEnv();
    TextEnv(const TextEnv&) = delete;
    TextEnv& operator=(const TextEnv&) = delete;

private:
    JNIEnv* previous_;
};
} // namespace avsync
