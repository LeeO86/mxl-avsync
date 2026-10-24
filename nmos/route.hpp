#pragma once

#include <string>

namespace avsync {

struct NmosInputRoute {
    bool enable = false;
    std::string domain_id;
    std::string flow_id;
};

void set_input_route(bool video, NmosInputRoute route);
NmosInputRoute get_input_route(bool video);

} // namespace avsync
