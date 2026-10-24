cmake_minimum_required(VERSION 3.24)
project(mxl-avsync VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AVSYNC_CORE_ONLY "Build only avsync-core and its tests" OFF)
option(AVSYNC_WITH_FFMPEG "Build the file generator and analyzer" ON)
option(AVSYNC_WITH_MXL "Build the live MXL path" OFF)

add_library(avsync-core STATIC
    core/src/plan.cpp
    core/src/chirp.cpp
    core/src/detect.cpp
    core/src/marker.cpp
    core/src/grade.cpp
    core/src/qr.cpp
    core/src/g711.cpp
    core/src/text.cpp
)
target_include_directories(avsync-core
    PUBLIC core/include
    PRIVATE third_party/pocketfft
    PRIVATE third_party/font8x8
)
target_compile_options(avsync-core PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(avsync-core-tests tests/unit/test_core.cpp)
target_include_directories(avsync-core-tests PRIVATE third_party/doctest)
target_link_libraries(avsync-core-tests PRIVATE avsync-core)
target_compile_options(avsync-core-tests PRIVATE -Wall -Wextra -Wno-unused-parameter)

enable_testing()
add_test(NAME core COMMAND avsync-core-tests)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
if(FFMPEG_FOUND AND NOT AVSYNC_CORE_ONLY)
    add_library(quirc STATIC
        third_party/quirc/quirc.c
        third_party/quirc/decode.c
        third_party/quirc/identify.c
        third_party/quirc/version_db.c
    )
    target_include_directories(quirc PUBLIC third_party/quirc)
    target_link_libraries(quirc PUBLIC m)
    add_library(avsync-file STATIC
        generator/picture.cpp
        third_party/nayuki/qrcodegen.cpp
        app/report.cpp
        app/file_tool.cpp
    )
    target_include_directories(avsync-file PUBLIC generator app core/include third_party third_party/nayuki third_party/quirc)
    target_link_libraries(avsync-file PUBLIC avsync-core quirc PkgConfig::FFMPEG)
    target_compile_options(avsync-file PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_executable(mxl-avsync
        app/main.cpp
        app/config.cpp
        util/uuid.cpp
        ops/http.cpp
        ops/service.cpp
    )
    target_include_directories(mxl-avsync PRIVATE util ops)
    target_compile_definitions(mxl-avsync PRIVATE AVSYNC_WEB_DIR="${CMAKE_SOURCE_DIR}/web")
    target_link_libraries(mxl-avsync PRIVATE avsync-file pthread)
    target_compile_options(mxl-avsync PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
