# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-src"
  "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-build"
  "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-subbuild/zxing_src-populate-prefix"
  "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-subbuild/zxing_src-populate-prefix/tmp"
  "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-subbuild/zxing_src-populate-prefix/src/zxing_src-populate-stamp"
  "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-subbuild/zxing_src-populate-prefix/src"
  "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-subbuild/zxing_src-populate-prefix/src/zxing_src-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-subbuild/zxing_src-populate-prefix/src/zxing_src-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/_deps/zxing_src-subbuild/zxing_src-populate-prefix/src/zxing_src-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
