#pragma once

#include <string>
#include <vector>

namespace avsync {

struct StagePath {
    std::string id;
    std::string stage;
    bool runnable_here = false;
    std::string detail;
};

// Stage 2 and 3 paths. runnable_here is what this VM can execute.
std::vector<StagePath> stage_paths();
std::string stage_paths_text();

} // namespace avsync
