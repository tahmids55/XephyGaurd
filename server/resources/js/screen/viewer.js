import { TileCanvas, TILE_SIZE } from './tilecanvas.js';

export const MAGIC = 0x5847;
export const VERSION = 1;
export const HEADER_SIZE = 28;
export const TILE_HEADER_SIZE = 10;
export const MAX_TILES = 4096;
export const MAX_PAYLOAD = 64 * 64 * 4;

export const MsgType = {
  Frame: 1,
  Cursor: 2,
  Ack: 3,
  KeyframeReq: 4,
  Status: 5
};

export const Flags = {
  KEY: 1,
  FULL: 2,
  DELTA: 4,
  MASKED: 8,
  SECURE: 16,
  CURSOR_F: 32
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
    if (version !== VERSION) {
      return null;
    }

    const type = view.getUint8(3);
    const flags = view.getUint16(4, true);
    const monitorId = view.getUint16(6, true);
    const streamId = view.getUint32(8, true);
    const seq = view.getUint32(12, true);
    const baseSeq = view.getUint32(16, true);
    const width = view.getUint16(20, true);
    const height = view.getUint16(22, true);
    const tileCount = view.getUint16(24, true);

    if (tileCount > MAX_TILES) {
      return null;
    }

    let offset = HEADER_SIZE;
    const tiles = [];

    for (let i = 0; i < tileCount; ++i) {
      if (offset + TILE_HEADER_SIZE > byteLength) {
        return null;
      }

      const tileX = view.getUint16(offset, true);
      const tileY = view.getUint16(offset + 2, true);
      const codec = view.getUint8(offset + 4);
      // offset + 5 is reserved
      const payloadLen = view.getUint32(offset + 6, true);

      if (payloadLen > MAX_PAYLOAD) {
        return null;
      }

      if (offset + TILE_HEADER_SIZE + payloadLen > byteLength) {
        return null;
      }

      const payload = new Uint8Array(
        buffer,
        byteOffset + offset + TILE_HEADER_SIZE,
        payloadLen
      );

      tiles.push({
        tileX,
        tileY,
        codec,
        payload
      });

      offset += TILE_HEADER_SIZE + payloadLen;
    }

    return {
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
    this.ws = null;
    this.lastAppliedSeq = 0;
    this.waitingForKeyframe = true;
  }

  connect(url) {
    const targetUrl = url || this.defaultUrl;
    if (!targetUrl) {
      throw new Error('No WebSocket URL provided to ScreenViewer.connect()');
    }
    if (this.ws) {
      this.disconnect();
    }
    this.waitingForKeyframe = true;
    this.ws = new WebSocket(targetUrl);
    this.ws.binaryType = 'arraybuffer';

    this.ws.onopen = () => {
      this.waitingForKeyframe = true;
      this.requestKeyframe(0, 0);
      if (this.options.onOpen) this.options.onOpen();
    };

    this.ws.onclose = () => {
      if (this.options.onClose) this.options.onClose();
    };

    this.ws.onerror = (err) => {
      if (this.options.onError) this.options.onError(err);
    };

    this.ws.onmessage = async (event) => {
      if (typeof event.data === 'string') {
        // Control JSON message
        try {
          const json = JSON.parse(event.data);
          if (this.options.onControlMessage) {
            this.options.onControlMessage(json);
          }
        } catch {}
        return;
      }

      await this.handleBinaryFrame(event.data);
    };
  }

  async handleBinaryFrame(data) {
    const msg = parseMessage(data);
    if (!msg) {
      return; // Reject malformed message without throwing
    }

    if (msg.type !== MsgType.Frame) {
      return;
    }

    // Check banner flags
    if (msg.flags & Flags.MASKED) {
      this.tileCanvas.setBanner('PRIVACY MASKED');
    } else if (msg.flags & Flags.SECURE) {
      this.tileCanvas.setBanner('SECURE DESKTOP');
    } else {
      this.tileCanvas.setBanner(null);
    }

    // Delta / Keyframe sequence handling
    const isKey = (msg.flags & Flags.KEY) !== 0;
    const isFull = (msg.flags & Flags.FULL) !== 0;
    const isDelta = (msg.flags & Flags.DELTA) !== 0;

    if (isKey) {
      this.tileCanvas.setSize(msg.width, msg.height);
      this.lastAppliedSeq = msg.seq;
      this.waitingForKeyframe = false;
    } else if (isFull) {
      this.tileCanvas.setSize(msg.width, msg.height);
      this.lastAppliedSeq = msg.seq;
    } else if (isDelta) {
      if (this.waitingForKeyframe) {
        if (Date.now() - (this.keyframeReqTime || 0) > 1500) {
          this.requestKeyframe(msg.streamId, msg.monitorId);
          this.keyframeReqTime = Date.now();
        }
        return; // Drop deltas until requested keyframe arrives
      }
      if (msg.baseSeq !== this.lastAppliedSeq) {
        // Gap or mismatch detected: send keyframe request
        this.requestKeyframe(msg.streamId, msg.monitorId);
        this.waitingForKeyframe = true;
        this.keyframeReqTime = Date.now();
        return;
      }
      this.lastAppliedSeq = msg.seq;
    }

    // Decode tiles
    const decodedTiles = [];
    for (const tile of msg.tiles) {
      try {
        let bitmap = null;
        if (tile.codec === Codec.Webp) {
          const blob = new Blob([tile.payload], { type: 'image/webp' });
          bitmap = await createImageBitmap(blob);
        } else if (tile.codec === Codec.Jpeg) {
          const blob = new Blob([tile.payload], { type: 'image/jpeg' });
          bitmap = await createImageBitmap(blob);
        } else if (tile.codec === Codec.Raw) {
          const tileW = Math.min(TILE_SIZE, msg.width - tile.tileX * TILE_SIZE);
          const tileH = Math.min(TILE_SIZE, msg.height - tile.tileY * TILE_SIZE);
          // Convert BGRA to RGBA for ImageData if needed
          const rgba = new Uint8ClampedArray(tileW * tileH * 4);
          for (let p = 0; p < tileW * tileH; ++p) {
            rgba[p * 4 + 0] = tile.payload[p * 4 + 2]; // R
            rgba[p * 4 + 1] = tile.payload[p * 4 + 1]; // G
            rgba[p * 4 + 2] = tile.payload[p * 4 + 0]; // B
            rgba[p * 4 + 3] = tile.payload[p * 4 + 3]; // A
          }
          const imgData = new ImageData(rgba, tileW, tileH);
          bitmap = await createImageBitmap(imgData);
        }

        if (bitmap) {
          decodedTiles.push({
            tileX: tile.tileX,
            tileY: tile.tileY,
            bitmap
          });
        }
      } catch {
        // Skip un-decodable tile without throwing
      }
    }

    // Batch draw on canvas
    this.tileCanvas.drawTiles(decodedTiles);

    if (this.options.onFrame) {
      this.options.onFrame(msg);
    }

    // Send ACK back to gateway/agent
    this.sendAck(msg.streamId, msg.monitorId, msg.seq);
  }

  sendAck(streamId, monitorId, seq) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      const ackBytes = encodeAck(streamId, monitorId, seq);
      this.ws.send(ackBytes);
    }
  }

  requestKeyframe(streamId, monitorId) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      const reqBytes = encodeKeyframeReq(streamId, monitorId);
      this.ws.send(reqBytes);
    }
  }

  disconnect() {
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    if (this.tileCanvas) {
      this.tileCanvas.clear();
    }
  }
}
