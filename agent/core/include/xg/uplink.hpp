#pragma once
// I/O-only threads of the agent: they talk to the CCC and post what they learn to the core mailbox.
// They read agent state only through the core's UplinkView snapshot (DECISIONS #22).
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "xg/agent_messages.hpp"
#include "xg/event_queue.hpp"
#include "xg/http.hpp"
#include "xg/mailbox.hpp"

namespace xg {

/// Connection status flags written by the I/O threads and read by the core (scalars only).
struct AgentStatus {
    std::atomic<bool> online{false};
    std::atomic<bool> realtime_connected{false};
    std::atomic<uint64_t> bytes_up{0};
};

net::TlsOptions tls_options_for(const UplinkView& v, bool with_client_cert);

/// One auto evidence upload (F123): the JPEG the helper took, or `masked` with no image. The core checked it.
struct EvidenceUpload {
    std::string nonce;  // 32 lowercase hex, from the CCC's evidence_snapshot command
    std::string jpeg;   // empty when masked
    bool masked = false;
    /// Local fast path (plan step 8): the nonce came with our own rule_hit, so the CCC knows it only once that event
    /// is stored. The uplink sends it after an ack covering `after_seq` (set by queue_evidence), within 60 s.
    bool after_events = false;
    int64_t after_seq = 0;
    int64_t queued_ms = 0;
};

/// HTTPS batch uplink (POST /events), policy fetch and enrollment polling.
class Uplink {
public:
    Uplink(EventQueue* queue, const Snapshot<UplinkView>& view, Mailbox<AgentMessage>& core, AgentStatus& status)
        : queue_(queue), view_(view), core_(core), status_(status) {}

    void run(const std::atomic<bool>& stop);
    /// Sends one batch (queued events + latest heartbeat). Used by run() and once more at shutdown.
    bool flush_once();
    /// Asks the next loop iteration to (re)fetch the policy.
    void request_policy() { policy_dirty_ = true; wake(); }
    /// Thread-safe: uploads `u` on the next loop iteration, before the event batch. One slot: a newer upload
    /// replaces one not sent yet (each nonce is valid for 60 s only). One attempt; the CCC consumes the nonce.
    void queue_evidence(EvidenceUpload u);
    void wake();

private:
    bool fetch_policy(const UplinkView& v);
    void send_evidence(const UplinkView& v);
    void poll_enrollment(const UplinkView& v);
    net::HttpsClient& api(const UplinkView& v);

    EventQueue* queue_;
    const Snapshot<UplinkView>& view_;
    Mailbox<AgentMessage>& core_;
    AgentStatus& status_;
    std::unique_ptr<net::HttpsClient> api_;
    std::string api_key_;  // host/port/token the client was built for
    std::atomic<bool> policy_dirty_{true};
    int64_t last_policy_fetch_ = 0;
    bool warned_not_enrolled_ = false;
    std::string enroll_posted_for_;
    std::mutex wake_mu_;
    std::condition_variable wake_cv_;
    bool wake_ = false;
    std::mutex evidence_mu_;
    std::optional<EvidenceUpload> evidence_;  // guarded by evidence_mu_
    std::atomic<int64_t> acked_seq_{0};        // highest seq the CCC acknowledged (uplink thread writes)
    static constexpr int64_t kEvidenceWaitMs = 55000;  // a local nonce is valid 60 s from ingest on the CCC
};

/// Reverb (Pusher protocol) WSS channel for commands from the CCC.
class Realtime {
public:
    Realtime(const Snapshot<UplinkView>& view, Mailbox<AgentMessage>& core, AgentStatus& status)
        : view_(view), core_(core), status_(status) {}
    void run(const std::atomic<bool>& stop);

private:
    const Snapshot<UplinkView>& view_;
    Mailbox<AgentMessage>& core_;
    AgentStatus& status_;
};

}  // namespace xg
