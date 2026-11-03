cmake_minimum_required(VERSION 3.22)
project(avsync-android CXX)

# The shared phone analyzer, compiled from the repository's core like the iOS Core_*.cpp wrappers.
set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
set(CORE_SOURCES chirp detect g711 grade locate marker optical_code phone phone_analyzer plan qr sync_scale timecode two_way)
list(TRANSFORM CORE_SOURCES PREPEND ${REPO}/core/src/)
list(TRANSFORM CORE_SOURCES APPEND .cpp)

add_library(avsync-jni SHARED src/main/cpp/avsync_jni.cpp ${CORE_SOURCES} ${REPO}/third_party/nayuki/qrcodegen.cpp)
set_target_properties(avsync-jni PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(avsync-jni PRIVATE ${REPO}/core/include ${REPO}/third_party/pocketfft ${REPO}/third_party/nayuki)
target_compile_options(avsync-jni PRIVATE -O2 -fexceptions)
target_link_options(avsync-jni PRIVATE -Wl,-z,max-page-size=16384)
target_link_libraries(avsync-jni PRIVATE log)
