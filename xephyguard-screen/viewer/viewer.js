import { TileCanvas, TILE_SIZE } from './tilecanvas.js';

export const MAGIC = 0x5847;
export const VERSION = 1;
export const VERSION_2 = 2; // Frame records are pixel rectangles
export const HEADER_SIZE = 28;
export const TILE_HEADER_SIZE = 10;
export const MAX_TILES = 16384;
export const MONITOR_REC_SIZE = 8;
export const MAX_MONITORS = 16;
export const ALL_SCREENS = 0xFFFF; // KeyframeReq monitor_id: the whole desktop
export const MAX_PAYLOAD = 64 * 64 * 4;
export const RECT_HEADER_SIZE = 14;
export const MAX_RECT_SIDE = 256;
export const MAX_RECT_PAYLOAD = 256 * 256 * 4;
export const MAX_QUEUED_FRAMES = 8; // decoded-but-not-yet-drawn frames kept before the viewer drops its backlog

export const MsgType = {
  Frame: 1,
  Cursor: 2,
  Ack: 3,
  KeyframeReq: 4,
  Status: 5,
  Monitors: 6 // list of monitors for the selector (Step 35)
};

export const Flags = {
  KEY: 1,
  FULL: 2,
  DELTA: 4,
  MASKED: 8,
  SECURE: 16,
  CURSOR_F: 32,
  ALL_SCREENS: 128, // Monitors message only: the agent can show the whole desktop
  CACHED: 64 // gateway replayed its cached keyframe (up to 5 s old); the next live frame replaces it
};

export const Codec = {
  Raw: 0,
  Webp: 1,
  Jpeg: 2
};

/**
 * Parses a binary protocol frame safely using DataView (little-endian).
 * Returns null on ANY malformed input without throwing.
 * @param {ArrayBuffer|Uint8Array} input
 * @returns {object|null}
 */
export function parseMessage(input) {
  try {
    let buffer;
    let byteOffset = 0;
    let byteLength = 0;

    if (input instanceof ArrayBuffer) {
      buffer = input;
      byteOffset = 0;
      byteLength = input.byteLength;
    } else if (ArrayBuffer.isView(input)) {
      buffer = input.buffer;
      byteOffset = input.byteOffset;
      byteLength = input.byteLength;
    } else {
      return null;
    }

    if (byteLength < HEADER_SIZE) {
      return null;
    }

    const view = new DataView(buffer, byteOffset, byteLength);

    const magic = view.getUint16(0, true);
    if (magic !== MAGIC) {
      return null;
    }

    const version = view.getUint8(2);
    if (version !== VERSION && version !== VERSION_2) {
      return null;
    }
    const v2 = version === VERSION_2;

    const type = view.getUint8(3);
    const flags = view.getUint16(4, true);
    const monitorId = view.getUint16(6, true);
    const streamId = view.getUint32(8, true);
    const seq = view.getUint32(12, true);
    const baseSeq = view.getUint32(16, true);
    const width = view.getUint16(20, true);
    const height = view.getUint16(22, true);
    const tileCount = view.getUint16(24, true);

    if (type === MsgType.Monitors) { // monitor rectangles, v1 only; the length must match the count exactly
      if (v2 || tileCount > MAX_MONITORS || byteLength !== HEADER_SIZE + tileCount * MONITOR_REC_SIZE) return null;
      const monitors = [];
      for (let i = 0; i < tileCount; ++i) {
        const o = HEADER_SIZE + i * MONITOR_REC_SIZE;
        const m = { id: i + 1, x: view.getUint16(o, true), y: view.getUint16(o + 2, true), w: view.getUint16(o + 4, true), h: view.getUint16(o + 6, true) };
        if (m.w === 0 || m.h === 0) return null;
        monitors.push(m);
      }
      return { version, type, flags, monitorId, streamId, seq, baseSeq, width, height, tiles: [], monitors };
    }

    if (tileCount > MAX_TILES) {
      return null;
    }

    // v2 records exist for Frame messages only.
    if (v2 && type !== MsgType.Frame && tileCount !== 0) {
      return null;
    }

    const recHeader = v2 ? RECT_HEADER_SIZE : TILE_HEADER_SIZE;
    let offset = HEADER_SIZE;
    const tiles = [];

    for (let i = 0; i < tileCount; ++i) {
      if (offset + recHeader > byteLength) {
        return null;
      }

      let x, y, w, h, codec, payloadLen, tileX, tileY;
      if (v2) {
        x = view.getUint16(offset, true);
        y = view.getUint16(offset + 2, true);
        w = view.getUint16(offset + 4, true);
        h = view.getUint16(offset + 6, true);
        codec = view.getUint8(offset + 8);
        // offset + 9 is reserved
        payloadLen = view.getUint32(offset + 10, true);
        if (w < 1 || h < 1 || w > MAX_RECT_SIDE || h > MAX_RECT_SIDE || x + w > width || y + h > height) {
          return null;
        }
        if (payloadLen > MAX_RECT_PAYLOAD || codec > Codec.Jpeg) {
          return null;
        }
        if (codec === Codec.Raw && payloadLen !== w * h * 4) {
          return null;
        }
      } else {
        tileX = view.getUint16(offset, true);
        tileY = view.getUint16(offset + 2, true);
        codec = view.getUint8(offset + 4);
        // offset + 5 is reserved
        payloadLen = view.getUint32(offset + 6, true);
        if (payloadLen > MAX_PAYLOAD) {
          return null;
        }
        // Pixel rectangle of the 64 px tile, clipped to the frame (0 when outside it).
        x = tileX * TILE_SIZE;
        y = tileY * TILE_SIZE;
        w = Math.max(0, Math.min(TILE_SIZE, width - x));
        h = Math.max(0, Math.min(TILE_SIZE, height - y));
      }

      if (offset + recHeader + payloadLen > byteLength) {
        return null;
      }

      const payload = new Uint8Array(
        buffer,
        byteOffset + offset + recHeader,
        payloadLen
      );

      tiles.push(v2 ? { x, y, w, h, codec, payload } : { tileX, tileY, x, y, w, h, codec, payload });

      offset += recHeader + payloadLen;
    }

    return {
      version,
      type,
      flags,
      monitorId,
      streamId,
      seq,
      baseSeq,
      width,
      height,
      tiles
    };
  } catch {
    return null;
  }
}

/**
 * Builds an ACK message.
 * @param {number} streamId
 * @param {number} monitorId
 * @param {number} seq
 * @returns {ArrayBuffer}
 */
export function encodeAck(streamId, monitorId, seq) {
  const buffer = new ArrayBuffer(HEADER_SIZE);
  const view = new DataView(buffer);

  view.setUint16(0, MAGIC, true);
  view.setUint8(2, VERSION);
  view.setUint8(3, MsgType.Ack);
  view.setUint16(4, 0, true); // flags
  view.setUint16(6, monitorId, true);
  view.setUint32(8, streamId, true);
  view.setUint32(12, seq, true);
  view.setUint32(16, 0, true); // base_seq
  view.setUint16(20, 0, true); // width
  view.setUint16(22, 0, true); // height
  view.setUint16(24, 0, true); // tile_count
  view.setUint16(26, 0, true); // reserved

  return buffer;
}

/**
 * Builds a KEYFRAME_REQ message.
 * @param {number} streamId
 * @param {number} monitorId
 * @returns {ArrayBuffer}
 */
export function encodeKeyframeReq(streamId, monitorId) {
  const buffer = new ArrayBuffer(HEADER_SIZE);
  const view = new DataView(buffer);

  view.setUint16(0, MAGIC, true);
  view.setUint8(2, VERSION);
  view.setUint8(3, MsgType.KeyframeReq);
  view.setUint16(4, 0, true); // flags
  view.setUint16(6, monitorId, true);
  view.setUint32(8, streamId, true);
  view.setUint32(12, 0, true); // seq
  view.setUint32(16, 0, true); // base_seq
  view.setUint16(20, 0, true); // width
  view.setUint16(22, 0, true); // height
  view.setUint16(24, 0, true); // tile_count
  view.setUint16(26, 0, true); // reserved

  return buffer;
}

export class ScreenViewer {
  /**
   * @param {HTMLCanvasElement|string} canvasOrUrl
   * @param {object|HTMLCanvasElement} [optionsOrCanvas]
   */
  constructor(canvasOrUrl, optionsOrCanvas = {}) {
    if (typeof canvasOrUrl === 'string') {
      this.defaultUrl = canvasOrUrl;
      this.canvas = optionsOrCanvas;
      this.options = arguments[2] || {};
    } else {
      this.canvas = canvasOrUrl;
      this.options = optionsOrCanvas || {};
    }
    this.tileCanvas = this.canvas && typeof this.canvas.getContext === 'function'
      ? new TileCanvas(this.canvas)
      : null;
    this.stats = this.options.stats || null; // optional ViewerStats (debug overlay)
    this.ws = null;
    this.lastAppliedSeq = 0;
    this.waitingForKeyframe = true;
    this.frameQueue = [];
    this.queueHead = 0; // index of the next frame to draw; the array is compacted now and then (no shift())
    this.drainPromise = null;
    this.generation = 0; // bumped on connect/disconnect: late decodes of an old socket are discarded
  }

  resetQueue() {
    this.generation++;
    this.clearQueue();
    this.drainPromise = null;
  }

  /**
   * Counts an error (kind: 'parse' | 'decode' | 'control') in the stats and tells options.onError({kind}),
   * at most once per second per kind. Counters only, never message content (Step 45).
   */
  reportError(kind) {
    if (this.stats) this.stats[kind + 'Errors']++;
    const now = Date.now();
    this.errorTimes = this.errorTimes || {};
    if (this.options.onError && now - (this.errorTimes[kind] || 0) >= 1000) {
      this.errorTimes[kind] = now;
      try { this.options.onError({ kind }); } catch {}
    }
  }

  clearQueue() {
    this.frameQueue.length = 0;
    this.queueHead = 0;
  }

  queuedFrames() {
    return this.frameQueue.length - this.queueHead;
  }

  /**
   * Opens the socket. `token` (a lease JWT) is sent as the first message {"type":"auth","token":...} once the
   * socket opens, never in the URL (Step 40); omit it for a gateway that authenticates otherwise (dev).
   */
  connect(url, token) {
    const targetUrl = url || this.defaultUrl;
    if (!targetUrl) {
      throw new Error('No WebSocket URL provided to ScreenViewer.connect()');
    }
    this.cancelReconnect();
    if (this.ws) {
      this.disconnect();
    }
    this.lastUrl = targetUrl;
    this.lastToken = token || this.options.token || '';
    this.waitingForKeyframe = true;
    this.resetQueue();
    const ws = new WebSocket(targetUrl);
    this.ws = ws;
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
      // The gateway waits for this before it sends anything; the token is single use, so it is dropped after sending.
      if (this.lastToken) ws.send(JSON.stringify({ type: 'auth', token: this.lastToken }));
      this.lastToken = '';
      this.waitingForKeyframe = true;
      this.requestKeyframe(0, 0);
      if (this.options.onOpen) this.options.onOpen();
    };

    ws.onclose = (ev) => {
      if (this.options.onClose) this.options.onClose(ev);
      if (this.ws === ws) { // not closed by disconnect() or replaced by connect()
        this.ws = null;
        this.scheduleReconnect(ev && ev.code);
      }
    };

    ws.onerror = (err) => {
      if (this.options.onError) this.options.onError(err);
    };

    ws.onmessage = (event) => {
      if (typeof event.data === 'string') {
        // Control JSON message
        try {
          const json = JSON.parse(event.data);
          if (this.options.onControlMessage) {
            this.options.onControlMessage(json);
          }
        } catch {
          this.reportError('control');
        }
        return;
      }

      // Synchronous: parse, ACK and queue. Decoding and drawing happen in drain(), one frame at a time.
      this.handleBinaryFrame(event.data);
    };
  }

  /**
   * Auto-reconnect (Step 33). On when options.getUrl is given (it returns the next WebSocket URL with a fresh
   * token, sync or async, as a URL string or {url, token}; a falsy result or a throw counts as a failed attempt) or
   * options.autoReconnect === true (reuses the last URL; its token was single use, so this only suits dev gateways). Backoff 1 s, 2 s, 4 s ... 30 s with jitter; reset by the first frame of a connection.
   * Policy closes (1008) are final. options.onReconnect(attempt, delayMs) is told about each wait.
   */
  scheduleReconnect(code) {
    if (code === 1008 || (!this.options.getUrl && this.options.autoReconnect !== true)) return;
    const attempt = (this.reconnectAttempts = (this.reconnectAttempts || 0) + 1);
    const base = Math.min(30000, 1000 * 2 ** Math.min(attempt - 1, 5));
    const delay = Math.round(base * (0.8 + Math.random() * 0.4));
    if (this.options.onReconnect) this.options.onReconnect(attempt, delay);
    const epoch = this.reconnectEpoch = this.reconnectEpoch || 0;
    this.reconnectTimer = setTimeout(async () => {
      this.reconnectTimer = null;
      let url = this.lastUrl;
      let token = '';
      if (this.options.getUrl) {
        try {
          url = await this.options.getUrl();
          if (url && typeof url === 'object') ({ url, token } = url);
        } catch {
          url = null;
        }
      }
      if (epoch !== this.reconnectEpoch || this.ws) return; // disconnect()/connect() ran while waiting
      if (!url) {
        this.scheduleReconnect();
        return;
      }
      try {
        this.connect(url, token);
      } catch {
        this.scheduleReconnect();
      }
    }, delay);
  }

  cancelReconnect() {
    this.reconnectEpoch = (this.reconnectEpoch || 0) + 1;
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
  }

  /**
   * Takes one binary message from the socket: parses it, ACKs it on receipt (not on paint, so a slow
   * decode never stalls the agent's send window) and queues it. Returns the promise of the running
   * drain, which settles when the queue is empty.
   * @param {ArrayBuffer} data
   * @returns {Promise<void>}
   */
  handleBinaryFrame(data) {
    const msg = parseMessage(data);
    if (msg && msg.type === MsgType.Monitors) { // not a frame: no ACK, nothing drawn
      this.monitors = msg.monitors;
      this.activeMonitor = msg.monitorId;
      this.allScreens = (msg.flags & Flags.ALL_SCREENS) !== 0;
      if (this.options.onMonitors) {
        this.options.onMonitors({ monitors: msg.monitors, active: msg.monitorId, allScreens: this.allScreens });
      }
      return this.drainPromise || Promise.resolve();
    }
    if (msg && msg.type === MsgType.Cursor) { // pointer position only (Step 34): width/height carry x/y, no ACK
      if (this.tileCanvas) this.tileCanvas.setCursor(msg.width, msg.height);
      if (this.options.onCursor) this.options.onCursor(msg.width, msg.height);
      return this.drainPromise || Promise.resolve();
    }
    if (!msg) this.reportError('parse');
    if (!msg || msg.type !== MsgType.Frame) {
      return this.drainPromise || Promise.resolve(); // reject malformed message without throwing
    }
    this.reconnectAttempts = 0;
    this.sendAck(msg.streamId, msg.monitorId, msg.seq);
    if (this.stats) msg.receivedAt = this.stats.recordReceived(data.byteLength);

    // A keyframe or full frame repaints everything, so frames queued before it are stale.
    if (msg.flags & (Flags.KEY | Flags.FULL)) {
      this.clearQueue();
    } else if (this.queuedFrames() >= MAX_QUEUED_FRAMES) {
      // Behind by more than a delta chain can recover from: drop it and ask for a fresh keyframe.
      this.clearQueue();
      if (this.stats) this.stats.queueDrops++;
      this.waitingForKeyframe = true;
      this.requestKeyframeLimited(msg.streamId, 0);
    }
    this.frameQueue.push(msg);
    return this.drain();
  }

  /** Applies queued frames in arrival order, one at a time; tiles of a frame decode in parallel. */
  drain() {
    if (!this.drainPromise) {
      const gen = this.generation;
      this.drainPromise = (async () => {
        try {
          while (this.queueHead < this.frameQueue.length && gen === this.generation) {
            const next = this.frameQueue[this.queueHead];
            this.frameQueue[this.queueHead++] = undefined; // release the frame's memory
            if (this.queueHead >= 16 && this.queueHead * 2 >= this.frameQueue.length) {
              this.frameQueue.splice(0, this.queueHead); // compact
              this.queueHead = 0;
            }
            try {
              await this.applyFrame(next, gen);
            } catch {
              // a throwing onFrame callback must not stop the queue
            }
          }
        } finally {
          if (gen === this.generation) this.drainPromise = null;
        }
      })();
    }
    return this.drainPromise;
  }

  requestKeyframeLimited(streamId, monitorId) {
    if (Date.now() - (this.keyframeReqTime || 0) > 1500) {
      this.requestKeyframe(streamId, monitorId);
      this.keyframeReqTime = Date.now();
    }
  }

  async decodeTile(tile, msg) {
    try {
      if (tile.codec === Codec.Webp) {
        return await createImageBitmap(new Blob([tile.payload], { type: 'image/webp' }));
      }
      if (tile.codec === Codec.Jpeg) {
        return await createImageBitmap(new Blob([tile.payload], { type: 'image/jpeg' }));
      }
      if (tile.codec === Codec.Raw) {
        const tileW = tile.w;
        const tileH = tile.h;
        if (tileW < 1 || tileH < 1) return null;
        // Convert BGRA to RGBA for ImageData if needed
        const rgba = new Uint8ClampedArray(tileW * tileH * 4);
        for (let p = 0; p < tileW * tileH; ++p) {
          rgba[p * 4 + 0] = tile.payload[p * 4 + 2]; // R
          rgba[p * 4 + 1] = tile.payload[p * 4 + 1]; // G
          rgba[p * 4 + 2] = tile.payload[p * 4 + 0]; // B
          rgba[p * 4 + 3] = 255; // A (force opaque; BGRX byte 3 can be 0x00)
        }
        return await createImageBitmap(new ImageData(rgba, tileW, tileH));
      }
    } catch {
      // Skip un-decodable tile without throwing
    }
    this.reportError('decode');
    return null;
  }

  async applyFrame(msg, gen) {
    const isKey = (msg.flags & Flags.KEY) !== 0;
    const isFull = (msg.flags & Flags.FULL) !== 0;
    const isDelta = (msg.flags & Flags.DELTA) !== 0;

    // Sequence checks first (synchronous), so lastAppliedSeq follows frames in order.
    if (isKey || isFull) {
      this.lastAppliedSeq = msg.seq;
      if (isKey) this.waitingForKeyframe = false;
    } else if (isDelta) {
      if (this.waitingForKeyframe) {
        this.requestKeyframeLimited(msg.streamId, 0);
        return; // Drop deltas until requested keyframe arrives
      }
      if (msg.baseSeq !== this.lastAppliedSeq) {
        // Gap or mismatch detected: send keyframe request
        this.requestKeyframe(msg.streamId, 0);
        this.waitingForKeyframe = true;
        this.keyframeReqTime = Date.now();
        return;
      }
      this.lastAppliedSeq = msg.seq;
    }

    const bitmaps = await Promise.all(msg.tiles.map((t) => this.decodeTile(t, msg)));
    const tc = this.tileCanvas;
    if (gen !== this.generation || !tc) {
      for (const b of bitmaps) if (b && typeof b.close === 'function') b.close();
      return; // disconnected or reconnected while decoding
    }

    if (isKey || isFull) tc.setSize(msg.width, msg.height);
    if (msg.flags & Flags.MASKED) {
      tc.setBanner('PRIVACY MASKED');
    } else if (msg.flags & Flags.SECURE) {
      tc.setBanner('SECURE DESKTOP');
    } else if (msg.flags & Flags.CACHED) {
      tc.setBanner('LAST PICTURE (CACHED)');
    } else {
      tc.setBanner(null);
    }

    const decodedTiles = [];
    msg.tiles.forEach((t, i) => {
      if (bitmaps[i]) decodedTiles.push({ x: t.x, y: t.y, bitmap: bitmaps[i] });
    });
    tc.drawTiles(decodedTiles);
    if (this.stats && msg.receivedAt !== undefined) this.stats.recordPaint(msg.receivedAt);

    if (this.options.onFrame) {
      this.options.onFrame(msg);
    }
  }

  sendAck(streamId, monitorId, seq) {
    if (this.ws && this.ws.readyState === 1 /* OPEN */) {
      const ackBytes = encodeAck(streamId, monitorId, seq);
      this.ws.send(ackBytes);
    }
  }

  /**
   * Asks the agent to show monitor `id` (1..monitors.length) or ALL_SCREENS (Step 35). The picture changes size, so
   * frames of the old one are dropped until the keyframe of the new one arrives. False when the list does not
   * contain it (nothing sent).
   */
  selectMonitor(id) {
    const ok = id === ALL_SCREENS ? this.allScreens === true : Array.isArray(this.monitors) && id >= 1 && id <= this.monitors.length;
    if (!ok || !this.ws || this.ws.readyState !== 1 /* OPEN */) return false;
    this.waitingForKeyframe = true;
    this.clearQueue();
    this.requestKeyframe(0, id);
    return true;
  }

  requestKeyframe(streamId, monitorId) {
    if (this.ws && this.ws.readyState === 1 /* OPEN */) {
      const reqBytes = encodeKeyframeReq(streamId, monitorId);
      if (this.stats) this.stats.keyframeRequests++;
      this.ws.send(reqBytes);
    }
  }

  /**
   * Hands a renewed lease token (from POST /api/screen/{audit}/renew) to the gateway on the open
   * socket, before the current lease ends; otherwise the gateway closes the view (code 4001).
   * The gateway answers with a control message {type: 'lease', expires_at}.
   * @param {string} token
   * @returns {boolean} false when there is no open socket or no token
   */
  renewLease(token) {
    if (!token || !this.ws || this.ws.readyState !== 1 /* OPEN */) {
      return false;
    }
    this.ws.send(JSON.stringify({ type: 'renew', token }));
    return true;
  }

  disconnect() {
    this.cancelReconnect();
    this.reconnectAttempts = 0;
    this.resetQueue();
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    if (this.tileCanvas) {
      this.tileCanvas.clear();
    }
  }
}
