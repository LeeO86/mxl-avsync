# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.28.3)

# Reject any attempt to use a toolchain file. We must not use one because
# we could be downloading it here. If the CMAKE_TOOLCHAIN_FILE environment
# variable is set, the cache variable will have been initialized from it.
unset(CMAKE_TOOLCHAIN_FILE CACHE)
unset(ENV{CMAKE_TOOLCHAIN_FILE})

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(blend2d_src-populate NONE)



include(ExternalProject)
ExternalProject_Add(blend2d_src-populate
                     "UPDATE_DISCONNECTED" "False" "DOWNLOAD_EXTRACT_TIMESTAMP" "FALSE" "EXTERNALPROJECT_INTERNAL_ARGUMENT_SEPARATOR" "URL" "https://blend2d.com/download/blend2d-0.21.2.tar.gz" "URL_HASH" "SHA256=234ba57a64153c96885a6271f15fdcf6c594ac8cb03adaad19abb8d3b2b533fa"
                    SOURCE_DIR          "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/blend2d_src-src"
                    BINARY_DIR          "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/blend2d_src-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
                    USES_TERMINAL_PATCH     YES
)


