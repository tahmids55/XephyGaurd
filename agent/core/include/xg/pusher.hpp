#pragma once
#include <atomic>
#include <functional>
#include <string>

#include "xg/event.hpp"
#include "xg/http.hpp"
#include "xg/websocket.hpp"

namespace xg::net {

struct PusherConfig {
    std::string host;
    uint16_t port = 8443;
    std::string app_key;
    std::string channel;          // private-pc.<id>
    std::string auth_path;        // REST path that signs channel subscriptions
};

/// Laravel Reverb speaks the Pusher protocol. This client subscribes to the agent's private
/// channel and invokes `on_event(event_name, data)` for every event received. Runs until stop().
class PusherClient {
public:
    using Handler = std::function<void(const std::string& event, const json& data)>;
    PusherClient(PusherConfig cfg, TlsOptions tls, HttpsClient& api, Handler h);
    /// Blocks: connect, subscribe, dispatch, reconnect with exponential backoff.
    void run(const std::atomic<bool>& stop);
    bool connected() const { return connected_; }

private:
    bool session(const std::atomic<bool>& stop);
    PusherConfig cfg_;
    TlsOptions tls_;
    HttpsClient& api_;
    Handler handler_;
    std::atomic<bool> connected_{false};
};

/// Pusher private-channel signature: "<key>:" + hex(HMAC-SHA256(secret, socket_id:channel)).
std::string pusher_auth_signature(const std::string& key, const std::string& secret, const std::string& socket_id,
                                  const std::string& channel);

}  // namespace xg::net
