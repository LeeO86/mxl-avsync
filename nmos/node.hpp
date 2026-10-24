#pragma once

#include "config.hpp"

#include <memory>

namespace avsync {

// nmos-cpp node. DNS-SD stays off unless NMOS_DNS_SD is set. Throws if the NMOS port is not ours.
class NmosNode {
public:
    explicit NmosNode(Config config);
    ~NmosNode();
    void start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace avsync
