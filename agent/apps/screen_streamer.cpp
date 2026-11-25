#include "screen_streamer.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "agent_state.h"

#if defined(_WIN32)
#include "win_dxgi_capture.h"
#include "win_gdi_capture.h"
#include "win_multi_capture.h"
#else
#include "x11_capture.h"
#ifdef Status
#undef Status
#endif
#endif

namespace xg {
namespace {

std::shared_ptr<ScreenCapture> platform_capture() {
#if defined(_WIN32)
    auto dxgi = std::make_shared<WinMultiOutputCapture>();  // every output, one at a time (screen Step 36)
    if (dxgi->initialize()) return dxgi;
    return std::make_shared<WinGdiCapture>();
#else
    return std::make_shared<X11Capture>();
#endif
}

// The gateway's Status message carries the viewers' mode in the reserved header field (bytes 26-27, u16 LE,
// docs/PROTOCOL.md): 2 snapshot, 3 wall, anything else (0 not stated, 1 live) is live.
const char* mode_from_status(std::string_view msg) {
    if (msg.size() < kHeaderSize) return "live";
    const uint16_t code = static_cast<uint8_t>(msg[26]) | (static_cast<uint16_t>(static_cast<uint8_t>(msg[27])) << 8);
    return code == 2 ? "snapshot" : code == 3 ? "wall" : "live";
}

constexpr size_t kMaxTitleBytes = 512;  // bounds the regex work on a title the student controls

std::shared_ptr<PrivacyFilter> make_filter(const WindowSource& src, const std::vector<MaskRule>& rules) {
    auto f = std::make_shared<PrivacyFilter>();
    std::vector<PrivacyRule> pr;
    for (const auto& r : rules) {
        PrivacyRule x;
        if (!r.app.empty()) x.process_name = r.app;
        if (!r.title_regex.empty()) x.title_regex = r.title_regex;
        pr.push_back(std::move(x));
    }
    f->set_rules(std::move(pr));
    f->set_window_provider([src]() -> xg::WindowInfo {
        auto w = src ? src() : std::nullopt;
        if (!w) throw std::runtime_error("foreground window unknown");  // PrivacyFilter turns this into MASKED
        if (w->title.size() > kMaxTitleBytes) w->title.resize(kMaxTitleBytes);
        return {w->app, {}, w->title};
    });
    return f;
}

}  // namespace

struct ScreenStreamer::Impl {
    CaptureFactory factory;
    mutable std::mutex mu;
    std::unique_ptr<AgentStateMachine> sm;
    uint32_t stream_id = 0;
    std::chrono::steady_clock::time_point lease_until{};  // default (epoch): no lease
    bool lease_active_locked() const { return std::chrono::steady_clock::now() < lease_until; }
    std::string mode = "live";  // what the gateway last asked for: wall, snapshot or live
    bool viewing = false;
    std::function<void(bool)> on_viewing_changed;
    WindowSource window_source;
    std::vector<MaskRule> mask_rules;
    ScreenEncoding encoding;
    double cpu_budget_pct = 8.0;
    std::shared_ptr<FrameOut> out;  // kept so a settings change can rebuild the state machine
    void make_sm_locked(std::shared_ptr<ScreenCapture> capture) {
        EncoderConfig cfg{};
        cfg.codec = encoding.webp ? Codec::Webp : Codec::Jpeg;
        cfg.quality_text = std::clamp(encoding.quality, 10, 100);
        cfg.webp_method = std::clamp(encoding.webp_method, 0, 6);
        // Called on the capture thread; the IPC client serialises writes with the helper's other messages.
        sm = std::make_unique<AgentStateMachine>(std::move(capture), cfg, [o = out](const std::vector<uint8_t>& d) {
            (*o)(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()));
        });
        sm->set_cpu_budget_pct(cpu_budget_pct);
        install_filter_locked();  // always: with no window source the filter masks everything
    }
    // The filter is rebuilt (not mutated) when the rules change; the state machine swaps it under its own lock.
    void install_filter_locked() {
        if (sm) sm->set_privacy_filter(make_filter(window_source, mask_rules));
    }

    void set_viewing(bool v) {
        if (viewing == v) return;
        viewing = v;
        if (on_viewing_changed) on_viewing_changed(v);
    }
    void stop_capture_locked() {
        if (sm && sm->is_thread_running()) sm->on_view_stop();
        set_viewing(false);
    }
};

ScreenStreamer::ScreenStreamer(CaptureFactory factory) : impl_(std::make_unique<Impl>()) {
    impl_->factory = factory ? std::move(factory) : CaptureFactory(platform_capture);
}

ScreenStreamer::~ScreenStreamer() { stop(); }

void ScreenStreamer::set_window_source(WindowSource src) {
    std::lock_guard lk(impl_->mu);
    impl_->window_source = std::move(src);
    impl_->install_filter_locked();
}

void ScreenStreamer::set_mask_rules(std::vector<MaskRule> rules) {
    std::lock_guard lk(impl_->mu);
    impl_->mask_rules = std::move(rules);
    impl_->install_filter_locked();
}

void ScreenStreamer::set_encoding(const ScreenEncoding& enc) {
    std::lock_guard lk(impl_->mu);
    if (impl_->encoding == enc) return;
    impl_->encoding = enc;
    if (impl_->sm && !impl_->sm->is_thread_running()) {  // armed, not capturing: rebuild now
        auto capture = impl_->factory();
        if (capture) impl_->make_sm_locked(std::move(capture));
    }
}

void ScreenStreamer::set_cpu_budget_pct(double pct) {
    std::lock_guard lk(impl_->mu);
    impl_->cpu_budget_pct = std::clamp(pct, 1.0, 100.0);
    if (impl_->sm) impl_->sm->set_cpu_budget_pct(impl_->cpu_budget_pct);
}

StreamStats ScreenStreamer::stream_stats() const {
    std::lock_guard lk(impl_->mu);
    return impl_->sm ? impl_->sm->stream_stats() : StreamStats{};
}

ScreenCost ScreenStreamer::cost() const {
    const StreamStats s = stream_stats();
    return ScreenCost{s.cpu_pct, s.level};
}

bool ScreenStreamer::start(uint32_t stream_id, FrameOut out, std::function<void(bool)> on_viewing_changed) {
    if (stream_id == 0 || !out) return false;
    std::lock_guard lk(impl_->mu);
    if (impl_->sm && impl_->stream_id == stream_id) return true;
    impl_->stop_capture_locked();
    impl_->sm.reset();
    auto capture = impl_->factory();
    if (!capture) return false;
    impl_->out = std::make_shared<FrameOut>(std::move(out));
    impl_->make_sm_locked(std::move(capture));
    impl_->stream_id = stream_id;
    impl_->on_viewing_changed = std::move(on_viewing_changed);
    return true;
}

void ScreenStreamer::grant_lease(int ttl_s) {
    std::lock_guard lk(impl_->mu);
    impl_->lease_until = std::chrono::steady_clock::now() + std::chrono::seconds(std::clamp(ttl_s, 1, 120));
}

void ScreenStreamer::tick() {
    std::lock_guard lk(impl_->mu);
    if (impl_->sm && impl_->sm->is_thread_running() && !impl_->lease_active_locked()) impl_->stop_capture_locked();
}

void ScreenStreamer::on_gateway_message(std::string_view msg) {
    std::lock_guard lk(impl_->mu);
    if (!impl_->sm) return;
    const auto* bytes = reinterpret_cast<const uint8_t*>(msg.data());
    if (auto m = decode(bytes, msg.size())) {
        if (m->type == MsgType::Status && m->flags == 0) {  // no viewer left
            impl_->stop_capture_locked();
            impl_->mode = "live";
            return;
        }
        if (m->type == MsgType::Status) impl_->mode = mode_from_status(msg);
        if (m->type == MsgType::KeyframeReq || (m->type == MsgType::Status && m->flags > 0)) {
            if (!impl_->lease_active_locked()) return;  // no CCC lease: a gateway request alone never starts capture
            // A different mode (a wall viewer became a live one, or the reverse) restarts capture with it.
            if (impl_->sm->is_thread_running() && impl_->sm->mode() != impl_->mode) impl_->sm->on_view_stop();
            if (!impl_->sm->is_thread_running()) {
                impl_->sm->on_view_stop();
                impl_->sm->on_view_start(impl_->stream_id, impl_->mode);
            }
            impl_->set_viewing(true);
        }
    }
    impl_->sm->on_incoming_message(bytes, msg.size());
}

void ScreenStreamer::on_link_lost() {
    std::lock_guard lk(impl_->mu);
    impl_->lease_until = {};  // the service grants it again when it reconnects
    impl_->stop_capture_locked();
}

void ScreenStreamer::stop() {
    if (!impl_) return;
    std::lock_guard lk(impl_->mu);
    impl_->stop_capture_locked();
    impl_->sm.reset();
    impl_->stream_id = 0;
}

bool ScreenStreamer::is_running() const {
    std::lock_guard lk(impl_->mu);
    return impl_->sm != nullptr;
}

bool ScreenStreamer::is_capturing() const {
    std::lock_guard lk(impl_->mu);
    return impl_->sm && impl_->sm->is_thread_running();
}

}  // namespace xg
