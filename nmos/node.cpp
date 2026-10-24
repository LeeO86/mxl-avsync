#include "node.hpp"

#include "avsync/avsync.hpp"
#include "ids.hpp"
#include "route.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(AVSYNC_WITH_NMOS)
#include "cpprest/host_utils.h"
#include "cpprest/http_client.h"
#include "nmos/channels.h"
#include "nmos/clock_name.h"
#include "nmos/colorspace.h"
#include "nmos/connection_resources.h"
#include "nmos/format.h"
#include "nmos/group_hint.h"
#include "nmos/interlace_mode.h"
#include "nmos/log_gate.h"
#include "nmos/model.h"
#include "nmos/mxl.h"
#include "nmos/node_interfaces.h"
#include "nmos/node_resource.h"
#include "nmos/node_resources.h"
#include "nmos/node_server.h"
#include "nmos/server.h"
#include "nmos/settings.h"
#include "nmos/transfer_characteristic.h"
#include "nmos/transport.h"
#include "sdp/json.h"
#endif

namespace avsync {
namespace {

bool uuid_ok(const std::string& text)
{
    return text.size() == 36 && text[8] == '-' && text[13] == '-' && text[18] == '-' && text[23] == '-';
}

#if defined(AVSYNC_WITH_NMOS)
utility::string_t us(const std::string& text)
{
    return utility::conversions::to_string_t(text);
}

std::string su(const utility::string_t& text)
{
    return utility::conversions::to_utf8string(text);
}

void tag_group(nmos::resource& resource, const std::string& group, const std::string& role)
{
    if (!resource.data.has_field(U("tags"))) {
        resource.data[U("tags")] = web::json::value::object();
    }
    web::json::push_back(resource.data[U("tags")][U("urn:x-nmos:tag:grouphint/v1.0")], nmos::make_group_hint({us(group), us(role)}));
}

void tag_test(nmos::resource& resource)
{
    if (!resource.data.has_field(U("tags"))) {
        resource.data[U("tags")] = web::json::value::object();
    }
    resource.data[U("tags")][U("urn:x-mxl:tag:test-signal/v1.0")] = web::json::value_of({web::json::value::string(U("mxl-avsync"))});
}

void apply_route(const nmos::resource& connection, const std::string& receiver_id, const NmosIds& ids)
{
    if (su(connection.id) != receiver_id || !connection.data.has_field(U("active"))) {
        return;
    }
    const auto& active = connection.data.at(U("active"));
    NmosInputRoute next;
    next.enable = active.has_field(U("master_enable")) && active.at(U("master_enable")).as_bool();
    if (active.has_field(U("transport_params")) && active.at(U("transport_params")).is_array() && active.at(U("transport_params")).size() > 0) {
        const auto& params = active.at(U("transport_params")).at(0);
        if (params.has_field(U("mxl_domain_id")) && params.at(U("mxl_domain_id")).is_string()) {
            next.domain_id = su(params.at(U("mxl_domain_id")).as_string());
        }
        if (params.has_field(U("mxl_flow_id")) && params.at(U("mxl_flow_id")).is_string()) {
            next.flow_id = su(params.at(U("mxl_flow_id")).as_string());
        }
    }
    if ((!next.domain_id.empty() && !uuid_ok(next.domain_id)) || (!next.flow_id.empty() && !uuid_ok(next.flow_id))) {
        throw std::runtime_error("mxl_domain_id and mxl_flow_id must be UUIDs");
    }
    if (next.domain_id == "00000000-0000-0000-0000-000000000000") {
        next.domain_id.clear();
    }
    if (!next.enable) {
        next.flow_id.clear();
    }
    set_input_route(receiver_id == ids.video_receiver, next);
}
#endif

} // namespace

struct NmosNode::Impl {
    Config config;
    NmosIds ids;
    std::atomic<bool> running{false};
    std::mutex ready_mu;
    std::condition_variable ready_cv;
    bool ready = false;
    std::string error;
#if defined(AVSYNC_WITH_NMOS)
    std::thread thread;
#endif

    explicit Impl(Config cfg)
        : config(std::move(cfg))
        , ids(make_nmos_ids(config.seed(), config.video_format, config.audio_channels, config.result_format))
    {
    }
};

NmosNode::NmosNode(Config config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

NmosNode::~NmosNode()
{
    stop();
}

#if !defined(AVSYNC_WITH_NMOS)
void NmosNode::start()
{
    if (impl_->config.nmos_enable) {
        throw std::runtime_error("this binary was built without nmos-cpp");
    }
}

void NmosNode::stop() {}
#else
void NmosNode::start()
{
    if (!impl_->config.nmos_enable) {
        return;
    }
    impl_->thread = std::thread([this] {
        nmos::experimental::log_model log_model;
        std::ostream error_log(std::cerr.rdbuf());
        std::filebuf discarded;
        std::ostream access_log(&discarded);
        nmos::experimental::log_gate gate(error_log, access_log, log_model);
        try {
            nmos::node_model node_model;
            const auto& config = impl_->config;
            const auto& ids = impl_->ids;
            web::json::value settings = web::json::value::object();
            const auto node_label = config.nmos_label.empty() ? config.host_id : config.nmos_label;
            settings[U("http_port")] = config.nmos_port;
            settings[U("label")] = web::json::value::string(us(node_label));
            settings[U("description")] = web::json::value::string(U("mxl-avsync"));
            settings[U("seed_id")] = web::json::value::string(us(ids.node));
            settings[U("service_name_prefix")] = web::json::value::string(U("mxl-avsync"));
            settings[U("logging_level")] = 20;
            settings[U("control_protocol_ws_port")] = -1;
            if (!config.nmos_host_address.empty()) {
                settings[U("host_address")] = web::json::value::string(us(config.nmos_host_address));
                settings[U("host_addresses")] = web::json::value::array({web::json::value::string(us(config.nmos_host_address))});
            }
            if (!config.nmos_dns_sd) {
                settings[U("pri")] = std::numeric_limits<int>::max();
                settings[U("highest_pri")] = std::numeric_limits<int>::max();
            }
            if (!config.nmos_registry_address.empty()) {
                settings[U("registry_address")] = web::json::value::string(us(config.nmos_registry_address));
                settings[U("registration_port")] = config.nmos_registry_port;
                if (!config.nmos_query_address.empty()) {
                    settings[U("query_address")] = web::json::value::string(us(config.nmos_query_address));
                }
                settings[U("query_port")] = config.nmos_query_port;
            }
            node_model.settings = settings;
            nmos::insert_node_default_settings(node_model.settings);
            log_model.settings = node_model.settings;
            log_model.level = nmos::fields::logging_level(log_model.settings);

            auto implementation = nmos::experimental::node_implementation()
                .on_parse_transport_file([](const nmos::resource&, const nmos::resource&, const utility::string_t&, const utility::string_t&,
                                              slog::base_gate&) -> web::json::value { throw std::runtime_error("MXL does not use a transport file"); })
                .on_resolve_auto([](const nmos::resource&, const nmos::resource&, web::json::value& params) {
                    if (!params.is_array() || params.size() == 0) {
                        return;
                    }
                    auto& leg = params.at(0);
                    nmos::details::resolve_auto(leg, U("mxl_domain_id"), [] { return web::json::value::string(U("00000000-0000-0000-0000-000000000000")); });
                    nmos::details::resolve_auto(leg, U("mxl_flow_id"), [] { return web::json::value::null(); });
                })
                .on_set_transportfile([](const nmos::resource&, const nmos::resource&, web::json::value& transport_file) { transport_file = web::json::value::null(); })
                .on_connection_activated([this](const nmos::resource&, const nmos::resource& connection) {
                    apply_route(connection, impl_->ids.video_receiver, impl_->ids);
                    apply_route(connection, impl_->ids.audio_receiver, impl_->ids);
                });

            auto server = nmos::experimental::make_node_server(node_model, implementation, log_model, gate);
            server.thread_functions.push_back([this, &node_model, node_label] {
                try {
                    auto lock = node_model.write_lock();
                    const auto& config = impl_->config;
                    const auto& ids = impl_->ids;
                    using web::json::value;
                    const bool generator = config.role == "generator";
                    const auto clocks = web::json::value_of({nmos::make_internal_clock(nmos::clock_names::clk0)});
                    const auto interfaces = nmos::experimental::node_interfaces(nmos::get_host_interfaces(node_model.settings));
                    auto node = nmos::make_node(us(ids.node), clocks, nmos::make_node_interfaces(interfaces), node_model.settings);
                    node.data[U("label")] = value::string(us(node_label));
                    node.data[U("description")] = value::string(U("mxl-avsync"));
                    nmos::insert_resource(node_model.node_resources, std::move(node));

                    std::vector<nmos::id> senders;
                    std::vector<nmos::id> receivers;
                    if (generator) {
                        senders = {us(ids.video_sender), us(ids.audio_sender)};
                    } else {
                        receivers = {us(ids.video_receiver), us(ids.audio_receiver)};
                        if (config.result_enable) {
                            senders.push_back(us(ids.result_sender));
                        }
                    }
                    auto device = nmos::make_device(us(ids.device), us(ids.node), senders, receivers, node_model.settings);
                    device.data[U("label")] = value::string(us(config.label() + (generator ? " avsync generator" : " avsync analyzer")));
                    device.data[U("description")] = value::string(U("mxl-avsync"));
                    nmos::insert_resource(node_model.node_resources, std::move(device));

                    const auto parsed = parse_video_format(config.video_format);
                    const nmos::rational video_rate{parsed ? parsed->frame.num : 50, parsed ? parsed->frame.den : 1};
                    const unsigned width = parsed ? static_cast<unsigned>(parsed->width) : 1920;
                    const unsigned height = parsed ? static_cast<unsigned>(parsed->height) : 1080;
                    const auto interlace = parsed && parsed->interlaced ? nmos::interlace_modes::interlaced_tff : nmos::interlace_modes::progressive;
                    const auto domain = us(config.mxl_output_domain_id.empty() ? ids.domain : config.mxl_output_domain_id);

                    auto add_video_sender = [&](const std::string& source_id, const std::string& flow_id, const std::string& sender_id, const std::string& label,
                                                const std::string& group, unsigned flow_width, unsigned flow_height, nmos::rational rate, const nmos::interlace_mode& mode) {
                        auto source = nmos::make_video_source(us(source_id), us(ids.device), nmos::clock_names::clk0, rate, node_model.settings);
                        source.data[U("label")] = value::string(us(label));
                        tag_group(source, group, "Video");
                        tag_test(source);
                        nmos::insert_resource(node_model.node_resources, std::move(source));
                        auto flow = nmos::make_coded_video_flow(us(flow_id), us(source_id), us(ids.device), rate, flow_width, flow_height, mode,
                            nmos::colorspaces::BT709, nmos::transfer_characteristics::SDR, sdp::samplings::YCbCr_4_2_2, 10, nmos::media_types::video_v210, node_model.settings);
                        flow.data[U("label")] = value::string(us(label));
                        flow.data[U("description")] = value::string(U("TEST SIGNAL — AV sync"));
                        tag_group(flow, group, "Video");
                        tag_test(flow);
                        nmos::insert_resource(node_model.node_resources, std::move(flow));
                        auto sender = nmos::make_sender(us(sender_id), us(flow_id), nmos::transports::mxl, us(ids.device), {}, {}, node_model.settings);
                        sender.data[U("label")] = value::string(us(label));
                        sender.data[U("subscription")][U("active")] = value::boolean(true);
                        tag_group(sender, group, "Video");
                        tag_test(sender);
                        nmos::insert_resource(node_model.node_resources, std::move(sender));
                        auto connection = nmos::make_connection_mxl_sender(us(sender_id), domain, us(flow_id));
                        for (const auto* key : {U("active"), U("staged")}) {
                            connection.data[key][U("master_enable")] = value::boolean(true);
                            connection.data[key][U("transport_params")][0][U("mxl_domain_id")] = value::string(domain);
                            connection.data[key][U("transport_params")][0][U("mxl_flow_id")] = value::string(us(flow_id));
                        }
                        nmos::insert_resource(node_model.connection_resources, std::move(connection));
                    };

                    if (generator) {
                        add_video_sender(ids.video_source, ids.video_flow, ids.video_sender, "AVSync Test Video", "AVSync Test", width, height, video_rate, interlace);
                        std::vector<nmos::channel> channels;
                        for (int channel = 0; channel < config.audio_channels; ++channel) {
                            channels.push_back(nmos::channel{us("Ch" + std::to_string(channel + 1)), channel == 0 ? nmos::channel_symbols::M1 : nmos::channel_symbols::M2});
                        }
                        if (channels.size() > 2) {
                            channels.resize(2);
                            for (int channel = 2; channel < config.audio_channels; ++channel) {
                                channels.push_back(nmos::channel{us("Ch" + std::to_string(channel + 1)), nmos::channel_symbols::Undefined(static_cast<unsigned>(channel + 1))});
                            }
                        }
                        const nmos::rational audio_rate{48000, 1};
                        auto source = nmos::make_audio_source(us(ids.audio_source), us(ids.device), nmos::clock_names::clk0, audio_rate, channels, node_model.settings);
                        source.data[U("label")] = value::string(U("AVSync Test Audio"));
                        tag_group(source, "AVSync Test", "Audio");
                        tag_test(source);
                        nmos::insert_resource(node_model.node_resources, std::move(source));
                        auto flow = nmos::make_raw_audio_flow(us(ids.audio_flow), us(ids.audio_source), us(ids.device), audio_rate, nmos::media_types::audio_float32, 32, node_model.settings);
                        flow.data[U("label")] = value::string(U("AVSync Test Audio"));
                        flow.data[U("channel_count")] = config.audio_channels;
                        tag_group(flow, "AVSync Test", "Audio");
                        tag_test(flow);
                        nmos::insert_resource(node_model.node_resources, std::move(flow));
                        auto sender = nmos::make_sender(us(ids.audio_sender), us(ids.audio_flow), nmos::transports::mxl, us(ids.device), {}, {}, node_model.settings);
                        sender.data[U("label")] = value::string(U("AVSync Test Audio"));
                        sender.data[U("subscription")][U("active")] = value::boolean(true);
                        tag_group(sender, "AVSync Test", "Audio");
                        tag_test(sender);
                        nmos::insert_resource(node_model.node_resources, std::move(sender));
                        auto connection = nmos::make_connection_mxl_sender(us(ids.audio_sender), domain, us(ids.audio_flow));
                        for (const auto* key : {U("active"), U("staged")}) {
                            connection.data[key][U("master_enable")] = value::boolean(true);
                            connection.data[key][U("transport_params")][0][U("mxl_domain_id")] = value::string(domain);
                            connection.data[key][U("transport_params")][0][U("mxl_flow_id")] = value::string(us(ids.audio_flow));
                        }
                        nmos::insert_resource(node_model.connection_resources, std::move(connection));
                    } else {
                        auto receiver = nmos::make_receiver(us(ids.video_receiver), us(ids.device), nmos::transports::mxl, {}, nmos::formats::video,
                            {nmos::media_types::video_v210}, node_model.settings);
                        receiver.data[U("label")] = value::string(U("AVSync In Video"));
                        tag_group(receiver, "AVSync In", "Video");
                        nmos::insert_resource(node_model.node_resources, std::move(receiver));
                        auto video_connection = nmos::make_connection_mxl_receiver(us(ids.video_receiver), utility::string_t{});
                        video_connection.data[U("active")][U("master_enable")] = value::boolean(false);
                        video_connection.data[U("staged")][U("master_enable")] = value::boolean(false);
                        video_connection.data[U("active")][U("transport_params")][0][U("mxl_domain_id")] = value::null();
                        video_connection.data[U("active")][U("transport_params")][0][U("mxl_flow_id")] = value::null();
                        nmos::insert_resource(node_model.connection_resources, std::move(video_connection));

                        auto audio_receiver = nmos::make_receiver(us(ids.audio_receiver), us(ids.device), nmos::transports::mxl, {}, nmos::formats::audio,
                            {nmos::media_types::audio_float32}, node_model.settings);
                        audio_receiver.data[U("label")] = value::string(U("AVSync In Audio"));
                        tag_group(audio_receiver, "AVSync In", "Audio");
                        nmos::insert_resource(node_model.node_resources, std::move(audio_receiver));
                        auto audio_connection = nmos::make_connection_mxl_receiver(us(ids.audio_receiver), utility::string_t{});
                        audio_connection.data[U("active")][U("master_enable")] = value::boolean(false);
                        audio_connection.data[U("staged")][U("master_enable")] = value::boolean(false);
                        audio_connection.data[U("active")][U("transport_params")][0][U("mxl_domain_id")] = value::null();
                        audio_connection.data[U("active")][U("transport_params")][0][U("mxl_flow_id")] = value::null();
                        nmos::insert_resource(node_model.connection_resources, std::move(audio_connection));

                        if (config.result_enable) {
                            VideoFormat result;
                            result.width = 960;
                            result.height = 540;
                            if (auto known = parse_video_format(config.result_format)) {
                                result = *known;
                            }
                            add_video_sender(ids.result_source, ids.result_flow, ids.result_sender, "AVSync Result Video", "AVSync Result",
                                static_cast<unsigned>(result.width), static_cast<unsigned>(result.height), nmos::rational{std::max(1, config.result_rate), 1},
                                nmos::interlace_modes::progressive);
                        }
                    }

                    node_model.notify();
                    lock.unlock();
                    {
                        std::lock_guard ready_lock(impl_->ready_mu);
                        impl_->ready = true;
                        impl_->ready_cv.notify_all();
                    }
                    auto wait_lock = node_model.write_lock();
                    node_model.wait(wait_lock, [&] { return node_model.shutdown; });
                } catch (const std::exception& ex) {
                    std::lock_guard ready_lock(impl_->ready_mu);
                    impl_->error = ex.what();
                    impl_->ready = true;
                    impl_->ready_cv.notify_all();
                }
            });
            nmos::server_guard guard(server);
            {
                std::unique_lock ready_lock(impl_->ready_mu);
                impl_->ready_cv.wait(ready_lock, [&] { return impl_->ready; });
                if (!impl_->error.empty()) {
                    throw std::runtime_error(impl_->error);
                }
                impl_->running.store(true);
            }
            while (impl_->running.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            {
                auto lock = node_model.write_lock();
                node_model.shutdown = true;
                node_model.notify();
            }
        } catch (const std::exception& ex) {
            std::lock_guard ready_lock(impl_->ready_mu);
            if (impl_->error.empty()) {
                impl_->error = ex.what();
            }
            impl_->ready = true;
            impl_->ready_cv.notify_all();
        }
    });

    std::unique_lock ready_lock(impl_->ready_mu);
    impl_->ready_cv.wait_for(ready_lock, std::chrono::seconds(30), [&] { return impl_->ready; });
    if (!impl_->error.empty() || !impl_->ready) {
        const auto message = impl_->error.empty() ? std::string("NMOS node did not become ready") : impl_->error;
        ready_lock.unlock();
        stop();
        throw std::runtime_error(message);
    }
    ready_lock.unlock();
    for (int attempt = 0; attempt < 40; ++attempt) {
        try {
            web::http::client::http_client client(us("http://127.0.0.1:" + std::to_string(impl_->config.nmos_port)));
            const auto response = client.request(web::http::methods::GET, U("/x-nmos/node/v1.3/self")).get();
            const auto body = response.extract_string().get();
            if (response.status_code() == 200 && su(body).find(impl_->ids.node) != std::string::npos) {
                return;
            }
        } catch (const std::exception&) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    stop();
    throw std::runtime_error("the NMOS Node API on port " + std::to_string(impl_->config.nmos_port) + " does not answer as this node (port taken?)");
}

void NmosNode::stop()
{
    impl_->running.store(false);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}
#endif

} // namespace avsync
