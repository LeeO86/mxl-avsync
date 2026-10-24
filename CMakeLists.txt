cmake_minimum_required(VERSION 3.24)
project(mxl-avsync VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AVSYNC_CORE_ONLY "Build only avsync-core and its tests" OFF)
option(AVSYNC_WITH_FFMPEG "Build the file generator and analyzer" ON)
option(AVSYNC_WITH_MXL "Build the live MXL path" OFF)

# Same pin and static options as mxl-multiviewer. The release tarball ships asmjit.
include(FetchContent)
FetchContent_Declare(blend2d_src
    URL https://blend2d.com/download/blend2d-0.21.2.tar.gz
    URL_HASH SHA256=234ba57a64153c96885a6271f15fdcf6c594ac8cb03adaad19abb8d3b2b533fa
)
FetchContent_GetProperties(blend2d_src)
if(NOT blend2d_src_POPULATED)
    FetchContent_Populate(blend2d_src)
endif()
set(BLEND2D_STATIC ON CACHE BOOL "" FORCE)
set(BLEND2D_TEST OFF CACHE BOOL "" FORCE)
set(BLEND2D_DEMOS OFF CACHE BOOL "" FORCE)
set(BLEND2D_NO_INSTALL ON CACHE BOOL "" FORCE)
set(BLEND2D_NO_JIT_LOGGING ON CACHE BOOL "" FORCE)
add_subdirectory(${blend2d_src_SOURCE_DIR} ${CMAKE_BINARY_DIR}/blend2d EXCLUDE_FROM_ALL)

set(DEJAVU_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/avsync/dejavu_sans.hpp)
add_custom_command(
    OUTPUT ${DEJAVU_HEADER}
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/third_party/dejavu/DejaVuSans.ttf
            -DOUTPUT=${DEJAVU_HEADER}
            -DNAMESPACE=avsync::font
            -DVAR=dejavuSans
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBytes.cmake
    DEPENDS third_party/dejavu/DejaVuSans.ttf cmake/EmbedBytes.cmake
    COMMENT "Embedding DejaVu Sans"
    VERBATIM
)

add_library(avsync-core STATIC
    core/src/plan.cpp
    core/src/chirp.cpp
    core/src/detect.cpp
    core/src/marker.cpp
    core/src/grade.cpp
    core/src/qr.cpp
    core/src/g711.cpp
    core/src/text.cpp
    core/src/locate.cpp
    core/src/phone.cpp
)
target_include_directories(avsync-core
    PUBLIC core/include
    PRIVATE third_party/pocketfft
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)
set_source_files_properties(core/src/text.cpp PROPERTIES OBJECT_DEPENDS "${DEJAVU_HEADER}")
target_link_libraries(avsync-core PUBLIC blend2d::blend2d)
target_compile_options(avsync-core PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(avsync-core-tests tests/unit/test_core.cpp tests/unit/test_stage23.cpp)
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
        generator/source_image.cpp
        third_party/nayuki/qrcodegen.cpp
        app/report.cpp
        app/file_tool.cpp
    )
    target_include_directories(avsync-file PUBLIC generator app core/include third_party third_party/nayuki third_party/quirc)
    target_compile_definitions(avsync-file PUBLIC AVSYNC_ICON_PATH="${CMAKE_SOURCE_DIR}/web/icon.png")
    target_link_libraries(avsync-file PUBLIC avsync-core quirc PkgConfig::FFMPEG)
    add_executable(avsync-picture-tests tests/unit/test_picture.cpp)
    target_include_directories(avsync-picture-tests PRIVATE third_party/doctest)
    target_compile_definitions(avsync-picture-tests PRIVATE AVSYNC_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(avsync-picture-tests PRIVATE avsync-file)
    target_compile_options(avsync-picture-tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME picture COMMAND avsync-picture-tests)
    target_compile_options(avsync-file PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_executable(mxl-avsync
        app/main.cpp
        app/paths.cpp
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
