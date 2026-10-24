#include "route.hpp"

#include <mutex>

namespace avsync {
namespace {

std::mutex g_mu;
NmosInputRoute g_video;
NmosInputRoute g_audio;

} // namespace

void set_input_route(bool video, NmosInputRoute route)
{
    std::lock_guard lock(g_mu);
    (video ? g_video : g_audio) = std::move(route);
}

NmosInputRoute get_input_route(bool video)
{
    std::lock_guard lock(g_mu);
    return video ? g_video : g_audio;
}

} // namespace avsync
