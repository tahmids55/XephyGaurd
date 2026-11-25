#pragma once
// Embedded screen streamer in the session helper (F95). Since screen Step 61 (DECISIONS #24) it only captures
// and encodes: frames leave through the service IPC channel, and the gateway's control messages (Ack,
// KeyframeReq, Status) arrive from the service as `screen_rx`. The helper holds no gateway credential and
// opens no network connection.
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xg {

class ScreenCapture;
struct StreamStats;
/// What the helper reports for the heartbeat (screen Step 57): the governor's CPU % of one core and throttle level.
struct ScreenCost { double cpu_pct = 0; int level = 0; };

/// The window the student is working in, as the helper's window monitor last saw it. Used only to decide
/// whether to mask the picture; never sent anywhere (ETHICS.md, screen Step 11).
struct ScreenWindow {
    std::string app;    // executable name
    std::string title;
};
/// nullopt = unknown (no monitor, no event yet): the streamer then masks the screen (fail-safe).
using WindowSource = std::function<std::optional<ScreenWindow>()>;
/// A window to hide: every non-empty field must match (app case-insensitive, title as case-insensitive regex).
struct MaskRule {
    std::string app, title_regex;
};
/// How tiles are encoded (policy `screen.codec`, `screen.quality`, `screen.webp_method`; docs/EVALUATION.md M4-5).
struct ScreenEncoding {
    bool webp = false;    // false = JPEG (libjpeg-turbo), the cheapest on moving content
    int quality = 85;     // 10..100
    int webp_method = 1;  // 0..6, WebP only: low effort
    bool operator==(const ScreenEncoding&) const = default;
};

class ScreenStreamer {
public:
    /// Sends one encoded screen message to the service; false if it could not be sent.
    using FrameOut = std::function<bool(std::string_view)>;
    /// Makes the capture backend; empty = the platform default (DXGI/GDI on Windows, X11 on Linux).
    using CaptureFactory = std::function<std::shared_ptr<ScreenCapture>()>;

    explicit ScreenStreamer(CaptureFactory factory = {});
    ~ScreenStreamer();

    ScreenStreamer(const ScreenStreamer&) = delete;
    ScreenStreamer& operator=(const ScreenStreamer&) = delete;

    /// Where the streamer learns the focused window. Without one the picture stays masked.
    void set_window_source(WindowSource src);
    /// Replaces the windows to hide. Takes effect on a running stream. An invalid regex masks the screen.
    void set_mask_rules(std::vector<MaskRule> rules);
    /// Replaces the encoder settings. A view in progress keeps its settings; the next one uses the new ones.
    void set_encoding(const ScreenEncoding& enc);
    /// CPU budget of the capture/encode thread in percent of one core (policy `screen.cpu_budget_pct`, 1..100,
    /// default 8). Takes effect at once, also on a running view.
    void set_cpu_budget_pct(double pct);
    /// The governor's choice and measured CPU of the current or last view (numbers only).
    StreamStats stream_stats() const;
    ScreenCost cost() const;

    /// Arms the streamer for `stream_id`. Capture starts only when the gateway asks for a picture.
    /// A no-op if already armed for the same stream.
    bool start(uint32_t stream_id, FrameOut out, std::function<void(bool)> on_viewing_changed = {});
    /// The service says the CCC holds a live-view lease for `ttl_s` more seconds (clamped to 1..120). Capture starts
    /// only inside a lease and stops by itself when it lapses (checked by tick()).
    void grant_lease(int ttl_s);
    /// Stops capture if the lease has lapsed. Call regularly from the helper's main loop.
    void tick();
    /// One gateway message relayed by the service.
    void on_gateway_message(std::string_view msg);
    /// The service connection is gone: stop capturing until the gateway asks again.
    void on_link_lost();
    void stop();
    bool is_running() const;
    bool is_capturing() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xg
