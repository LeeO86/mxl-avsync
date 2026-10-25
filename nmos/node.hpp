#pragma once

#include "config.hpp"

#include <cstdint>
#include <memory>

namespace avsync {

// nmos-cpp node. DNS-SD stays off unless NMOS_DNS_SD is set. Throws if the NMOS port is not ours.
class NmosNode {
public:
    explicit NmosNode(Config config);
    ~NmosNode();
    void start();
    void stop();
    // True when a configured registry's Query API lists this node.
    bool registered() const;
    // IS-05 activations seen since start, including sender and receiver.
    uint64_t activations() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace avsync
