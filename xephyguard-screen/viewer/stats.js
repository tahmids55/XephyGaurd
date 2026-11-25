// Debug statistics for the viewer (screen Step 21). Numbers only: no frame content, no identities.
// Latency here is receipt -> painted (decode + queue wait in this browser). The wire has no capture timestamp,
// so end-to-end latency is measured by bench/stream_bench, not here.
export const STATS_WINDOW_MS = 2000;

export class ViewerStats {
  /** @param {() => number} [now] clock in ms (injectable for tests) */
  constructor(now = () => performance.now()) {
    this.now = now;
    this.received = []; // {t, bytes}
    this.painted = []; // {t, lag}
    this.keyframeRequests = 0;
    this.queueDrops = 0;
    this.parseErrors = 0; // malformed binary messages
    this.decodeErrors = 0; // tiles the browser could not decode
    this.controlErrors = 0; // control JSON that did not parse
  }

  prune(t) {
    const from = t - STATS_WINDOW_MS;
    while (this.received.length && this.received[0].t < from) this.received.shift();
    while (this.painted.length && this.painted[0].t < from) this.painted.shift();
  }

  /** @returns {number} the receipt time, to hand back to recordPaint */
  recordReceived(bytes) {
    const t = this.now();
    this.received.push({ t, bytes });
    this.prune(t);
    return t;
  }

  recordPaint(receivedAt) {
    const t = this.now();
    this.painted.push({ t, lag: t - receivedAt });
    this.prune(t);
  }

  snapshot() {
    const t = this.now();
    this.prune(t);
    const secs = STATS_WINDOW_MS / 1000;
    const bits = this.received.reduce((a, r) => a + r.bytes * 8, 0);
    const lags = this.painted.map((p) => p.lag);
    return {
      fps: this.painted.length / secs,
      kbps: bits / 1000 / secs,
      lagMs: lags.length ? lags.reduce((a, b) => a + b, 0) / lags.length : 0,
      lagMaxMs: lags.length ? Math.max(...lags) : 0,
      keyframeRequests: this.keyframeRequests,
      queueDrops: this.queueDrops,
      parseErrors: this.parseErrors,
      decodeErrors: this.decodeErrors,
      controlErrors: this.controlErrors,
    };
  }

  format(queueLength = 0) {
    const s = this.snapshot();
    return `${s.fps.toFixed(1)} fps  ${s.kbps.toFixed(0)} kbit/s\n` +
      `paint lag ${s.lagMs.toFixed(0)} ms (max ${s.lagMaxMs.toFixed(0)})  queue ${queueLength}\n` +
      `keyframe req ${s.keyframeRequests}  queue drops ${s.queueDrops}\n` +
      `errors parse ${s.parseErrors}  decode ${s.decodeErrors}  control ${s.controlErrors}`;
  }
}
