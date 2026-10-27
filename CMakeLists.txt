cmake_minimum_required(VERSION 3.24)
project(mxl-avsync VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AVSYNC_CORE_ONLY "Build only avsync-core and its tests" OFF)
option(AVSYNC_BUILD_TESTFILE_TOOL "Build the portable testfile generator/analyzer" OFF)
option(AVSYNC_WITH_FFMPEG "Build the file generator and analyzer" ON)
# Live MXL is linked when find_package(mxl) succeeds. avsync-core stays free of libmxl.

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
    core/src/timecode.cpp
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

add_executable(avsync-core-tests tests/unit/test_core.cpp tests/unit/test_stage23.cpp tests/unit/test_timecode.cpp)
target_include_directories(avsync-core-tests PRIVATE third_party/doctest)
target_link_libraries(avsync-core-tests PRIVATE avsync-core)
target_compile_options(avsync-core-tests PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(avsync-timecode-tool tests/tools/timecode_tool.cpp)
target_include_directories(avsync-timecode-tool PRIVATE third_party)
target_link_libraries(avsync-timecode-tool PRIVATE avsync-core)

enable_testing()
add_test(NAME core COMMAND avsync-core-tests)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(avsync-clock-tests tests/unit/test_clock.cpp ops/ptp.cpp)
    target_include_directories(avsync-clock-tests PRIVATE core/include ops third_party third_party/doctest)
    target_compile_definitions(avsync-clock-tests PRIVATE AVSYNC_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(avsync-clock-tests PRIVATE pthread)
    target_compile_options(avsync-clock-tests PRIVATE -Wall -Wextra)
    add_test(NAME clock COMMAND avsync-clock-tests)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
if(FFMPEG_FOUND AND NOT AVSYNC_CORE_ONLY)
    # Readers only. Writers would pull in the bundled zint encoder, which this repo does not use.
    set(BUILD_SHARED_LIBS OFF)
    set(ZXING_READERS ON CACHE BOOL "" FORCE)
    set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
    set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
    set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(ZXING_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
    set(ZXING_C_API OFF CACHE BOOL "" FORCE)
    set(ZXING_DEPENDENCIES LOCAL CACHE STRING "" FORCE)
    FetchContent_Declare(zxing_src
        URL https://github.com/zxing-cpp/zxing-cpp/archive/refs/tags/v2.3.0.tar.gz
        URL_HASH SHA256=64e4139103fdbc57752698ee15b5f0b0f7af9a0331ecbdc492047e0772c417ba
    )
    FetchContent_GetProperties(zxing_src)
    if(NOT zxing_src_POPULATED)
        FetchContent_Populate(zxing_src)
    endif()
    add_subdirectory(${zxing_src_SOURCE_DIR} ${CMAKE_BINARY_DIR}/zxing EXCLUDE_FROM_ALL)
    add_library(avsync-file STATIC
        generator/picture.cpp
        generator/source_image.cpp
        third_party/nayuki/qrcodegen.cpp
        app/report.cpp
        app/file_tool.cpp
    )
    target_include_directories(avsync-file PUBLIC generator app core/include third_party third_party/nayuki)
    target_compile_definitions(avsync-file PUBLIC AVSYNC_ICON_PATH="${CMAKE_SOURCE_DIR}/web/icon.png")
    target_link_libraries(avsync-file PUBLIC avsync-core ZXing::ZXing PkgConfig::FFMPEG)
    if(AVSYNC_BUILD_TESTFILE_TOOL)
        add_executable(avsync-testfile-tool testfiles/tool.cpp)
        target_link_libraries(avsync-testfile-tool PRIVATE avsync-file)
    endif()
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
        ops/ptp.cpp
        ops/service.cpp
        nmos/ids.cpp
        nmos/route.cpp
        nmos/node.cpp
    )
    target_include_directories(mxl-avsync PRIVATE util ops nmos app)
    target_compile_definitions(mxl-avsync PRIVATE AVSYNC_WEB_DIR="${CMAKE_SOURCE_DIR}/web")
    target_link_libraries(mxl-avsync PRIVATE avsync-file pthread)
    target_compile_options(mxl-avsync PRIVATE -Wall -Wextra -Wno-unused-parameter)

    find_package(mxl CONFIG QUIET)
    if(mxl_FOUND)
        pkg_check_modules(JPEG REQUIRED IMPORTED_TARGET libjpeg)
        add_library(avsync-mxlio STATIC
            mxlio/domain.cpp
            mxlio/engine.cpp
            mxlio/jpeg.cpp
            mxlio/preview.cpp
        )
        target_include_directories(avsync-mxlio PUBLIC mxlio PRIVATE util)
        target_link_libraries(avsync-mxlio PUBLIC avsync-file mxl::mxl PkgConfig::JPEG)
        target_compile_options(avsync-mxlio PRIVATE -Wall -Wextra -Wno-unused-parameter)
        target_link_libraries(mxl-avsync PRIVATE avsync-mxlio)
        target_compile_definitions(mxl-avsync PRIVATE AVSYNC_WITH_MXL=1)
        if(MXL_RUNTIME_LIB_DIRS)
            set_target_properties(mxl-avsync PROPERTIES BUILD_RPATH "${MXL_RUNTIME_LIB_DIRS}")
        endif()
        add_executable(avsync-sample-flow tests/tools/sample_flow.cpp)
        target_link_libraries(avsync-sample-flow PRIVATE avsync-mxlio)
        target_compile_options(avsync-sample-flow PRIVATE -Wall -Wextra -Wno-unused-parameter)
        add_executable(avsync-pattern-writer tests/tools/pattern_writer.cpp)
        target_link_libraries(avsync-pattern-writer PRIVATE avsync-mxlio)
        target_compile_options(avsync-pattern-writer PRIVATE -Wall -Wextra -Wno-unused-parameter)
        if(MXL_RUNTIME_LIB_DIRS)
            set_target_properties(avsync-sample-flow avsync-pattern-writer PROPERTIES BUILD_RPATH "${MXL_RUNTIME_LIB_DIRS}")
        endif()
        message(STATUS "libmxl found: live reader and writer are linked into mxl-avsync")
    else()
        message(STATUS "libmxl not found: mxl-avsync has no live MXL path")
    endif()

    option(AVSYNC_WITH_NMOS "Build the NMOS node with nmos-cpp" OFF)
    set(NMOS_CPP_DIR "" CACHE PATH "nmos-cpp Development directory")
    if(AVSYNC_WITH_NMOS)
        if(NOT NMOS_CPP_DIR)
            message(FATAL_ERROR "AVSYNC_WITH_NMOS=ON requires -DNMOS_CPP_DIR")
        endif()
        set(NMOS_CPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(NMOS_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(NMOS_CPP_USE_SUPPLIED_JSON_SCHEMA_VALIDATOR ON CACHE BOOL "" FORCE)
        set(NMOS_CPP_USE_SUPPLIED_JWT_CPP ON CACHE BOOL "" FORCE)
        set(NMOS_CPP_USE_AVAHI OFF CACHE BOOL "" FORCE)
        set(CMAKE_CXX_STANDARD 17)
        add_subdirectory(${NMOS_CPP_DIR} ${CMAKE_BINARY_DIR}/nmos-cpp EXCLUDE_FROM_ALL)
        set(CMAKE_CXX_STANDARD 20)
        target_compile_definitions(mxl-avsync PRIVATE AVSYNC_WITH_NMOS=1)
        target_link_libraries(mxl-avsync PRIVATE nmos-cpp::nmos-cpp)
        set_source_files_properties(nmos/node.cpp PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            COMPILE_FLAGS "-std=c++17 -w")
        message(STATUS "nmos-cpp: IS-04/IS-05 node is linked into mxl-avsync")
    endif()
endif()
