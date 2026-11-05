# CMake generated Testfile for 
# Source directory: /tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62
# Build directory: /tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[core]=] "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/avsync-core-tests")
set_tests_properties([=[core]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/CMakeLists.txt;86;add_test;/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/CMakeLists.txt;0;")
add_test([=[clock]=] "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/avsync-clock-tests")
set_tests_properties([=[clock]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/CMakeLists.txt;94;add_test;/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/CMakeLists.txt;0;")
add_test([=[picture]=] "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/build-sys/avsync-picture-tests")
set_tests_properties([=[picture]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/CMakeLists.txt;138;add_test;/tmp/claude-1000/-home-team42-mxl-lab/ab2ee25b-5594-4c0a-9c31-63e411ece620/scratchpad/pr62/CMakeLists.txt;0;")
subdirs("blend2d")
subdirs("zxing")
