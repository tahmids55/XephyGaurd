#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace xg {

class ScreenStreamer {
public:
    ScreenStreamer();
    ~ScreenStreamer();

    ScreenStreamer(const ScreenStreamer&) = delete;
    ScreenStreamer& operator=(const ScreenStreamer&) = delete;

    // Starts background screen capture and streaming thread.
    // If already running with the same host and stream_id, this is a no-op.
    // If target changed, restarts the streaming loop with the new parameters.
    bool start(const std::string& ccc_host,
               uint32_t stream_id,
               uint16_t gateway_port = 8082,
               const std::string& gateway_path = "/agent",
               bool use_tls = false,
               const std::string& agent_token = {});

    // Stops streaming thread cleanly.
    void stop();

    bool is_running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xg
