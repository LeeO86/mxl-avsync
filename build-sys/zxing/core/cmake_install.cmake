# Install script for directory: /tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/zxing/core/libZXing.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/ZXing" TYPE FILE FILES
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/Barcode.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/BarcodeFormat.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/BitHacks.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/ByteArray.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/CharacterSet.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/Content.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/Error.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/Flags.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/GTIN.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/ImageView.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/Point.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/Quadrilateral.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/ReadBarcode.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/ReaderOptions.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/StructuredAppend.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/TextUtfEncoding.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/ZXingCpp.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/ZXAlgorithms.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/ZXVersion.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/DecodeHints.h"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src/core/src/Result.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/ZXing" TYPE FILE FILES "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/zxing/core/Version.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing/ZXingTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing/ZXingTargets.cmake"
         "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/zxing/core/CMakeFiles/Export/f9e04a807b27a41299a115186893fdf1/ZXingTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing/ZXingTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing/ZXingTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing" TYPE FILE FILES "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/zxing/core/CMakeFiles/Export/f9e04a807b27a41299a115186893fdf1/ZXingTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing" TYPE FILE FILES "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/zxing/core/CMakeFiles/Export/f9e04a807b27a41299a115186893fdf1/ZXingTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/zxing/core/zxing.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing" TYPE FILE FILES
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/zxing/core/ZXingConfig.cmake"
    "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/zxing/core/ZXingConfigVersion.cmake"
    )
endif()

