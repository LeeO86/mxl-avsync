#pragma once

#include "config.hpp"

namespace avsync {

// Blocks until SIGTERM/SIGINT. Returns 143 on signal, 75 if a port cannot be bound.
int run_service(Config config);

} // namespace avsync
