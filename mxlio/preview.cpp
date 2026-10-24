#include "preview.hpp"

#include "jpeg.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace avsync {
namespace {

struct Slot {
    std::vector<uint8_t> pending;
    int width = 0;
    int height = 0;
    bool dirty = false;
    std::chrono::steady_clock::time_point last_copy{};
    std::vector<uint8_t> jpeg;
    bool have_jpeg = false;
};

std::mutex g_mu;
std::condition_variable g_cv;
std::unordered_map<std::string, Slot> g_slots;
std::thread g_worker;
bool g_run = false;

void encode_loop()
{
    while (true) {
        std::string slot_name;
        std::vector<uint8_t> pending;
        int width = 0;
        int height = 0;
        {
            std::unique_lock lock(g_mu);
            g_cv.wait(lock, [] { return !g_run || std::any_of(g_slots.begin(), g_slots.end(), [](const auto& item) { return item.second.dirty; }); });
            if (!g_run) {
                return;
            }
            for (auto& [name, slot] : g_slots) {
                if (!slot.dirty) {
                    continue;
                }
                slot_name = name;
                pending = slot.pending;
                width = slot.width;
                height = slot.height;
                slot.dirty = false;
                break;
            }
        }
        if (pending.empty()) {
            continue;
        }
        const int out_width = std::min(width, 640);
        auto jpeg = encode_v210_jpeg(pending.data(), width, height, out_width, 75);
        if (jpeg.empty()) {
            continue;
        }
        std::lock_guard lock(g_mu);
        auto& slot = g_slots[slot_name];
        slot.jpeg = std::move(jpeg);
        slot.have_jpeg = true;
    }
}

} // namespace

void start_preview_worker()
{
    std::lock_guard lock(g_mu);
    if (g_run) {
        return;
    }
    g_run = true;
    g_worker = std::thread(encode_loop);
}

void stop_preview_worker()
{
    {
        std::lock_guard lock(g_mu);
        g_run = false;
    }
    g_cv.notify_all();
    if (g_worker.joinable()) {
        g_worker.join();
    }
}

void note_preview_v210(const std::string& slot_name, const uint8_t* v210, int width, int height)
{
    if (v210 == nullptr || width < 2 || height < 2) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const size_t bytes = static_cast<size_t>(((width + 47) / 48) * 128) * static_cast<size_t>(height);
    {
        std::lock_guard lock(g_mu);
        if (!g_run) {
            return;
        }
        auto& slot = g_slots[slot_name];
        if (slot.last_copy.time_since_epoch().count() != 0 && now - slot.last_copy < std::chrono::seconds(1)) {
            return;
        }
        slot.pending.assign(v210, v210 + bytes);
        slot.width = width;
        slot.height = height;
        slot.dirty = true;
        slot.last_copy = now;
    }
    g_cv.notify_one();
}

bool preview_jpeg(const std::string& slot_name, std::vector<uint8_t>* jpeg)
{
    if (jpeg == nullptr) {
        return false;
    }
    std::lock_guard lock(g_mu);
    const auto it = g_slots.find(slot_name);
    if (it == g_slots.end() || !it->second.have_jpeg) {
        return false;
    }
    *jpeg = it->second.jpeg;
    return true;
}

} // namespace avsync
