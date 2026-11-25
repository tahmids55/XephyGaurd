// XephyGuard UI v2 · interactive & backend-wired runtime (CSP-safe)

window.xgCsrf = () => document.querySelector('meta[name="csrf-token"]')?.content || '';

window.xgJson = function (id, fallback = null) {
  const el = document.getElementById(id);
  if (!el) return fallback;
  try {
    return JSON.parse(el.textContent || 'null') ?? fallback;
  } catch (e) {
    return fallback;
  }
};

window.xgHydrateGlobals = function () {
  const app = window.xgJson('xg-app-data', null);
  if (app && typeof app === 'object') {
    window.XG_APP = app;
    if (app.userName !== undefined) window.XG_USER_NAME = app.userName;
    if (app.serverHost !== undefined) window.XG_SERVER_HOST = app.serverHost;
  }
  const live = window.xgJson('xg-live-data', null);
  if (live && typeof live === 'object') {
    window.XG_LIVE = live;
  }
};

window.xgAdjustAlertBadge = function (delta) {
  const badge = document.getElementById('xg-open-alerts-badge');
  if (!badge) return;
  const cur = parseInt(badge.textContent || '0', 10);
  if (Number.isNaN(cur)) return;
  const next = Math.max(0, cur + delta);
  badge.textContent = String(next);
  badge.style.display = next > 0 ? '' : 'none';
};

window.xgHydrateGlobals();
document.addEventListener('DOMContentLoaded', window.xgHydrateGlobals);

window.xgParseApiError = async function (res) {
  if (res.status === 401 || res.status === 419) {
    if (typeof window !== 'undefined' && window.location && window.location.pathname !== '/login' && !window._xgAuthRedirecting) {
      window._xgAuthRedirecting = true;
      setTimeout(() => window.location.assign('/login'), 600);
    }
    return 'Session expired — redirecting to sign in…';
  }
  let msg = res.statusText || ('HTTP ' + res.status);
  try {
    const j = await res.json();
    if (j) {
      if (j.errors && typeof j.errors === 'object') {
        const first = Object.values(j.errors).flat()[0];
        if (first) msg = first;
        else if (j.message) msg = j.message;
      } else if (j.message) {
        msg = j.message;
      }
    }
  } catch (e) {}
  return msg;
};

window.xgApi = async function (url, opts = {}) {
  const method = (opts.method || 'GET').toUpperCase();
  const headers = {
    Accept: 'application/json',
    ...(method !== 'GET' ? { 'Content-Type': 'application/json', 'X-CSRF-TOKEN': window.xgCsrf() } : {}),
    ...(opts.headers || {})
  };
  const res = await fetch(url, { credentials: 'same-origin', ...opts, method, headers });
  if (!res.ok) {
    throw new Error(await window.xgParseApiError(res));
  }
  const ct = res.headers.get('content-type') || '';
  return ct.includes('application/json') ? res.json() : res.text();
};

/* ---------- Binary Screen Protocol (0x5847) & TileCanvas ---------- */
(function () {
  const TILE_SIZE = 64;
  const MAGIC = 0x5847;
  const VERSION = 1;
  const HEADER_SIZE = 28;
  const TILE_HEADER_SIZE = 10;
  const MAX_TILES = 16384;
  const MONITOR_REC_SIZE = 8, MAX_MONITORS = 16, ALL_SCREENS = 0xFFFF;
  const MAX_PAYLOAD = 64 * 64 * 4;
  const MsgType = { Frame: 1, Cursor: 2, Ack: 3, KeyframeReq: 4, Status: 5, Monitors: 6 };
  const Flags = { KEY: 1, FULL: 2, DELTA: 4, MASKED: 8, SECURE: 16, CURSOR_F: 32, ALL_SCREENS: 128 };
  const Codec = { Raw: 0, Webp: 1, Jpeg: 2 };

  class TileCanvas {
    constructor(canvas) {
      this.canvas = canvas;
      this.ctx = (canvas && typeof canvas.getContext === 'function') ? canvas.getContext('2d') : null;
      this.width = 0;
      this.height = 0;
      this.pendingTiles = [];
      this.pendingClear = false;
      this.rafId = null;
      this.banner = null;
    }
    setSize(w, h) {
      if (this.width !== w || this.height !== h) {
        this.width = w;
        this.height = h;
        if (this.canvas) { this.canvas.width = w; this.canvas.height = h; }
      }
    }
    setBanner(b) { this.banner = b; this.scheduleRender(); }
    drawTiles(tiles, clearFirst = false) {
      if (clearFirst) {
        this.pendingTiles = [];
        this.pendingClear = true;
      }
      if ((!tiles || !tiles.length) && !clearFirst) return;
      if (tiles && tiles.length) this.pendingTiles.push(...tiles);
      this.scheduleRender();
    }
    scheduleRender() {
      if (this.rafId !== null) return;
      if (typeof requestAnimationFrame === 'function') this.rafId = requestAnimationFrame(() => this.flush());
      else this.flush();
    }
    flush() {
      this.rafId = null;
      if (!this.ctx) return;
      if (this.pendingClear) {
        this.pendingClear = false;
        if (this.width > 0 && this.height > 0) this.ctx.clearRect(0, 0, this.width, this.height);
      }
      while (this.pendingTiles.length > 0) {
        const tile = this.pendingTiles.shift();
        if (tile && tile.bitmap) {
          this.ctx.drawImage(tile.bitmap, tile.tileX * TILE_SIZE, tile.tileY * TILE_SIZE);
          if (typeof tile.bitmap.close === 'function') tile.bitmap.close();
        }
      }
      if (this.banner) {
        this.ctx.save();
        this.ctx.fillStyle = 'rgba(0, 0, 0, 0.75)';
        this.ctx.fillRect(0, 0, this.width, this.height);
        this.ctx.fillStyle = '#FFFFFF';
        this.ctx.font = 'bold 24px sans-serif';
        this.ctx.textAlign = 'center';
        this.ctx.textBaseline = 'middle';
        this.ctx.fillText(this.banner, this.width / 2, this.height / 2);
        this.ctx.restore();
      }
    }
    clear() {
      this.pendingTiles = [];
      if (this.ctx && this.width > 0 && this.height > 0) this.ctx.clearRect(0, 0, this.width, this.height);
    }
  }

  function parseMessage(input) {
    try {
      let buffer, byteOffset = 0, byteLength = 0;
      if (input instanceof ArrayBuffer) { buffer = input; byteLength = input.byteLength; }
      else if (ArrayBuffer.isView(input)) { buffer = input.buffer; byteOffset = input.byteOffset; byteLength = input.byteLength; }
      else return null;
      if (byteLength < HEADER_SIZE) return null;
      const view = new DataView(buffer, byteOffset, byteLength);
      if (view.getUint16(0, true) !== MAGIC || view.getUint8(2) !== VERSION) return null;
      const type = view.getUint8(3), flags = view.getUint16(4, true), monitorId = view.getUint16(6, true);
      const streamId = view.getUint32(8, true), seq = view.getUint32(12, true), baseSeq = view.getUint32(16, true);
      const width = view.getUint16(20, true), height = view.getUint16(22, true), tileCount = view.getUint16(24, true);
      if (type === MsgType.Monitors) { // monitor rectangles for the picker; length must match the count exactly
        if (tileCount > MAX_MONITORS || byteLength !== HEADER_SIZE + tileCount * MONITOR_REC_SIZE) return null;
        const monitors = [];
        for (let i = 0; i < tileCount; ++i) {
          const o = HEADER_SIZE + i * MONITOR_REC_SIZE;
          const m = { id: i + 1, x: view.getUint16(o, true), y: view.getUint16(o + 2, true), w: view.getUint16(o + 4, true), h: view.getUint16(o + 6, true) };
          if (m.w === 0 || m.h === 0) return null;
          monitors.push(m);
        }
        return { type, flags, monitorId, streamId, seq, baseSeq, width, height, tiles: [], monitors };
      }
      if (tileCount > MAX_TILES) return null;
      let offset = HEADER_SIZE;
      const tiles = [];
      for (let i = 0; i < tileCount; ++i) {
        if (offset + TILE_HEADER_SIZE > byteLength) return null;
        const tileX = view.getUint16(offset, true), tileY = view.getUint16(offset + 2, true);
        const codec = view.getUint8(offset + 4), payloadLen = view.getUint32(offset + 6, true);
        if (payloadLen > MAX_PAYLOAD || offset + TILE_HEADER_SIZE + payloadLen > byteLength) return null;
        tiles.push({ tileX, tileY, codec, payload: new Uint8Array(buffer, byteOffset + offset + TILE_HEADER_SIZE, payloadLen) });
        offset += TILE_HEADER_SIZE + payloadLen;
      }
      return { type, flags, monitorId, streamId, seq, baseSeq, width, height, tiles };
    } catch (e) { return null; }
  }

  function encodeAck(streamId, monitorId, seq) {
    const buffer = new ArrayBuffer(HEADER_SIZE), view = new DataView(buffer);
    view.setUint16(0, MAGIC, true); view.setUint8(2, VERSION); view.setUint8(3, MsgType.Ack);
    view.setUint16(6, monitorId, true); view.setUint32(8, streamId, true); view.setUint32(12, seq, true);
    return buffer;
  }

  function encodeKeyframeReq(streamId, monitorId) {
    const buffer = new ArrayBuffer(HEADER_SIZE), view = new DataView(buffer);
    view.setUint16(0, MAGIC, true); view.setUint8(2, VERSION); view.setUint8(3, MsgType.KeyframeReq);
    view.setUint16(6, monitorId, true); view.setUint32(8, streamId, true);
    return buffer;
  }

  class ScreenViewer {
    constructor(canvas, options = {}) {
      this.canvas = canvas;
      this.options = options || {};
      this.tileCanvas = canvas && typeof canvas.getContext === 'function' ? new TileCanvas(canvas) : null;
      this.ws = null;
      this.lastAppliedSeq = 0;
      this.waitingForKeyframe = true;
      this._manualClose = false;
      this._rxQueue = Promise.resolve();
    }
    connect(url, token) {
      if (this.ws) this.disconnect();
      this._manualClose = false;
      this.waitingForKeyframe = true;
      this._rxQueue = Promise.resolve();
      this.ws = new WebSocket(url);
      this.ws.binaryType = 'arraybuffer';
      this.ws.onopen = () => {
        // The lease token is the first message, never part of the URL (screen Step 40).
        if (token) this.ws.send(JSON.stringify({ type: 'auth', token }));
        this.waitingForKeyframe = true;
        this.requestKeyframe(0, 0);
        if (this.options.onOpen) this.options.onOpen();
      };
      this.ws.onclose = evt => { if (this.options.onClose) this.options.onClose(this._manualClose, evt); };
      this.ws.onerror = err => { if (this.options.onError) this.options.onError(err); };
      this.ws.onmessage = event => {
        if (typeof event.data === 'string') {
          try { const j = JSON.parse(event.data); if (this.options.onControlMessage) this.options.onControlMessage(j); } catch (e) {}
          return;
        }
        this._rxQueue = this._rxQueue
          .then(() => this.handleBinaryFrame(event.data))
          .catch(() => {});
      };
    }
    async handleBinaryFrame(data) {
      const msg = parseMessage(data);
      if (msg && msg.type === MsgType.Monitors) { // not a frame: no ACK, nothing drawn
        this.monitors = msg.monitors;
        this.allScreens = (msg.flags & Flags.ALL_SCREENS) !== 0;
        if (this.options.onMonitors) this.options.onMonitors({ monitors: msg.monitors, active: msg.monitorId, allScreens: this.allScreens });
        return;
      }
      if (!msg || msg.type !== MsgType.Frame || !this.tileCanvas) return;
      if (msg.flags & Flags.MASKED) this.tileCanvas.setBanner('PRIVACY MASKED');
      else if (msg.flags & Flags.SECURE) this.tileCanvas.setBanner('SECURE DESKTOP');
      else this.tileCanvas.setBanner(null);

      const isFullReset = (msg.flags & Flags.KEY) !== 0 || (msg.flags & Flags.FULL) !== 0;
      if (isFullReset) {
        this.tileCanvas.setSize(msg.width, msg.height);
        this.lastAppliedSeq = msg.seq;
        this.waitingForKeyframe = false;
      } else if ((msg.flags & Flags.DELTA) !== 0) {
        if (this.waitingForKeyframe || msg.baseSeq !== this.lastAppliedSeq) {
          this.requestKeyframe(msg.streamId, 0); // 0 = keep the monitor that is shown
          this.waitingForKeyframe = true;
          return;
        }
        this.lastAppliedSeq = msg.seq;
      }

      const decodedTiles = [];
      for (const tile of msg.tiles) {
        try {
          let bitmap = null;
          if (tile.codec === Codec.Webp) bitmap = await createImageBitmap(new Blob([tile.payload], { type: 'image/webp' }));
          else if (tile.codec === Codec.Jpeg) bitmap = await createImageBitmap(new Blob([tile.payload], { type: 'image/jpeg' }));
          else if (tile.codec === Codec.Raw) {
            const tileW = Math.min(TILE_SIZE, msg.width - tile.tileX * TILE_SIZE);
            const tileH = Math.min(TILE_SIZE, msg.height - tile.tileY * TILE_SIZE);
            const rgba = new Uint8ClampedArray(tileW * tileH * 4);
            for (let p = 0; p < tileW * tileH; ++p) {
              rgba[p * 4 + 0] = tile.payload[p * 4 + 2];
              rgba[p * 4 + 1] = tile.payload[p * 4 + 1];
              rgba[p * 4 + 2] = tile.payload[p * 4 + 0];
              rgba[p * 4 + 3] = 255;
            }
            bitmap = await createImageBitmap(new ImageData(rgba, tileW, tileH));
          }
          if (bitmap) decodedTiles.push({ tileX: tile.tileX, tileY: tile.tileY, bitmap });
        } catch (e) {}
      }
      this.tileCanvas.drawTiles(decodedTiles, isFullReset);
      if (this.options.onFrame) this.options.onFrame(msg);
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        this.ws.send(encodeAck(msg.streamId, msg.monitorId, msg.seq));
      }
    }
    requestKeyframe(streamId, monitorId) {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        this.ws.send(encodeKeyframeReq(streamId, monitorId));
      }
    }
    // Asks the agent to show monitor `id` (1..N) or ALL_SCREENS; frames of the old picture are dropped until its keyframe.
    selectMonitor(id) {
      const ok = id === ALL_SCREENS ? this.allScreens === true : Array.isArray(this.monitors) && id >= 1 && id <= this.monitors.length;
      if (!ok || !this.ws || this.ws.readyState !== WebSocket.OPEN) return false;
      this.waitingForKeyframe = true;
      this._rxQueue = Promise.resolve();
      this.requestKeyframe(0, id);
      return true;
    }
    // Hands the token from POST /api/screen/{audit}/renew to the gateway; without it the socket closes at the lease end.
    renewLease(token) {
      if (!token || !this.ws || this.ws.readyState !== WebSocket.OPEN) return false;
      this.ws.send(JSON.stringify({ type: 'renew', token }));
      return true;
    }
    disconnect() {
      this._manualClose = true;
      if (this.ws) { this.ws.close(); this.ws = null; }
    }
  }

  window.TileCanvas = TileCanvas;
  window.ScreenViewer = ScreenViewer;
})();

/* ---------- Synthetic student desktops (stand-in when no live agent frame is active) ---------- */
(function () {
  const rng = seed => () => { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
  const rr = (x, ctx, a, b, w, h, rad, col) => { ctx.fillStyle = col; ctx.beginPath(); ctx.roundRect(a, b, w, h, rad); ctx.fill(); };
  const palette = ['#7aa2f7', '#9ece6a', '#e0af68', '#bb9af7', '#c0caf5', '#7dcfff'];

  window.xgDraw = function (c, pc, t, monitor) {
    if (!c || !pc) return;
    if (c.dataset && c.dataset.hasLiveFrame === '1') return;
    const ctx = c.getContext('2d'), w = c.width, h = c.height, u = w / 320;
    if (!window.XG_DEMO) {
      ctx.fillStyle = '#11141a';
      ctx.fillRect(0, 0, w, h);
      ctx.fillStyle = 'rgba(255,255,255,.55)';
      ctx.font = `500 ${Math.max(10, Math.round(11 * u))}px Inter, sans-serif`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const label = pc.status === 'offline' ? 'No signal'
        : (pc.session === 'wayland' ? 'Wayland session (capture disabled)'
        : (pc.status === 'idle' ? 'Signed out / idle' : 'Click Live or Take snapshot'));
      ctx.fillText(label, w / 2, h / 2);
      return;
    }
    const r = rng((pc.id || 1) * 977 + (monitor || 0) * 131);
    const hue = ((pc.id || 1) * 53 + (monitor || 0) * 40) % 360;
    const g = ctx.createLinearGradient(0, 0, w, h);
    g.addColorStop(0, `hsl(${hue},45%,28%)`); g.addColorStop(1, `hsl(${(hue + 50) % 360},50%,18%)`);
    ctx.fillStyle = g; ctx.fillRect(0, 0, w, h);

    const down = pc.status === 'offline' || pc.kind === 'lock';
    if (!down) {
      const X = w * .05, Y = h * .07, W = w * .9, H = h * .82;
      const kind = (monitor || 0) === 1 ? 'browser' : (pc.kind || 'code');
      if (kind === 'code') {
        rr(0, ctx, X, Y, W, H, 4 * u, '#1e2128');
        rr(0, ctx, X, Y, W, 9 * u, 4 * u, '#2a2e37');
        ctx.fillStyle = '#16181d'; ctx.fillRect(X, Y + 9 * u, W * .17, H - 9 * u);
        for (let i = 0; i < 9; i++) rr(0, ctx, X + 6 * u, Y + 18 * u + i * 9 * u, W * .17 * (.4 + r() * .45), 3 * u, 1.5 * u, '#3a3f4b');
        const lines = Math.floor((H - 22 * u) / (8 * u));
        for (let k = 0; k < lines; k++) {
          const ind = Math.floor(r() * 3) * 10 * u, len = (.12 + r() * .5) * W * .7;
          const live = k === Math.floor(lines * .6);
          const L = live ? len * (.55 + .45 * Math.abs(Math.sin(t / 900))) : len;
          rr(0, ctx, X + W * .2 + ind, Y + 17 * u + k * 8 * u, L, 3.2 * u, 1.6 * u, palette[Math.floor(r() * palette.length)]);
          if (live && Math.floor(t / 500) % 2 === 0) { ctx.fillStyle = '#fff'; ctx.fillRect(X + W * .2 + ind + L + 2 * u, Y + 15.5 * u + k * 8 * u, 1.2 * u, 6 * u); }
        }
      } else if (kind === 'terminal') {
        rr(0, ctx, X + W * .08, Y + H * .06, W * .84, H * .86, 4 * u, '#0e1013');
        rr(0, ctx, X + W * .08, Y + H * .06, W * .84, 8 * u, 4 * u, '#23262d');
        const lines = Math.floor((H * .8) / (8 * u));
        for (let k = 0; k < lines; k++) { const len = (.1 + r() * .6) * W * .7; rr(0, ctx, X + W * .11, Y + H * .06 + 14 * u + k * 8 * u, k % 4 === 0 ? len * .5 : len, 3 * u, 1.5 * u, k % 4 === 0 ? '#58d68d' : '#7b8494'); }
      } else if (kind === 'chat') {
        rr(0, ctx, X, Y, W, H, 4 * u, '#14161a'); rr(0, ctx, X, Y, W, 9 * u, 4 * u, '#23262d');
        ctx.fillStyle = '#101216'; ctx.fillRect(X, Y + 9 * u, W * .14, H - 9 * u);
        for (let i = 0; i < 6; i++) rr(0, ctx, X + 5 * u, Y + 18 * u + i * 10 * u, W * .14 - 10 * u, 4 * u, 2 * u, '#2c303a');
        for (let k = 0; k < 5; k++) { const me = k % 2 === 0, bw = (.25 + r() * .3) * W * .6; rr(0, ctx, me ? X + W * .95 - bw : X + W * .2, Y + 16 * u + k * 17 * u, bw, 12 * u, 5 * u, me ? '#2b5cd6' : '#262a33'); }
        rr(0, ctx, X + W * .2, Y + H - 18 * u, W * .7, 10 * u, 5 * u, '#262a33');
        if (Math.floor(t / 600) % 2 === 0) { ctx.fillStyle = '#fff'; ctx.fillRect(X + W * .22, Y + H - 15.5 * u, 1.2 * u, 5 * u); }
      } else {
        rr(0, ctx, X, Y, W, H, 4 * u, '#f3f4f6'); rr(0, ctx, X, Y, W, 9 * u, 4 * u, '#d9dce1');
        rr(0, ctx, X + W * .14, Y + 11 * u, W * .6, 7 * u, 3.5 * u, '#fff');
        rr(0, ctx, X + 8 * u, Y + 24 * u, W * .5, 7 * u, 2 * u, '#bfc5ce');
        for (let k = 0; k < 4; k++) rr(0, ctx, X + 8 * u, Y + 36 * u + k * 9 * u, W * (.5 + r() * .35), 3 * u, 1.5 * u, '#cdd2d9');
        for (let k = 0; k < 3; k++) rr(0, ctx, X + 8 * u + k * (W * .3), Y + H - 46 * u, W * .27, 38 * u, 4 * u, '#fff');
      }
    } else {
      ctx.fillStyle = 'rgba(0,0,0,.28)'; ctx.fillRect(0, 0, w, h);
      ctx.fillStyle = 'rgba(255,255,255,.9)'; ctx.font = `600 ${22 * u}px Inter, sans-serif`; ctx.textAlign = 'center';
      if (pc.status !== 'offline') ctx.fillText(new Date().toTimeString().slice(0, 5), w / 2, h * .46);
      ctx.fillStyle = 'rgba(255,255,255,.5)'; ctx.font = `${8 * u}px Inter, sans-serif`;
      ctx.fillText(pc.status === 'offline' ? 'No signal' : 'Locked', w / 2, h * .46 + 14 * u);
    }
    ctx.fillStyle = 'rgba(14,16,20,.88)'; ctx.fillRect(0, h - 11 * u, w, 11 * u);
    for (let i = 0; i < 6; i++) rr(0, ctx, w * .36 + i * 9 * u, h - 8.5 * u, 6 * u, 6 * u, 1.5 * u, i === 1 ? '#5b9bff' : '#59606c');
    rr(0, ctx, w - 30 * u, h - 8 * u, 24 * u, 5 * u, 2 * u, '#59606c');
    if (!down) {
      const cx = w * (.5 + .28 * Math.sin(t / 2300 + (pc.id || 1))), cy = h * (.5 + .2 * Math.cos(t / 1900 + (pc.id || 1)));
      ctx.fillStyle = '#fff'; ctx.strokeStyle = '#000'; ctx.lineWidth = .8 * u; ctx.beginPath();
      ctx.moveTo(cx, cy); ctx.lineTo(cx, cy + 10 * u); ctx.lineTo(cx + 3 * u, cy + 7.5 * u); ctx.lineTo(cx + 7 * u, cy + 7 * u); ctx.closePath(); ctx.fill(); ctx.stroke();
    }
  };

  setInterval(() => {
    if (document.hidden) return;
    const pool = (window.XG_LIVE && Array.isArray(window.XG_LIVE.pcs)) ? window.XG_LIVE.pcs : [];
    document.querySelectorAll('canvas[data-pc]').forEach(c => {
      const p = pool.find(x => x.id == c.dataset.pc);
      if (p) xgDraw(c, p, performance.now());
    });
  }, 1000);
})();

/* ---------- Alert grouping ---------- */
window.xgGroup = function (list) {
  const rank = { critical: 4, high: 3, medium: 2, low: 1, info: 0 };
  const map = new Map();
  list.forEach(a => {
    const k = a.pcId + '|' + a.rule;
    if (!map.has(k)) map.set(k, { key: k, pcId: a.pcId, pc: a.pc, pcRetired: !!a.pcRetired, user: a.user, rule: a.rule, sev: a.sev, title: a.title, at: a.at, ago: a.ago, items: [] });
    const g = map.get(k); g.items.push(a);
    if ((rank[a.sev] || 0) > (rank[g.sev] || 0)) g.sev = a.sev;
    if (a.ago < g.ago) { g.ago = a.ago; g.at = a.at; g.title = a.title; }
  });
  const arr = [...map.values()].map(g => ({ ...g, count: g.items.length, acked: g.items.every(i => i.acked), by: (g.items.find(i => i.acked) || {}).by, note: (g.items.find(i => i.note) || {}).note }));
  return arr.sort((a, b) => (a.acked - b.acked) || ((rank[b.sev] || 0) - (rank[a.sev] || 0)) || (a.ago - b.ago));
};
window.xgAgo = m => m < 60 ? m + ' min ago' : Math.floor(m / 60) + ' h ago';

window.xgAckItems = async function (items, comment = null) {
  const payload = comment !== null ? { comment } : {};
  if (items.length === 1) {
    const res = await window.xgApi(`/alerts/${items[0].id}/ack`, { method: 'POST', body: JSON.stringify(payload) });
    return res && res.pc ? [res.pc] : [];
  }
  const res = await window.xgApi('/alerts/ack-batch', {
    method: 'POST',
    body: JSON.stringify({ ids: items.map(a => a.id), ...payload }),
  });
  return res && Array.isArray(res.pcs) ? res.pcs : [];
};

document.addEventListener('alpine:init', () => {
  window.xgHydrateGlobals();

  /* ---------- Theme ---------- */
  const apply = mode => {
    const dark = mode === 'dark' || (mode === 'system' && matchMedia('(prefers-color-scheme: dark)').matches);
    document.documentElement.dataset.theme = dark ? 'dark' : 'light';
  };
  Alpine.store('theme', {
    mode: localStorage.getItem('xg-theme') || 'system',
    get dark() { return document.documentElement.dataset.theme === 'dark'; },
    set(m) { this.mode = m; try { localStorage.setItem('xg-theme', m); } catch (e) {} apply(m); },
    toggle() { this.set(this.dark ? 'light' : 'dark'); }
  });
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => { if (Alpine.store('theme').mode === 'system') apply('system'); });

  /* ---------- Toasts & copy ---------- */
  Alpine.store('toasts', {
    items: [],
    add(title, severity = 'info') { const id = Date.now() + Math.random(); this.items.push({ id, title, severity }); setTimeout(() => this.remove(id), 4200); },
    push(t) { this.add(t.title || '', t.severity || 'info'); },
    remove(id) { this.items = this.items.filter(t => t.id !== id); }
  });
  window.xgCopy = (text, label = 'Copied to clipboard') => {
    const done = () => Alpine.store('toasts').add(label, 'ok');
    if (navigator.clipboard && window.isSecureContext) navigator.clipboard.writeText(text).then(done, done);
    else { const t = document.createElement('textarea'); t.value = text; document.body.appendChild(t); t.select(); try { document.execCommand('copy'); } catch (e) {} t.remove(); done(); }
  };

  /* ---------- Command palette ---------- */
  Alpine.data('palette', () => ({
    open: false, q: '', idx: 0,
    get base() {
      const app = window.XG_APP || {};
      const canInstructor = !!app.canInstructor;
      const canAdmin = !!app.canAdmin;
      const nav = [
        { g: 'Go to', t: 'Live lab', icon: 'live', href: '/', k: 'Alt+1' },
        { g: 'Go to', t: 'Alerts', icon: 'bell', href: '/alerts', k: 'Alt+2' },
        { g: 'Go to', t: 'Sessions', icon: 'clock', href: '/sessions', k: 'Alt+3' },
      ];
      if (canInstructor) {
        nav.push(
          { g: 'Go to', t: 'Timetable and profiles', icon: 'calendar', href: '/profiles', k: 'Alt+4' },
          { g: 'Go to', t: 'Roster', icon: 'users', href: '/roster', k: 'Alt+5' }
        );
      }
      if (canAdmin) {
        nav.push({ g: 'Go to', t: 'Blocklists and rules', icon: 'shield', href: '/signatures', k: 'Alt+6' });
      }
      if (canInstructor) {
        nav.push({ g: 'Go to', t: 'Reports', icon: 'report', href: '/reports', k: 'Alt+7' });
      }
      nav.push({ g: 'Go to', t: 'Add a PC', icon: 'addpc', href: '/enroll', k: 'Alt+8' });
      if (canAdmin) {
        nav.push(
          { g: 'Go to', t: 'Health', icon: 'pulse', href: '/admin/health', k: 'Alt+9' },
          { g: 'Go to', t: 'Settings', icon: 'sliders', href: '/admin/settings' },
          { g: 'Go to', t: 'Staff accounts', icon: 'badge', href: '/admin/users' },
          { g: 'Go to', t: 'Audit log', icon: 'list', href: '/admin/audit' }
        );
      }
      nav.push(
        { g: 'Go to', t: 'Shared clipboard', icon: 'clipboard', href: '/share' },
        { g: 'Go to', t: 'Downloads', icon: 'download', href: '/downloads' },
        { g: 'Actions', t: 'Open the screen wall', icon: 'grid', href: '/#wall' },
        { g: 'Actions', t: 'Start a session', icon: 'plus', href: '/sessions#start' },
        { g: 'Actions', t: 'Switch theme', icon: 'sun', run: () => Alpine.store('theme').toggle() }
      );
      return nav;
    },
    get items() {
      const pool = (window.XG_LIVE && Array.isArray(window.XG_LIVE.pcs)) ? window.XG_LIVE.pcs : [];
      const pcs = pool.map(p => ({ g: 'PCs', t: p.name, icon: 'live', href: '/#pc=' + p.id, k: p.user || '—' }));
      const q = this.q.trim().toLowerCase();
      const all = this.base.concat(pcs);
      return q ? all.filter(i => (i.t + ' ' + (i.k || '')).toLowerCase().includes(q)).slice(0, 12) : this.base.slice(0, 8);
    },
    show() { this.open = true; this.q = ''; this.idx = 0; this.$nextTick(() => this.$refs.q && this.$refs.q.focus()); },
    go(i) { const it = this.items[i]; if (!it) return; this.open = false; if (it.run) it.run(); else location.href = it.href; },
    move(d) { const n = this.items.length; this.idx = (this.idx + d + n) % n; }
  }));

  /* ---------- Live lab ---------- */
  Alpine.data('labView', () => ({
    view: 'map',
    cols: (window.XG_LIVE && (window.XG_LIVE.cols || (window.XG_LIVE.lab && window.XG_LIVE.lab.cols))) || 6,
    rows: (window.XG_LIVE && (window.XG_LIVE.rows || (window.XG_LIVE.lab && window.XG_LIVE.lab.rows))) || 4,
    editing: false, dragId: null, dropKey: null,
    pcs: (window.XG_LIVE && Array.isArray(window.XG_LIVE.pcs)) ? structuredClone(window.XG_LIVE.pcs) : [],
    alerts: (window.XG_LIVE && Array.isArray(window.XG_LIVE.alerts)) ? structuredClone(window.XG_LIVE.alerts) : [],
    serverDetails: {},
    activePcId: null, inspector: false, tab: 'screen', screenMode: 'snapshot', monitor: 0, screenMonitors: [], screenMonitorActive: 0, screenAllScreens: false, snapT: 4200, snapAt: '',
    screenStreaming: false, screenHasFrame: false, screenStatus: '', screenAuditId: null, screenViewer: null, screenRenewTimer: null,
    wallStreams: {},
    tile: 240, flagged: false, blur: false, pinned: [], dock: { att: true, props: true, list: true }, pcFilter: 'all',
    messageModal: false, messageTarget: null, messageText: '', showDone: false, closeModal: false, closeName: '', closePid: null, closeStart: null, closeManual: false,

    init() {
      const h = location.hash;
      if (h.includes('wall')) this.view = 'wall';
      const m = h.match(/pc=(\d+)/); if (m) this.select(+m[1]);
      if (this.view === 'wall') this.$nextTick(() => this.syncWallStreams());
      this._timer = setInterval(() => {
        if (document.hidden || !this.inspector || this.tab !== 'screen' || !this.$refs.live) return;
        if (!this.screenHasFrame) {
          xgDraw(this.$refs.live, this.active, this.screenMode === 'live' ? performance.now() : this.snapT, this.monitor);
        }
      }, 110);
      this.$watch('view', v => {
        if (v === 'wall' && !this.inspector) this.$nextTick(() => this.syncWallStreams());
        else this.stopWallStreams();
      });
      this.$watch('pinned', () => {
        if (this.view === 'wall' && !this.inspector) this.$nextTick(() => this.syncWallStreams());
      });
      this.$watch('inspector', v => {
        if (v) {
          this.stopWallStreams();
          this.$nextTick(() => this.paintLive());
          if (this.activePcId) this.fetchDetail(this.activePcId);
        } else {
          this.stopScreen();
          if (this.view === 'wall') this.$nextTick(() => this.syncWallStreams());
        }
      });
      this.$watch('tab', t => {
        this.$nextTick(() => this.paintLive());
        if (t !== 'screen' && this.screenStreaming) this.stopScreen();
      });
      this.$watch('monitor', () => this.paintLive());
    },
    paintLive() {
      if (this.$refs.live && !this.screenHasFrame) {
        xgDraw(this.$refs.live, this.active, this.screenMode === 'live' ? performance.now() : this.snapT, this.monitor);
      }
    },

    async fetchDetail(id) {
      try {
        const d = await window.xgApi(`/pcs/${id}`);
        if (!d || !d.pc) return;
        const base = {
          events: [],
          processes: [],
          aiTraffic: [],
          tabs: [],
          extensions: [],
          artifacts: [],
          adapters: [],
          ports: [],
          devices: [],
          netEvents: [],
          keyrate: [],
          pastes: [],
          evidence: [],
        };
        const pcObj = d.pc || {};
        const existingPc = this.pcs.find(x => x.id === id) || {};
        const pcUiStatus = this.uiStatusFromTile(pcObj);
        const pcUser = pcObj.user || 'No one signed in';
        const pcScoreVal = (pcUiStatus === 'offline' || pcUiStatus === 'idle') ? '—' : Number(pcObj.score ?? 0);
        const mappedCaps = this.mapScreenCaps(pcObj, existingPc);
        const mappedSession = this.mapSessionType(pcObj, existingPc);
        const mappedMonitors = this.mapMonitorCount(pcObj, existingPc);
        const mapped = {
          pcInfo: {
            id: pcObj.id || id,
            name: pcObj.name || ('PC-' + id),
            host: pcObj.hostname || pcObj.name || ('PC-' + id),
            ip: pcObj.ip || '—',
            os: ((pcObj.os || 'Linux') + ' ' + (pcObj.os_version || '')).trim(),
            session: mappedSession,
            agent: pcObj.agent_version || '—',
            streamCost: pcObj.screen_cost || '',
            status: pcUiStatus,
            user: pcUser,
            last: pcObj.last_event || (pcUiStatus === 'offline' ? 'Offline' : 'Active'),
            score: pcScoreVal,
            isolated: !!pcObj.isolated,
            monitors: mappedMonitors,
            caps: mappedCaps,
          },
          processesUpdatedAt: d.processes_updated_at || '',
          events: (d.timeline && d.timeline.length)
            ? d.timeline.map(e => ({ time: e.at, sev: e.type === 'rule_hit' || e.type === 'tamper' ? 'high' : 'info', label: e.summary }))
            : base.events,
          processes: (d.running_processes && d.running_processes.length)
            ? d.running_processes.map(p => ({ pid: p.pid || 0, start: p.start || 0, name: p.exe || p.name || 'process', title: p.title || p.cmdline || '', cpu: Number(p.cpu || 0), mem: Math.round(Number(p.rss_mb || p.mem || 0)), net: Number(p.net_mbps || 0), threat: p.rule_id || '' }))
            : ((d.processes && d.processes.length)
              ? d.processes.map(p => ({ pid: p.pid || 0, name: p.exe || 'process', title: p.type || '', cpu: 0, mem: 0, net: 0, threat: p.rule || '' }))
              : base.processes),
          aiTraffic: (d.ai_traffic && d.ai_traffic.length)
            ? d.ai_traffic.map(r => ({ service: r.description || r.rule_id, host: r.domain || r.remote_ip, proc: r.process || 'browser', count: Number(r.count || 1), last: r.at, mode: r.mode || 'Observed' }))
            : base.aiTraffic,
          tabs: (d.browser_tabs && d.browser_tabs.length)
            ? d.browser_tabs.map(t => ({ browser: t.app || 'Browser', title: t.title || t.app, host: t.ai_site || t.user || '', ai: !!t.ai_site }))
            : base.tabs,
          extensions: (d.extensions && d.extensions.length)
            ? d.extensions.map(e => ({ browser: e.browser || 'Browser', name: e.name || e.id, id: e.id || '' }))
            : base.extensions,
          artifacts: (d.artifacts && d.artifacts.length)
            ? d.artifacts.map(a => ({ kind: a.kind || 'File', path: a.path || '', note: a.rule_id || 'Local AI file' }))
            : base.artifacts,
          adapters: (d.adapters && d.adapters.length)
            ? d.adapters.map(a => ({ name: a.name || 'adapter', type: (a.description || a.kind || (a.wireless ? 'Wi-Fi' : 'Ethernet')) + (a.default_route ? ' · default route' : ''), ip: (a.addresses || [])[0] || '', mac: a.driver || '' }))
            : base.adapters,
          ports: (d.listening_ports && d.listening_ports.length)
            ? d.listening_ports.map(p => ({ port: p.port, proto: 'TCP', proc: (p.process || 'unknown') + (p.pid ? ` (PID ${p.pid})` : ''), pid: p.pid || null, exe: p.process || '', rule: p.rule_id || 'Local AI port' }))
            : base.ports,
          devices: (d.devices && d.devices.length)
            ? d.devices.map(dv => ({ vidpid: (dv.vendor && dv.product) ? `${dv.vendor}:${dv.product}` : (dv.class || 'USB'), name: dv.model || dv.action || 'USB device', time: dv.at }))
            : base.devices,
          netEvents: d.network || [],
          keyrate: (d.keyrate && d.keyrate.series && d.keyrate.series.length)
            ? d.keyrate.series.map(s => s.keys || 0)
            : base.keyrate,
          pastes: (d.pastes && d.pastes.length)
            ? d.pastes.map(p => ({ time: p.occurred_at, chars: p.chars, lines: p.lines, from: p.source_app || '?', to: p.target_app || '?', code: !!p.looks_like_code }))
            : base.pastes,
          evidence: Array.isArray(d.evidence) ? d.evidence : base.evidence,
        };
        this.serverDetails[id] = mapped;
        this.applyTile(id, pcObj);
      } catch (e) {
        Alpine.store('toasts').add(e.message || 'Could not load PC details.', 'alert');
      }
    },

    /* derived */
    get active() { return this.pcs.find(p => p.id === this.activePcId) || (this.serverDetails[this.activePcId]?.pcInfo) || {}; },
    get det() {
      if (!this.activePcId) return null;
      return this.serverDetails[this.activePcId] || {
        processesUpdatedAt: '',
        events: [], processes: [], aiTraffic: [], tabs: [], extensions: [],
        artifacts: [], adapters: [], ports: [], devices: [], netEvents: [],
        keyrate: [], pastes: [], evidence: [],
      };
    },
    get counts() { const c = { ok: 0, idle: 0, watch: 0, alert: 0, off: 0 }; this.pcs.filter(p => p.seat).forEach(p => c[this.kind(p)]++); return c; },
    get total() { return this.pcs.filter(p => p.seat).length; },
    get online() { return this.pcs.filter(p => p.seat && p.status !== 'offline').length; },
    get groups() { return xgGroup(this.alerts.filter(a => !a.acked)); },
    get doneGroups() { return xgGroup(this.alerts.filter(a => a.acked)); },
    get half() { return Math.floor(this.cols / 2); },
    get gridStyle() { const h = this.half; return `grid-template-columns: repeat(${h}, minmax(0,1fr)) 18px repeat(${this.cols - h}, minmax(0,1fr))`; },
    get cells() {
      const out = [];
      for (let y = 0; y < this.rows; y++) for (let x = 0; x < this.cols; x++)
        out.push({ key: x + ':' + y, x, y, pc: this.pcs.find(p => p.seat && p.seat.x === x && p.seat.y === y) || null, col: x + 1 + (x >= this.half ? 1 : 0) });
      return out;
    },
    get unplaced() { return this.pcs.filter(p => !p.seat); },
    get listed() { return this.pcs.filter(p => p.seat && (this.pcFilter === 'all' || !['ok', 'idle'].includes(this.kind(p)))); },
    get wallPcs() { return this.pcs.filter(p => p.seat && !this.pinned.includes(p.id) && (!this.flagged || !['ok', 'idle'].includes(this.kind(p)))); },
    get pinnedPcs() { return this.pinned.map(id => this.pcs.find(p => p.id === id)).filter(Boolean); },
    get screenOk() { return !!(this.active.caps && this.active.caps.snapshot && this.active.status !== 'offline'); },
    get pcAlerts() { return this.alerts.filter(a => a.pcId === this.activePcId); },
    get tabs() {
      const d = this.det; if (!d) return [];
      return [['screen', 'Screen', 'monitor', ''], ['timeline', 'Timeline', 'clock', ''], ['alerts', 'Alerts', 'bell', this.pcAlerts.length || ''],
        ['programs', 'Programs', 'terminal', d.processes.length], ['browser', 'Browser', 'globe', d.aiTraffic.length || ''],
        ['installed', 'Installed AI', 'shield', (d.extensions.length + d.artifacts.length) || ''], ['network', 'Network', 'wifi', ''],
        ['typing', 'Typing', 'keyboard', ''], ['pastes', 'Pastes', 'clipboard', d.pastes.length || '']];
    },
    pct(n) { return Math.max(0, n / Math.max(1, this.total) * 100); },
    uiStatusFromTile(tile) {
      const status = tile?.status || 'normal';
      const score = Number(tile?.score ?? 0);
      const user = tile?.user ?? '';
      if (status === 'offline') return 'offline';
      if (!user || user === 'No one signed in') return 'idle';
      if (score >= 50 || status === 'alert') return 'alert';
      if (score >= 25 || status === 'watch' || status === 'warning') return 'watch';
      return 'normal';
    },
    mapSessionType(pcObj, fallback = {}) {
      const scr = pcObj?.capabilities?.screen;
      if (scr && scr.session_type) return scr.session_type === 'wayland' ? 'wayland' : (scr.session_type === 'win' ? 'win' : 'x11');
      if (fallback.session) return fallback.session;
      return String(pcObj?.os || '').toLowerCase().includes('windows') ? 'win' : 'x11';
    },
    mapMonitorCount(pcObj, fallback = {}) {
      const m = Number(pcObj?.capabilities?.screen?.monitors ?? fallback.monitors ?? 1);
      return m > 0 ? m : 1;
    },
    mapScreenCaps(pcObj, fallback = {}) {
      const uiStatus = this.uiStatusFromTile(pcObj);
      const scr = pcObj?.capabilities?.screen;
      const win = String(pcObj?.os || '').toLowerCase().includes('windows');
      if (scr && typeof scr === 'object') {
        return {
          snapshot: !!scr.snapshot && uiStatus !== 'offline',
          live: !!scr.live && uiStatus !== 'offline',
          delta: !!scr.delta_tiles && uiStatus !== 'offline',
          multi: Number(scr.monitors || 1) > 1,
          secure: !!scr.secure_desktop,
        };
      }
      if (fallback.caps) {
        return {
          ...fallback.caps,
          snapshot: !!fallback.caps.snapshot && uiStatus !== 'offline',
          live: !!fallback.caps.live && uiStatus !== 'offline',
        };
      }
      return {
        snapshot: uiStatus !== 'offline',
        live: uiStatus !== 'offline',
        delta: uiStatus !== 'offline',
        multi: false,
        secure: win,
      };
    },
    applyTile(pcId, tile) {
      if (!tile) return;
      const uiStatus = this.uiStatusFromTile(tile);
      const scoreVal = (uiStatus === 'offline' || uiStatus === 'idle') ? '—' : Number(tile.score ?? 0);
      const applyTo = (target) => {
        if (!target) return;
        target.status = uiStatus;
        target.score = scoreVal;
        target.isolated = !!tile.isolated;
        if (tile.user !== undefined) target.user = tile.user || 'No one signed in';
        if (tile.app !== undefined) target.app = tile.app || '—';
        if (tile.last_event !== undefined) target.last = tile.last_event || 'Active';
        if (tile.capabilities !== undefined) {
          target.caps = this.mapScreenCaps({ ...target, ...tile }, target);
          target.session = this.mapSessionType({ ...target, ...tile }, target);
          target.monitors = this.mapMonitorCount({ ...target, ...tile }, target);
        }
        if (uiStatus === 'offline') target.scene = 'off';
        else if (uiStatus === 'idle') target.scene = 'idle';
        else if (uiStatus === 'alert') target.scene = 'flag';
        else if (target.scene === 'flag' || target.scene === 'off' || target.scene === 'idle') target.scene = 'code';
      };
      const p = this.pcs.find(x => x.id === pcId);
      applyTo(p);
      const det = this.serverDetails[pcId];
      if (det && det.pcInfo) applyTo(det.pcInfo);
      if (window.XG_LIVE && Array.isArray(window.XG_LIVE.pcs)) {
        applyTo(window.XG_LIVE.pcs.find(x => x.id === pcId));
      }
      this.$nextTick(() => {
        document.querySelectorAll(`canvas[data-pc="${pcId}"]`).forEach(c => {
          if (p) window.xgDraw(c, p, performance.now());
        });
        if (this.activePcId === pcId) this.paintLive();
      });
    },
    kind(p) { if (p.isolated || p.status === 'alert') return 'alert'; return ({ active: 'ok', clean: 'ok', normal: 'ok', idle: 'idle', warning: 'watch', watch: 'watch', offline: 'off' })[p.status] || 'ok'; },
    seatClass(p) { if (!p) return 'empty'; const k = p.isolated ? 'iso' : this.kind(p); return 's-' + k + (this.editing ? ' editing' : ''); },
    tileClass(p) { const k = p.isolated ? 'iso' : this.kind(p); return { alert: 't-alert', watch: 't-watch', iso: 't-iso', off: 't-off' }[k] || ''; },
    dotOf(p) { if (p.isolated) return 'iso'; return ({ ok: 'ok', idle: '', watch: 'watch', alert: 'alert', off: 'off' })[this.kind(p)] || ''; },
    scoreClass(p) {
      if (typeof p.score !== 'number') return '';
      const k = this.kind(p);
      if (k === 'alert' || p.score >= 50) return 'low';
      if (k === 'watch' || p.score >= 25) return 'mid';
      return '';
    },
    ringColor(p) {
      if (typeof p.score !== 'number') return 'var(--off)';
      const k = this.kind(p);
      if (k === 'alert' || p.score >= 50) return 'var(--alert)';
      if (k === 'watch' || p.score >= 25) return 'var(--watch)';
      return 'var(--ok)';
    },
    statusText(p) { return p.isolated ? 'Isolated' : ({ active: 'Online', clean: 'Online', normal: 'Online', idle: 'Not in use', warning: 'Needs a look', watch: 'Needs a look', alert: 'Alert', offline: 'Offline' })[p.status] || 'Online'; },
    pc(id) { return this.pcs.find(p => p.id === id); },

    /* selection / inspector */
    select(id) { if (this.editing) return; this.activePcId = id; this.fetchDetail(id); },
    openInspector(id, tab) { if (this.editing) return; this.activePcId = id; this.tab = tab || 'screen'; this.screenMode = 'snapshot'; this.monitor = 0; this.inspector = true; this.fetchDetail(id); },
    closeInspector() { this.stopScreen(); this.inspector = false; },
    clear() { this.stopScreen(); this.inspector = false; this.messageModal = false; this.closeModal = false; },
    confirmForm(evt, msg) { if (!confirm(msg)) evt.preventDefault(); },

    async toggleIsolate(pc) {
      if (!pc || !pc.id) return;
      const next = !pc.isolated;
      pc.isolated = next;
      try {
        await window.xgApi(`/pcs/${pc.id}/isolate`, { method: 'POST', body: JSON.stringify({ isolated: next, reason: 'dashboard' }) });
        Alpine.store('toasts').add(pc.isolated ? `${pc.name} is cut off from the lab network.` : `${pc.name} is back on the network.`, pc.isolated ? 'alert' : 'ok');
      } catch (e) {
        pc.isolated = !next;
        Alpine.store('toasts').add(e.message || 'Could not change isolation state.', 'alert');
      }
    },
    async rescan(pcOrKind, kindArg) {
      const targetPc = (typeof pcOrKind === 'object' && pcOrKind) ? pcOrKind : this.active;
      const kind = (typeof pcOrKind === 'string') ? pcOrKind : (kindArg || 'all');
      if (!targetPc || !targetPc.id) return;
      Alpine.store('toasts').add(`Rescanning ${targetPc.name} (${kind})…`, 'info');
      try {
        await window.xgApi(`/pcs/${targetPc.id}/rescan`, { method: 'POST', body: JSON.stringify({ what: kind }) });
        setTimeout(() => this.fetchDetail(targetPc.id), 800);
      } catch (e) {
        Alpine.store('toasts').add(e.message || 'Rescan request failed.', 'alert');
      }
    },
    promptCloseProgram(name = '', pid = null, start = null) {
      this.closeName = name;
      this.closePid = pid;
      this.closeStart = start;
      this.closeManual = !name && !pid;
      this.closeModal = true;
    },
    async closeProgram() {
      const exe = (this.closeName || '').trim();
      if (!exe && !this.closePid) {
        Alpine.store('toasts').add('Enter a program name to close.', 'watch');
        return;
      }
      const target = this.active;
      if (!target || !target.id) return;
      try {
        await window.xgApi(`/pcs/${target.id}/kill`, { method: 'POST', body: JSON.stringify({ exe: exe || null, pid: this.closePid || null, start_time: (this.closePid && this.closeStart) || null }) });
        this.closeModal = false;
        Alpine.store('toasts').add(`Asked ${target.name} to close ${exe || ('PID ' + this.closePid)}.`, 'watch');
        this.closeName = '';
        this.closePid = null;
        this.closeStart = null;
        this.closeManual = false;
      } catch (e) {
        Alpine.store('toasts').add(e.message || 'Could not send kill command.', 'alert');
      }
    },
    async startScreenStream(mode = 'live') {
      const pcId = this.activePcId;
      if (!pcId) return;
      this.screenMode = mode;
      await this.stopScreen(false);
      this.screenStreaming = true;
      try {
        const data = await window.xgApi(`/api/screen/pc/${pcId}/token`, { method: 'POST', body: JSON.stringify({ mode }) });
        this.screenAuditId = data.audit_id;
        let gwUrl = String(data.gateway_url || '').trim();
        const wsProto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        if (!gwUrl) gwUrl = `${wsProto}//${window.location.host}/screen-gw`;
        else if (gwUrl.startsWith('/')) gwUrl = `${wsProto}//${window.location.host}${gwUrl.replace(/\/$/, '')}`;
        const wsUrl = `${gwUrl.replace(/\/$/, '')}/view`;
        const canvas = this.$refs.live;
        if (canvas && canvas.dataset) delete canvas.dataset.hasLiveFrame;
        if (canvas && window.ScreenViewer) {
          let warnedOffline = false;
          this.screenViewer = new window.ScreenViewer(canvas, {
            onFrame: () => {
              this.screenHasFrame = true;
              if (canvas.dataset) canvas.dataset.hasLiveFrame = '1';
              if (mode === 'snapshot') {
                setTimeout(() => { if (this.screenMode === 'snapshot') this.stopScreen(true); }, 60);
              }
            },
            onMonitors: ({ monitors, active, allScreens }) => {
              this.screenMonitors = monitors;
              this.screenMonitorActive = active;
              this.screenAllScreens = allScreens;
            },
            onControlMessage: msg => {
              if (msg && msg.type === 'status' && msg.agent_connected === false && !this.screenHasFrame && !warnedOffline) {
                warnedOffline = true;
                Alpine.store('toasts').add('Waiting for student screen agent to connect…', 'watch');
              }
            },
            onError: () => {
              if (!this.screenHasFrame && this.screenStreaming) {
                Alpine.store('toasts').add('Could not connect to live screen gateway.', 'watch');
              }
            },
            onClose: manual => {
              const hadFrame = this.screenHasFrame;
              this.screenStreaming = false;
              if (!manual && !hadFrame && !warnedOffline) {
                warnedOffline = true;
                Alpine.store('toasts').add('Screen stream closed before a frame arrived.', 'watch');
              }
            }
          });
          this.screenViewer.connect(wsUrl, data.token);
          if (mode === 'live' && this.screenAuditId) {
            this.screenRenewTimer = setInterval(() => {
              if (this.screenAuditId && this.screenStreaming) {
                window.xgApi(`/api/screen/${this.screenAuditId}/renew`, { method: 'POST' })
                  .then(r => { if (r && r.token && this.screenViewer) this.screenViewer.renewLease(r.token); })
                  .catch(() => {});
              }
            }, 20000);
          }
        }
      } catch (e) {
        this.screenStreaming = false;
        Alpine.store('toasts').add(e.message || 'Screen stream unavailable.', 'watch');
      }
    },
    pickMonitor(id) {
      if (this.screenViewer && this.screenViewer.selectMonitor(id)) this.screenMonitorActive = id === 0xFFFF ? 0 : id;
    },
    async stopScreen(keepCanvas = false) {
      this.screenMonitors = []; this.screenMonitorActive = 0; this.screenAllScreens = false;
      if (this.screenRenewTimer) { clearInterval(this.screenRenewTimer); this.screenRenewTimer = null; }
      if (this.screenViewer) { this.screenViewer.disconnect(); this.screenViewer = null; }
      if (this.screenAuditId) {
        const aid = this.screenAuditId;
        this.screenAuditId = null;
        window.xgApi(`/api/screen/${aid}/stop`, { method: 'POST' }).catch(() => {});
      }
      this.screenStreaming = false;
      if (!keepCanvas) {
        this.screenHasFrame = false;
        if (this.$refs.live && this.$refs.live.dataset) delete this.$refs.live.dataset.hasLiveFrame;
      }
    },
    async syncWallStreams() {
      if (this.view !== 'wall' || this.inspector) {
        this.stopWallStreams();
        return;
      }
      const candidates = [...this.pinnedPcs, ...this.wallPcs].filter((p, i, arr) =>
        p && arr.findIndex(x => x.id === p.id) === i &&
        p.status !== 'offline' && p.session !== 'wayland' && p.caps && p.caps.live
      ).slice(0, 6);
      const wantIds = new Set(candidates.map(p => p.id));
      Object.keys(this.wallStreams).forEach(k => {
        const id = Number(k);
        if (!wantIds.has(id)) this.stopWallOne(id);
      });
      for (const p of candidates) {
        if (this.wallStreams[p.id]) continue;
        const canvas = document.querySelector(`canvas[data-pc="${p.id}"]`);
        if (!canvas || !window.ScreenViewer) continue;
        this.wallStreams[p.id] = { viewer: null, auditId: null, timer: null };
        try {
          const data = await window.xgApi(`/api/screen/pc/${p.id}/token`, { method: 'POST', body: JSON.stringify({ mode: 'wall' }) });
          if (this.view !== 'wall' || this.inspector || !this.wallStreams[p.id]) {
            if (data?.audit_id) window.xgApi(`/api/screen/${data.audit_id}/stop`, { method: 'POST' }).catch(() => {});
            delete this.wallStreams[p.id];
            continue;
          }
          let gwUrl = String(data.gateway_url || '').trim();
          const wsProto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
          if (!gwUrl) gwUrl = `${wsProto}//${window.location.host}/screen-gw`;
          else if (gwUrl.startsWith('/')) gwUrl = `${wsProto}//${window.location.host}${gwUrl.replace(/\/$/, '')}`;
          const viewer = new window.ScreenViewer(canvas, {
            onFrame: () => { if (canvas.dataset) canvas.dataset.hasLiveFrame = '1'; },
            onClose: manual => {
              if (canvas.dataset) delete canvas.dataset.hasLiveFrame;
              // Closed by the gateway (lease end, agent restart, revoke): drop the entry so the next sync opens a new lease.
              if (!manual && this.wallStreams[p.id] && this.wallStreams[p.id].viewer === viewer) {
                this.stopWallOne(p.id);
                setTimeout(() => this.syncWallStreams(), 10000);
              }
            }
          });
          viewer.connect(`${gwUrl.replace(/\/$/, '')}/view`, data.token);
          const timer = setInterval(() => {
            const st = this.wallStreams[p.id];
            if (st && st.auditId) {
              window.xgApi(`/api/screen/${st.auditId}/renew`, { method: 'POST' })
                .then(r => { if (r && r.token && st.viewer) st.viewer.renewLease(r.token); })
                .catch(() => {});
            }
          }, 20000);
          this.wallStreams[p.id] = { viewer, auditId: data.audit_id, timer };
        } catch (e) {
          delete this.wallStreams[p.id];
        }
      }
    },
    stopWallOne(id) {
      const st = this.wallStreams[id];
      if (!st) return;
      delete this.wallStreams[id];
      if (st.timer) clearInterval(st.timer);
      if (st.viewer) st.viewer.disconnect();
      if (st.auditId) window.xgApi(`/api/screen/${st.auditId}/stop`, { method: 'POST' }).catch(() => {});
      document.querySelectorAll(`canvas[data-pc="${id}"]`).forEach(c => {
        if (c.dataset) delete c.dataset.hasLiveFrame;
      });
    },
    stopWallStreams() {
      Object.keys(this.wallStreams).forEach(k => this.stopWallOne(Number(k)));
    },
    snapshot() {
      this.snapT = performance.now();
      this.snapAt = new Date().toTimeString().slice(0, 8);
      this.screenMode = 'snapshot';
      this.screenHasFrame = false;
      this.startScreenStream('snapshot');
      this.paintLive();
    },
    goLive() {
      this.screenMode = 'live';
      this.startScreenStream('live');
    },
    async evidence() {
      const pcId = this.activePcId;
      const canvas = this.$refs.live;
      if (!pcId || !this.screenOk || !canvas) {
        Alpine.store('toasts').add('Screen view is unavailable on this PC.', 'watch');
        return;
      }
      if (!canvas.dataset || canvas.dataset.hasLiveFrame !== '1') {
        Alpine.store('toasts').add('Capture a snapshot or start Live view before saving evidence.', 'watch');
        return;
      }
      try {
        const blob = await new Promise((resolve, reject) => {
          canvas.toBlob(b => (b ? resolve(b) : reject(new Error('Could not encode screen canvas.'))), 'image/png');
        });
        const fd = new FormData();
        fd.append('image', blob, `evidence-pc-${pcId}.png`);
        fd.append('monitor', String(this.monitor || 0));
        fd.append('source_mode', this.screenMode === 'live' ? 'live' : 'snapshot');
        fd.append('screen_view_id', String(this.screenAuditId || ''));
        const res = await fetch(`/pcs/${pcId}/evidence`, {
          method: 'POST',
          credentials: 'same-origin',
          headers: {
            Accept: 'application/json',
            'X-CSRF-TOKEN': window.xgCsrf(),
            'X-Requested-With': 'XMLHttpRequest',
          },
          body: fd,
        });
        if (!res.ok) {
          throw new Error(await window.xgParseApiError(res));
        }
        const data = await res.json();
        if (data && data.evidence) {
          if (this.serverDetails[pcId]) {
            const prev = Array.isArray(this.serverDetails[pcId].evidence) ? this.serverDetails[pcId].evidence : [];
            this.serverDetails[pcId] = { ...this.serverDetails[pcId], evidence: [data.evidence, ...prev] };
          } else {
            await this.fetchDetail(pcId);
          }
        }
        Alpine.store('toasts').add('Screen snapshot saved as evidence.', 'ok');
      } catch (e) {
        Alpine.store('toasts').add(e.message || 'Could not save evidence.', 'alert');
      }
    },

    /* wall */
    togglePin(id) { const i = this.pinned.indexOf(id); if (i >= 0) this.pinned.splice(i, 1); else if (this.pinned.length < 4) this.pinned.push(id); else Alpine.store('toasts').add('You can pin up to 4 PCs.', 'watch'); },
    tileSize(s) { this.tile = s; },

    /* messages & alerts */
    message(id) { this.messageTarget = id ? this.pcs.find(p => p.id === id) : null; this.messageText = ''; this.messageModal = true; },
    async sendMessage() {
      const txt = this.messageText.trim();
      if (!txt) return;
      const target = this.messageTarget;
      try {
        await window.xgApi('/sessions/message', { method: 'POST', body: JSON.stringify({ text: txt, pc_ids: target ? [target.id] : [] }) });
        this.messageModal = false;
        this.messageText = '';
        Alpine.store('toasts').add(`Message sent to ${target ? target.name : 'every student screen'}.`, 'ok');
      } catch (e) {
        Alpine.store('toasts').add(e.message || 'Could not send message.', 'alert');
      }
    },
    async ackGroup(g) {
      const items = (g.items || []).filter(a => !a.acked && a.id);
      if (!items.length) return;
      try {
        const updatedTiles = await window.xgAckItems(items);
        items.forEach(a => {
          const x = this.alerts.find(y => y.id === a.id);
          if (x) { x.acked = true; x.by = window.XG_USER_NAME || 'Staff'; }
        });
        if (updatedTiles.length) {
          updatedTiles.forEach(t => this.applyTile(t.id, t));
        } else if (g.pcId) {
          const remaining = this.alerts.filter(a => a.pcId === g.pcId && !a.acked);
          if (!remaining.length) {
            const p = this.pcs.find(x => x.id === g.pcId);
            if (p) this.applyTile(g.pcId, { ...p, status: 'clean', score: 0 });
          }
        }
        window.xgAdjustAlertBadge(-items.length);
        Alpine.store('toasts').add(`${g.pc}: ${items.length > 1 ? items.length + ' alerts' : 'alert'} marked as reviewed.`, 'ok');
      } catch (e) {
        Alpine.store('toasts').add(e.message || 'Could not mark reviewed.', 'alert');
      }
    },

    /* seat map editing */
    startDrag(pc) { if (this.editing) this.dragId = pc.id; },
    dropOn(cell) {
      this.dropKey = null; const mv = this.pcs.find(p => p.id === this.dragId); this.dragId = null; if (!mv) return;
      const other = cell.pc, from = mv.seat; if (other && other.id === mv.id) return;
      if (other) other.seat = from ? { ...from } : null; mv.seat = { x: cell.x, y: cell.y };
    },
    dropShelf() { const mv = this.pcs.find(p => p.id === this.dragId); this.dragId = null; this.dropKey = null; if (mv) mv.seat = null; },
    async saveLayout() {
      const tiles = this.pcs.filter(p => p.seat).map(p => ({ pc_id: p.id, x: p.seat.x, y: p.seat.y }));
      try {
        await window.xgApi('/lab/layout', { method: 'POST', body: JSON.stringify({ cols: this.cols, rows: this.rows, tiles }) });
        this.editing = false;
        Alpine.store('toasts').add('Seat map saved.', 'ok');
      } catch (e) {
        Alpine.store('toasts').add(e.message || 'Could not save seat map.', 'alert');
      }
    }
  }));

  /* ---------- Alerts page ---------- */
  Alpine.data('alertsPage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      sev: d.sev || '',
      unacked: !!d.unacked,
      session: d.session || '',
      grouped: true,
      q: '',
      openKey: null,
      notes: {},
      byName: d.byName || window.XG_USER_NAME || 'Staff',
      openTotal: Number(d.openTotal ?? 0),
      total: Number(d.total ?? 0),
      hasPages: !!d.hasPages,
      sevTotals: d.sevTotals || null,
      all: Array.isArray(d.items) ? d.items : [],
      get filtered() {
        const q = this.q.toLowerCase();
        return this.all.filter(a =>
          (!this.sev || a.sev === this.sev)
          && (!this.unacked || !a.acked)
          && (!q || (a.pc + ' ' + a.title + ' ' + a.rule + ' ' + a.user).toLowerCase().includes(q))
        );
      },
      get rows() {
        return this.grouped
          ? window.xgGroup(this.filtered)
          : this.filtered.map(a => ({ ...a, key: 'a' + a.id, count: 1, items: [a] }));
      },
      get waiting() {
        if (!this.hasPages && !this.session) {
          return window.xgGroup(this.all.filter(a => !a.acked)).length;
        }
        return this.openTotal;
      },
      n(s) {
        if (this.sevTotals && s in this.sevTotals) return Number(this.sevTotals[s] || 0);
        return this.all.filter(a => a.sev === s).length;
      },
      allCount() {
        return this.total || this.all.length;
      },
      nav(params) {
        const u = new URL('/alerts', window.location.origin);
        if (params.session) u.searchParams.set('session', params.session);
        if (params.severity) u.searchParams.set('severity', params.severity);
        if (params.open) u.searchParams.set('open', '1');
        window.location.href = u.pathname + u.search;
      },
      selectSession(val) {
        this.nav({ session: val || '', severity: this.sev, open: this.unacked });
      },
      setSev(s) {
        if (this.hasPages || d.sev) {
          this.nav({ session: this.session, severity: s, open: this.unacked });
        } else {
          this.sev = s;
        }
      },
      toggleUnacked() {
        this.nav({ session: this.session, severity: this.sev, open: this.unacked });
      },
      async ack(r) {
        const note = (this.notes[r.key] || '').trim() || 'Reviewed.';
        const pending = (r.items || []).filter(i => !i.acked && i.id);
        if (!pending.length) return;
        try {
          await window.xgAckItems(pending, note);
          pending.forEach(i => {
            const a = this.all.find(x => x.id === i.id);
            if (a) { a.acked = true; a.by = this.byName; a.note = note; }
          });
          this.openTotal = Math.max(0, this.openTotal - pending.length);
          window.xgAdjustAlertBadge(-pending.length);
          this.openKey = null;
          Alpine.store('toasts').add(r.pc + ': marked as reviewed.', 'ok');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not mark reviewed.', 'alert');
        }
      }
    };
  });

  /* ---------- Settings page ---------- */
  Alpine.data('settingsPage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      cur: 'network',
      mode: d.mode || 'proxy_only',
      pasteAt: d.pasteAt ?? 800,
      tAlways: d.tAlways || 'off',
      tPractice: d.tPractice || 'app',
      tTest: d.tTest || 'full',
      hosts: !!d.hosts,
      queries: !!d.queries,
      init() {
        this.$watch('hosts', v => { if (!v) this.queries = false; });
      },
      go(id) {
        this.cur = id;
        document.getElementById(id)?.scrollIntoView({ behavior: 'smooth', block: 'start' });
      },
      reload() { window.location.reload(); }
    };
  });

  /* ---------- Staff accounts page ---------- */
  Alpine.data('usersPage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      users: Array.isArray(d.users) ? d.users : [],
      addOpen: false,
      caps: [
        ['Watch the live lab', 1, 1, 1],
        ['View student screens', 0, 1, 1],
        ['Enrol and approve PCs', 1, 0, 1],
        ['Start sessions and edit the timetable', 0, 1, 1],
        ['Reports and roster', 0, 1, 1],
        ['Blocklists, settings and staff', 0, 0, 1]
      ],
      initials(n) { return (n || '').split(' ').slice(0, 2).map(w => w[0] || '').join('').toUpperCase(); },
      async saveUser(u) {
        if (u.pwOpen && u.pw && u.pw.length < 10) {
          Alpine.store('toasts').add('Password must be at least 10 characters.', 'alert');
          return;
        }
        try {
          const payload = { name: u.name, email: u.email, role: u.role, active: !!u.on };
          if (u.pw) payload.password = u.pw;
          await window.xgApi('/users/' + u.id, { method: 'POST', body: JSON.stringify(payload) });
          u.pwOpen = false;
          u.pw = '';
          Alpine.store('toasts').add('Saved ' + u.name + '.', 'ok');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not save user.', 'alert');
        }
      }
    };
  });

  /* ---------- Audit log page ---------- */
  Alpine.data('auditPage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      fam: d.fam || '',
      q: '',
      openId: null,
      hasPages: !!d.hasPages,
      rows: Array.isArray(d.rows) ? d.rows : [],
      get shown() {
        const q = this.q.toLowerCase();
        return this.rows.filter(r =>
          (!this.fam || r.act.startsWith(this.fam))
          && (!q || (r.who + ' ' + r.act + ' ' + r.target + ' ' + r.sum).toLowerCase().includes(q))
        );
      },
      setFam(f) {
        if (this.hasPages || d.fam) {
          window.location.href = f ? ('/admin/audit?action=' + encodeURIComponent(f)) : '/admin/audit';
        } else {
          this.fam = f;
        }
      }
    };
  });

  /* ---------- Timetable & Profiles page ---------- */
  Alpine.data('profilesPage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      tab: location.hash === '#timetable' ? 'timetable' : 'profiles',
      cur: 0, hr: 44, start: 8, end: 18,
      days: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'],
      addOpen: false,
      canAdmin: !!(d.canAdmin ?? window.XG_APP?.canAdmin),
      profiles: Array.isArray(d.profiles) ? d.profiles : [],
      cats: [['ai', 'AI chat sites'], ['ide', 'Editor assistants'], ['marketplace', 'Code marketplaces'], ['vpn', 'VPNs and proxies'], ['doh', 'Encrypted DNS'], ['custom', 'Custom list']],
      coll: [['window', 'Active window'], ['clipboard', 'Paste size'], ['keys', 'Typing pace'], ['files', 'File activity'], ['gpu', 'GPU use'], ['artifacts', 'Installed AI files']],
      get p() { return this.profiles[this.cur] || null; },
      has(arr, k) { return (arr || []).includes(k); },
      flip(arr, k) { const i = arr.indexOf(k); i >= 0 ? arr.splice(i, 1) : arr.push(k); },
      newProfile() {
        this.profiles.push({ id: null, builtin: false, name: 'Custom profile ' + (this.profiles.length + 1), mode: 'test', desc: 'Custom policy profile.', block: ['ai', 'ide'], strict: false, aiov: true, collect: ['window', 'clipboard', 'keys', 'artifacts'], titles: 'app', kill: 'proc.ollama' });
        this.cur = this.profiles.length - 1;
      },
      deleteCurrent() {
        if (!this.p || !this.p.id) return;
        if (confirm('Delete this profile?')) document.getElementById('xg-del-profile-form')?.submit();
      },
      reload() { window.location.reload(); },
      items: Array.isArray(d.items) ? d.items : [],
      form: { day: 'Sun', from: '09:00', to: '11:00', course: '', profile: d.formProfile || '', valid_from: '', valid_until: '' },
      dayNum(day) { return ({ Mon: 1, Tue: 2, Wed: 3, Thu: 4, Fri: 5, Sat: 6, Sun: 7 })[day] || 7; },
      modeOf(p) { return (p || '').startsWith('Test') ? 'test' : (p || '').startsWith('Exam') ? 'exam' : (p || '').startsWith('Practice') ? 'practice' : 'always'; },
      top(t) { const [h, m] = t.split(':'); return ((+h - this.start) + (+m) / 60) * this.hr; },
      tall(i) { return Math.max(28, this.top(i.to) - this.top(i.from)); },
      async toggleSlot(i) {
        const prev = i.paused;
        i.paused = !prev;
        try {
          if (i.real) await window.xgApi('/timetable/' + i.id + '/toggle', { method: 'POST' });
          Alpine.store('toasts').add(i.paused ? 'Class paused.' : 'Class resumed.', 'ok');
        } catch (e) {
          i.paused = prev;
          Alpine.store('toasts').add(e.message || 'Could not update class.', 'alert');
        }
      },
      async removeSlot(i) {
        try {
          if (i.real) await window.xgApi('/timetable/' + i.id, { method: 'DELETE' });
          this.items = this.items.filter(x => x.id !== i.id);
          Alpine.store('toasts').add('Removed from timetable.', 'ok');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not remove class.', 'alert');
        }
      }
    };
  });

  /* ---------- Roster page ---------- */
  Alpine.data('rosterPage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      q: '',
      pseudo: !!d.pseudo,
      total: Number(d.total ?? 0),
      rows: Array.isArray(d.rows) ? d.rows : [],
      get shown() {
        const q = this.q.toLowerCase();
        return this.rows.filter(r => !q || (r.id + ' ' + r.name + ' ' + r.login).toLowerCase().includes(q));
      },
      triggerImport() { this.$refs.csv?.click(); },
      submitImport(evt) { if (evt.target?.files?.length) evt.target.form?.submit(); },
      async togglePseudo() {
        const next = this.pseudo;
        try {
          await window.xgApi('/roster/pseudonymize', { method: 'POST', body: JSON.stringify({ enabled: next }) });
          Alpine.store('toasts').add(next ? 'Pseudonyms enabled in exports.' : 'Student numbers used in exports.', 'ok');
        } catch (e) {
          this.pseudo = !next;
          Alpine.store('toasts').add(e.message || 'Could not update setting.', 'alert');
        }
      },
      async setConsent(r, status) {
        const prev = r.consent;
        r.consent = status;
        try {
          if (r.dbId) await window.xgApi('/roster/' + r.dbId + '/consent', { method: 'POST', body: JSON.stringify({ granted: status === 'granted' }) });
          Alpine.store('toasts').add('Consent updated for ' + r.name + '.', 'ok');
        } catch (e) {
          r.consent = prev;
          Alpine.store('toasts').add(e.message || 'Could not update consent.', 'alert');
        }
      },
      async saveExempt(r, exempt) {
        const reason = exempt ? String(r.ex || '').trim() : '';
        if (exempt && !reason) {
          Alpine.store('toasts').add('Enter what accommodation is needed.', 'watch');
          return;
        }
        try {
          if (r.dbId) await window.xgApi('/roster/' + r.dbId + '/exempt', { method: 'POST', body: JSON.stringify({ exempt: !!reason, reason: reason || null }) });
          r.ex = reason;
          r.exOpen = false;
          Alpine.store('toasts').add('Saved.', 'ok');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not update accommodation.', 'alert');
        }
      }
    };
  });

  /* ---------- Blocklists & Rules page ---------- */
  Alpine.data('sigPage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      tab: location.hash === '#rules' ? 'rules' : (location.hash === '#versions' ? 'versions' : 'sites'),
      cat: d.cat || 'all',
      q: d.q || '',
      newDomain: '',
      newCat: 'custom',
      rq: '',
      rk: 'all',
      importOpen: false,
      ruleModal: false,
      hasPages: !!d.hasPages,
      ruleForm: { rule_id: '', kind: 'process', severity: 'high', description: '', definition: '{}', actions: ['log', 'alert'] },
      counts: d.counts || {},
      sites: Array.isArray(d.sites) ? d.sites : [],
      rules: Array.isArray(d.rules) ? d.rules : [],
      versions: Array.isArray(d.versions) ? d.versions : [],
      lab(k) { return ({ all: 'All', ai: 'AI', ide: 'Editor', marketplace: 'Marketplace', doh: 'Encrypted DNS', vpn: 'VPN', custom: 'Custom' })[k] || k; },
      get catKeys() { return Object.keys(this.counts); },
      get shown() {
        const q = this.q.toLowerCase();
        return this.sites.filter(s => (this.cat === 'all' || s.c === this.cat) && (!q || s.d.toLowerCase().includes(q)));
      },
      get rshown() {
        const q = this.rq.toLowerCase();
        return this.rules.filter(r => (this.rk === 'all' || r.kind === this.rk) && (!q || (r.id + ' ' + r.desc).toLowerCase().includes(q)));
      },
      get kinds() { return ['all', ...new Set(this.rules.map(r => r.kind))]; },
      selectCat(k) {
        if (this.hasPages || d.cat !== 'all' || d.q) {
          const u = new URL('/signatures', window.location.origin);
          if (k && k !== 'all') u.searchParams.set('category', k);
          if (this.q.trim()) u.searchParams.set('q', this.q.trim());
          window.location.href = u.pathname + u.search;
        } else {
          this.cat = k;
        }
      },
      searchSites() {
        if (this.hasPages || d.q) {
          const u = new URL('/signatures', window.location.origin);
          if (this.cat && this.cat !== 'all') u.searchParams.set('category', this.cat);
          if (this.q.trim()) u.searchParams.set('q', this.q.trim());
          window.location.href = u.pathname + u.search;
        }
      },
      confirmSubmit(evt, msg) { if (!confirm(msg)) evt.preventDefault(); },
      async toggleSite(s) {
        const prev = s.off;
        s.off = !prev;
        try {
          if (s.real) await window.xgApi('/blocklist/' + s.id + '/toggle', { method: 'POST' });
        } catch (e) {
          s.off = prev;
          Alpine.store('toasts').add(e.message || 'Could not update blocklist entry.', 'alert');
        }
      },
      async deleteSite(s) {
        try {
          if (s.real) await window.xgApi('/blocklist/' + s.id, { method: 'DELETE' });
          this.sites = this.sites.filter(x => x.id !== s.id);
          Alpine.store('toasts').add(s.d + ' removed.', 'ok');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not delete entry.', 'alert');
        }
      },
      async toggleRule(r) {
        try {
          if (r.dbId) await window.xgApi('/rules/' + r.dbId + '/toggle', { method: 'POST' });
        } catch (e) {
          r.on = !r.on;
          Alpine.store('toasts').add(e.message || 'Could not toggle rule.', 'alert');
        }
      },
      openRuleModal(r) {
        if (r) {
          this.ruleForm = {
            rule_id: r.id,
            kind: ['process', 'extension', 'port', 'artifact', 'adapter', 'device'].includes(r.kind) ? r.kind : 'process',
            severity: r.sev || 'high',
            description: r.desc || '',
            definition: r.definition || '{}',
            actions: [...(r.actions || ['log', 'alert'])],
          };
        } else {
          this.ruleForm = {
            rule_id: 'proc.custom_ai',
            kind: 'process',
            severity: 'high',
            description: '',
            definition: '{}',
            actions: ['log', 'alert'],
          };
        }
        this.ruleModal = true;
      },
      flipAction(a) {
        const idx = this.ruleForm.actions.indexOf(a);
        idx >= 0 ? this.ruleForm.actions.splice(idx, 1) : this.ruleForm.actions.push(a);
      }
    };
  });

  /* ---------- Add a PC / Enrollment page ---------- */
  Alpine.data('enrollPage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      os: 'win', q: '',
      key: d.key || '',
      fp: d.fp || '',
      exeName: d.exeName || 'XephyGuard-Setup.exe',
      cccHost: d.cccHost || window.location.hostname || '',
      baseUrl: d.baseUrl || window.location.origin,
      pending: Array.isArray(d.pending) ? d.pending : [],
      enrolled: Array.isArray(d.enrolled) ? d.enrolled : [],
      get cmd() {
        const keyPart = this.key || '<ROTATE_KEY_FIRST>';
        if (this.os === 'win') {
          const cccArg = this.cccHost || window.location.hostname || this.baseUrl;
          return this.exeName + ' /S /KEY=' + keyPart + ' /CCC=' + cccArg + (this.fp ? ' /CAFP=' + this.fp : '');
        }
        return 'curl -fsSL ' + this.baseUrl + '/install/linux.sh | sudo bash -s -- --key ' + keyPart;
      },
      get shown() {
        const q = this.q.toLowerCase();
        return this.enrolled.filter(p => !q || (p.name + ' ' + p.host + ' ' + p.ip).toLowerCase().includes(q));
      },
      confirmSubmit(evt, msg) { if (!confirm(msg)) evt.preventDefault(); },
      async approve(p, adoptPcId = null) {
        try {
          const body = adoptPcId ? { name: p.name, adopt_pc_id: adoptPcId } : { name: p.name };
          const res = p.real ? await window.xgApi('/enroll/' + p.id + '/approve', { method: 'POST', body: JSON.stringify(body) }) : null;
          this.pending = this.pending.filter(x => x.id !== p.id);
          if (res && res.pc) this.enrolled.unshift(res.pc);
          Alpine.store('toasts').add('Enrolled ' + (p.name || p.host) + '.', 'ok');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not approve PC.', 'alert');
        }
      },
      async reject(p) {
        try {
          if (p.real) await window.xgApi('/enroll/' + p.id + '/reject', { method: 'POST' });
          this.pending = this.pending.filter(x => x.id !== p.id);
          Alpine.store('toasts').add('Turned away ' + p.host + '.', 'watch');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not reject request.', 'alert');
        }
      },
      async renamePc(p) {
        const nextName = String(p.name || '').trim();
        if (!nextName) return;
        try {
          if (p.real) await window.xgApi('/pcs/' + p.id + '/rename', { method: 'POST', body: JSON.stringify({ name: nextName }) });
          p.name = nextName;
          p.editingName = false;
          Alpine.store('toasts').add('Renamed to ' + nextName + '.', 'ok');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not rename PC.', 'alert');
        }
      },
      async retirePc(p) {
        if (!confirm('Retire ' + p.name + '? Its certificate stops working.')) return;
        try {
          if (p.real) await window.xgApi('/pcs/' + p.id + '/retire', { method: 'POST' });
          p.retired = true;
          Alpine.store('toasts').add(p.name + ' retired.', 'watch');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not retire PC.', 'alert');
        }
      }
    };
  });

  /* ---------- Shared clipboard page ---------- */
  Alpine.data('sharePage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      text: d.text || '',
      cmd: false,
      editing: false,
      last: d.last || 'Never',
      hist: Array.isArray(d.hist) ? d.hist : [],
      userName: d.userName || window.XG_USER_NAME || 'Staff',
      init() {
        this._poll = setInterval(async () => {
          if (this.editing || document.hidden) return;
          try {
            const r = await window.xgApi('/share/raw');
            if (r && r.ok && r.data) {
              if (r.data.updated_at && r.data.updated_at !== 'Never') this.last = r.data.updated_at;
              if (Array.isArray(r.data.history) && r.data.history.length) this.hist = r.data.history;
            }
          } catch (e) {}
        }, 2500);
      },
      async send() {
        if (!this.text.trim()) return;
        const v = this.text;
        const nowT = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false });
        try {
          const r = await window.xgApi('/share', { method: 'POST', body: JSON.stringify({ text: v, cmd: this.cmd }) });
          this.hist.unshift({ t: nowT, v: v, cmd: this.cmd });
          this.last = (r && r.data && r.data.updated_at) ? r.data.updated_at : ('Today ' + nowT + ' by ' + this.userName);
          this.text = '';
          this.editing = false;
          Alpine.store('toasts').add('Shared with all online PCs.', 'ok');
        } catch (e) {
          Alpine.store('toasts').add(e.message || 'Could not share item.', 'alert');
        }
      }
    };
  });

  /* ---------- Downloads / Sessions / Health / Session Report ---------- */
  Alpine.data('dlPage', () => {
    const d = window.xgJson('xg-page-data', {}) || {};
    return {
      cccHost: d.cccHost || window.location.hostname || '',
      baseUrl: d.baseUrl || window.location.origin,
      key: d.key || '',
      fp: d.fp || '',
      win: d.win || '',
      lin: d.lin || '',
      notify(msg) { Alpine.store('toasts').add(msg, 'info'); }
    };
  });

  Alpine.data('sessionsPage', () => ({
    startOpen: location.hash === '#start',
    bc: '',
    confirmStop(evt) { if (!confirm('End the session? Restrictions lift immediately.')) evt.preventDefault(); }
  }));

  Alpine.data('healthPage', () => ({
    running: false,
    rerun() { this.running = true; setTimeout(() => { window.location.reload(); }, 600); }
  }));

  Alpine.data('sessionReportPage', () => ({
    printReport() { window.print(); }
  }));
});

/* ---------- Flash messages on Alpine ready ---------- */
document.addEventListener('alpine:initialized', () => {
  window.xgHydrateGlobals();
  const app = window.XG_APP || {};
  const toasts = window.Alpine?.store('toasts');
  if (!toasts) return;
  if (app.flashStatus) toasts.add(app.flashStatus, 'ok');
  if (app.flashError) toasts.add(app.flashError, 'alert');
  if (Array.isArray(app.flashErrors)) {
    app.flashErrors.forEach(msg => toasts.add(msg, 'alert'));
  }
});

/* ---------- Shortcuts ---------- */
document.addEventListener('keydown', e => {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') { e.preventDefault(); window.dispatchEvent(new CustomEvent('xg-palette')); return; }
  if (!e.altKey || e.ctrlKey || e.metaKey) return;
  const map = { 1: '/', 2: '/alerts', 3: '/sessions', 4: '/profiles', 5: '/roster', 6: '/signatures', 7: '/reports', 8: '/enroll', 9: '/admin/health' };
  const code = e.code && e.code.startsWith('Digit') ? e.code.slice(5) : e.key;
  if (map[code]) { e.preventDefault(); location.href = map[code]; }
});
