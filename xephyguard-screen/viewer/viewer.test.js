import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  parseMessage,
  encodeAck,
  encodeKeyframeReq,
  MAGIC,
  VERSION,
  VERSION_2,
  RECT_HEADER_SIZE,
  MsgType,
  Flags,
  Codec,
  ScreenViewer,
  MAX_QUEUED_FRAMES,
  HEADER_SIZE,
  ALL_SCREENS
} from './viewer.js';
import { ViewerStats } from './stats.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const fixturesDir = path.resolve(__dirname, '../tests/fixtures');

test('Viewer Parser: Parse keyframe.bin fixture', () => {
  const filePath = path.join(fixturesDir, 'keyframe.bin');
  assert.ok(fs.existsSync(filePath), 'keyframe.bin fixture must exist');

  const buf = fs.readFileSync(filePath);
  const msg = parseMessage(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));

  assert.ok(msg !== null, 'keyframe must be parsed successfully');
  assert.equal(msg.type, MsgType.Frame);
  assert.ok((msg.flags & Flags.KEY) !== 0, 'KEY flag must be set');
  assert.equal(msg.baseSeq, 0, 'base_seq must be 0 for keyframe');
  assert.equal(msg.width, 1280);
  assert.equal(msg.height, 720);
  assert.ok(msg.tiles.length > 0, 'must contain tiles');

  for (const tile of msg.tiles) {
    assert.equal(tile.codec, Codec.Webp);
    assert.ok(tile.payload.length > 0);
  }
});

test('Viewer Parser: Parse delta.bin fixture', () => {
  const filePath = path.join(fixturesDir, 'delta.bin');
  assert.ok(fs.existsSync(filePath), 'delta.bin fixture must exist');

  const buf = fs.readFileSync(filePath);
  const msg = parseMessage(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));

  assert.ok(msg !== null, 'delta must be parsed successfully');
  assert.equal(msg.type, MsgType.Frame);
  assert.ok((msg.flags & Flags.DELTA) !== 0, 'DELTA flag must be set');
  assert.equal(msg.seq, 2);
  assert.equal(msg.baseSeq, 1);
  assert.equal(msg.width, 1280);
  assert.equal(msg.height, 720);
  assert.ok(msg.tiles.length > 0, 'must contain changed tiles');
});

test('Viewer Parser: Parse full.bin fixture', () => {
  const filePath = path.join(fixturesDir, 'full.bin');
  assert.ok(fs.existsSync(filePath), 'full.bin fixture must exist');

  const buf = fs.readFileSync(filePath);
  const msg = parseMessage(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));

  assert.ok(msg !== null, 'full must be parsed successfully');
  assert.equal(msg.type, MsgType.Frame);
  assert.ok((msg.flags & Flags.FULL) !== 0, 'FULL flag must be set');
  assert.equal(msg.width, 1280);
  assert.equal(msg.height, 720);
});

test('Viewer Parser: Reject malformed inputs safely without throwing', () => {
  // Truncated header
  assert.equal(parseMessage(new ArrayBuffer(10)), null);
  assert.equal(parseMessage(new ArrayBuffer(27)), null);

  // Wrong magic
  const badMagic = new Uint8Array(28);
  const dv1 = new DataView(badMagic.buffer);
  dv1.setUint16(0, 0x1234, true);
  dv1.setUint8(2, VERSION);
  assert.equal(parseMessage(badMagic), null);

  // Wrong version
  const badVer = new Uint8Array(28);
  const dv2 = new DataView(badVer.buffer);
  dv2.setUint16(0, MAGIC, true);
  dv2.setUint8(2, 99);
  assert.equal(parseMessage(badVer), null);

  // Excessive tile count
  const badTiles = new Uint8Array(28);
  const dv3 = new DataView(badTiles.buffer);
  dv3.setUint16(0, MAGIC, true);
  dv3.setUint8(2, VERSION);
  dv3.setUint16(24, 5000, true); // > 4096
  assert.equal(parseMessage(badTiles), null);

  // Truncated tile payload
  const truncTile = new Uint8Array(28 + 10 + 5);
  const dv4 = new DataView(truncTile.buffer);
  dv4.setUint16(0, MAGIC, true);
  dv4.setUint8(2, VERSION);
  dv4.setUint16(24, 1, true); // 1 tile
  dv4.setUint32(28 + 6, 100, true); // payload_len = 100, but buffer only has 5 bytes!
  assert.equal(parseMessage(truncTile), null);

  // Random fuzzing: 1,000 random byte buffers
  for (let i = 0; i < 1000; ++i) {
    const len = Math.floor(Math.random() * 200);
    const randBuf = new Uint8Array(len);
    for (let b = 0; b < len; ++b) {
      randBuf[b] = Math.floor(Math.random() * 256);
    }
    assert.doesNotThrow(() => {
      parseMessage(randBuf);
    });
  }
});

test('Viewer Protocol: ACK and KEYFRAME_REQ builders', () => {
  const ack = encodeAck(42, 1, 100);
  const ackMsg = parseMessage(ack);
  assert.ok(ackMsg !== null);
  assert.equal(ackMsg.type, MsgType.Ack);
  assert.equal(ackMsg.streamId, 42);
  assert.equal(ackMsg.monitorId, 1);
  assert.equal(ackMsg.seq, 100);

  const req = encodeKeyframeReq(99, 2);
  const reqMsg = parseMessage(req);
  assert.ok(reqMsg !== null);
  assert.equal(reqMsg.type, MsgType.KeyframeReq);
  assert.equal(reqMsg.streamId, 99);
  assert.equal(reqMsg.monitorId, 2);
});

test('Viewer: renewLease sends one renew message on an open socket only', () => {
  const viewer = new ScreenViewer({});
  assert.equal(viewer.renewLease('tok'), false); // no socket

  const sent = [];
  viewer.ws = { readyState: 0, send: (m) => sent.push(m) };
  assert.equal(viewer.renewLease('tok'), false); // still connecting
  viewer.ws.readyState = 1;
  assert.equal(viewer.renewLease(''), false); // no token
  assert.equal(viewer.renewLease('tok'), true);
  assert.deepEqual(sent.map((m) => JSON.parse(m)), [{ type: 'renew', token: 'tok' }]);
});

// ---- ordered frame queue (screen Step 12) ----

function frameBytes({ flags, seq, baseSeq = 0, tiles = [[0, 0]], w = 128, h = 128, stream = 7 }) {
  const parts = [];
  const head = new DataView(new ArrayBuffer(HEADER_SIZE));
  head.setUint16(0, MAGIC, true);
  head.setUint8(2, VERSION);
  head.setUint8(3, MsgType.Frame);
  head.setUint16(4, flags, true);
  head.setUint32(8, stream, true);
  head.setUint32(12, seq, true);
  head.setUint32(16, baseSeq, true);
  head.setUint16(20, w, true);
  head.setUint16(22, h, true);
  head.setUint16(24, tiles.length, true);
  parts.push(new Uint8Array(head.buffer));
  for (const [x, y] of tiles) {
    const th = new DataView(new ArrayBuffer(10));
    th.setUint16(0, x, true);
    th.setUint16(2, y, true);
    th.setUint8(4, Codec.Webp);
    th.setUint32(6, 4, true);
    parts.push(new Uint8Array(th.buffer), new Uint8Array([1, 2, 3, 4]));
  }
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out.buffer;
}

// A viewer on a fake canvas and socket. `decodeDelay(callIndex)` lets a test make some decodes slow.
function rig({ decodeDelay = () => 0 } = {}) {
  const drawn = []; // seq/tile order the canvas received
  const sent = [];
  let calls = 0;
  const closed = [];
  globalThis.createImageBitmap = async () => {
    const i = calls++;
    await new Promise((r) => setTimeout(r, decodeDelay(i)));
    return { id: i, close: () => closed.push(i) };
  };
  const ctx = { drawImage: (bmp, x, y) => drawn.push({ id: bmp.id, x, y }), save() {}, restore() {}, fillRect() {}, fillText() {}, clearRect() {} };
  const canvas = { getContext: () => ctx, width: 0, height: 0 };
  const viewer = new ScreenViewer(canvas, {});
  viewer.ws = { readyState: 1, send: (m) => sent.push(parseMessage(m)), close() {} };
  const applied = [];
  viewer.options.onFrame = (m) => applied.push(m.seq);
  return { viewer, drawn, sent, applied, closed, decodes: () => calls };
}

const KEY = Flags.KEY;
const DELTA = Flags.DELTA;

test('Viewer queue: frames are applied in arrival order even when an earlier one decodes slower', async () => {
  const r = rig({ decodeDelay: (i) => (i === 0 ? 40 : 0) }); // the keyframe's tile is the slow one
  r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 1 }));
  r.viewer.handleBinaryFrame(frameBytes({ flags: DELTA, seq: 2, baseSeq: 1 }));
  r.viewer.handleBinaryFrame(frameBytes({ flags: DELTA, seq: 3, baseSeq: 2 }));
  assert.equal(r.sent.filter((m) => m.type === MsgType.Ack).length, 3, 'all three ACKed on receipt, before any decode finished');
  assert.deepEqual(r.applied, []);
  await r.viewer.drain();
  assert.deepEqual(r.applied, [1, 2, 3]);
  assert.deepEqual(r.drawn.map((d) => d.id), [0, 1, 2]);
  assert.deepEqual(r.sent.map((m) => m.seq), [1, 2, 3]);
});

test('Viewer queue: the tiles of one frame decode in parallel', async () => {
  const r = rig({ decodeDelay: () => 20 });
  const t0 = Date.now();
  await r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 1, tiles: [[0, 0], [1, 0], [0, 1], [1, 1], [2, 0], [2, 1]] }));
  assert.equal(r.drawn.length, 6);
  assert.ok(Date.now() - t0 < 100, 'six 20 ms decodes must not run one after another');
});

test('Viewer queue: a keyframe drops the frames queued before it', async () => {
  const r = rig({ decodeDelay: () => 10 });
  r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 1 }));
  r.viewer.handleBinaryFrame(frameBytes({ flags: DELTA, seq: 2, baseSeq: 1 }));
  r.viewer.handleBinaryFrame(frameBytes({ flags: DELTA, seq: 3, baseSeq: 2 }));
  r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 10 }));
  await r.viewer.drain();
  assert.deepEqual(r.applied, [1, 10]); // 1 was already decoding; 2 and 3 were stale
  assert.equal(r.sent.filter((m) => m.type === MsgType.Ack).length, 4, 'dropped frames are still ACKed');
});

test('Viewer queue: a gap asks for a keyframe and drops deltas until it arrives', async () => {
  const r = rig();
  await r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 1 }));
  await r.viewer.handleBinaryFrame(frameBytes({ flags: DELTA, seq: 5, baseSeq: 4 })); // 2..4 lost
  assert.equal(r.sent.filter((m) => m.type === MsgType.KeyframeReq).length, 1);
  await r.viewer.handleBinaryFrame(frameBytes({ flags: DELTA, seq: 6, baseSeq: 5 }));
  assert.deepEqual(r.applied, [1]);
  await r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 9 }));
  await r.viewer.handleBinaryFrame(frameBytes({ flags: DELTA, seq: 10, baseSeq: 9 }));
  assert.deepEqual(r.applied, [1, 9, 10]);
});

test('Viewer queue: a backlog beyond the limit is dropped and a keyframe is requested', async () => {
  const r = rig({ decodeDelay: (i) => (i === 0 ? 30 : 0) });
  r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 1 }));
  for (let s = 2; s <= MAX_QUEUED_FRAMES + 3; ++s) r.viewer.handleBinaryFrame(frameBytes({ flags: DELTA, seq: s, baseSeq: s - 1 }));
  await r.viewer.drain();
  assert.ok(r.sent.some((m) => m.type === MsgType.KeyframeReq), 'asked for a fresh keyframe');
  assert.ok(r.applied.length < MAX_QUEUED_FRAMES + 3, 'stale frames were skipped, not all drawn');
});

test('Viewer queue: nothing is drawn after disconnect and late bitmaps are released', async () => {
  const r = rig({ decodeDelay: () => 30 });
  const p = r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 1 }));
  r.viewer.disconnect();
  await p;
  assert.deepEqual(r.drawn, []);
  assert.deepEqual(r.applied, []);
  assert.deepEqual(r.closed, [0]);
});

test('Viewer queue: malformed and non-frame messages are ignored without an ACK', async () => {
  const r = rig();
  await r.viewer.handleBinaryFrame(new ArrayBuffer(5));
  await r.viewer.handleBinaryFrame(encodeAck(7, 0, 1));
  assert.deepEqual(r.sent, []);
  assert.deepEqual(r.applied, []);
});

test('Viewer stats: receipt, paint, keyframe requests and queue overflow are counted when stats are given', async () => {
  const r = rig();
  let t = 0;
  r.viewer.stats = new ViewerStats(() => (t += 5));
  await r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 1 }));
  let snap = r.viewer.stats.snapshot();
  assert.ok(snap.kbps > 0 && snap.fps > 0 && snap.lagMs > 0);
  for (let i = 0; i < MAX_QUEUED_FRAMES + 2; i++) r.viewer.handleBinaryFrame(frameBytes({ flags: DELTA, seq: 2 + i, baseSeq: 1 + i }));
  await r.viewer.drain();
  snap = r.viewer.stats.snapshot();
  assert.equal(snap.queueDrops, 1);
  assert.ok(snap.keyframeRequests >= 1);
});

test('Viewer stats: without stats nothing changes', async () => {
  const r = rig();
  await r.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 1 }));
  assert.deepEqual(r.applied, [1]);
});

// ---- protocol v2 rectangles (screen Step 27) ----

function frameBytes2({ flags = Flags.KEY, seq = 1, baseSeq = 0, rects = [[0, 0, 64, 64]], w = 300, h = 200, stream = 7, codec = Codec.Jpeg, type = MsgType.Frame, payload = [1, 2, 3, 4] }) {
  const parts = [];
  const head = new DataView(new ArrayBuffer(HEADER_SIZE));
  head.setUint16(0, MAGIC, true);
  head.setUint8(2, VERSION_2);
  head.setUint8(3, type);
  head.setUint16(4, flags, true);
  head.setUint32(8, stream, true);
  head.setUint32(12, seq, true);
  head.setUint32(16, baseSeq, true);
  head.setUint16(20, w, true);
  head.setUint16(22, h, true);
  head.setUint16(24, rects.length, true);
  parts.push(new Uint8Array(head.buffer));
  for (const [x, y, rw, rh] of rects) {
    const body = codec === Codec.Raw ? new Uint8Array(rw * rh * 4) : new Uint8Array(payload);
    const rec = new DataView(new ArrayBuffer(RECT_HEADER_SIZE));
    rec.setUint16(0, x, true);
    rec.setUint16(2, y, true);
    rec.setUint16(4, rw, true);
    rec.setUint16(6, rh, true);
    rec.setUint8(8, codec);
    rec.setUint32(10, body.length, true);
    parts.push(new Uint8Array(rec.buffer), body);
  }
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out.buffer;
}

test('Viewer v2: parses pixel rectangles; v1 tiles get a clipped pixel rectangle too', () => {
  const m2 = parseMessage(frameBytes2({ rects: [[256, 100, 44, 100], [0, 0, 256, 256 - 56]] }));
  assert.equal(m2.version, VERSION_2);
  assert.deepEqual(m2.tiles.map((t) => [t.x, t.y, t.w, t.h, t.codec]), [[256, 100, 44, 100, Codec.Jpeg], [0, 0, 256, 200, Codec.Jpeg]]);
  assert.deepEqual([...m2.tiles[0].payload], [1, 2, 3, 4]);

  const m1 = parseMessage(frameBytes({ flags: KEY, seq: 1, tiles: [[0, 0], [1, 1]], w: 100, h: 70 }));
  assert.equal(m1.version, VERSION);
  assert.deepEqual(m1.tiles.map((t) => [t.x, t.y, t.w, t.h]), [[0, 0, 64, 64], [64, 64, 36, 6]]);
});

test('Viewer v2: rejects bad rectangles', () => {
  const bad = (opts) => parseMessage(frameBytes2(opts)) === null;
  assert.equal(bad({ rects: [[0, 0, 0, 10]] }), true);          // w = 0
  assert.equal(bad({ rects: [[0, 0, 10, 0]] }), true);          // h = 0
  assert.equal(bad({ rects: [[0, 0, 257, 10]], w: 400 }), true); // wider than 256
  assert.equal(bad({ rects: [[0, 0, 10, 257]], h: 400 }), true); // taller than 256
  assert.equal(bad({ rects: [[250, 0, 64, 64]] }), true);        // x + w = 314 > 300
  assert.equal(bad({ rects: [[0, 150, 64, 64]] }), true);        // y + h = 214 > 200
  assert.equal(bad({ rects: [[0, 0, 64, 64]], codec: 3 }), true); // unknown codec
  assert.equal(bad({ rects: [[0, 0, 64, 64]], type: MsgType.Cursor }), true); // v2 records on a non-Frame
  assert.equal(bad({ rects: [[0, 0, 64, 64]], codec: Codec.Raw }), false);
  const raw = new Uint8Array(frameBytes2({ rects: [[0, 0, 4, 4]], codec: Codec.Raw }));
  assert.equal(parseMessage(raw.slice(0, raw.length - 1)), null); // truncated
  new DataView(raw.buffer).setUint32(HEADER_SIZE + 10, 4 * 4 * 4 - 4, true);
  assert.equal(parseMessage(raw), null); // raw payload not w*h*4
});

test('Viewer v2: mutated and random buffers never throw', () => {
  const base = new Uint8Array(frameBytes2({ rects: [[10, 20, 30, 40], [100, 100, 64, 64]] }));
  for (let i = 0; i < 5000; ++i) {
    const b = base.slice();
    for (let f = 0, n = 1 + (i % 4); f < n; ++f) b[Math.floor(Math.random() * b.length)] = Math.floor(Math.random() * 256);
    const cut = i % 5 === 0 ? b.subarray(0, Math.floor(Math.random() * (b.length + 1))) : b;
    const m = parseMessage(cut);
    if (m && m.version === VERSION_2) {
      for (const t of m.tiles) assert.ok(t.w >= 1 && t.w <= 256 && t.h >= 1 && t.h <= 256 && t.x + t.w <= m.width && t.y + t.h <= m.height);
    }
  }
});

test('Viewer v2: rectangles are drawn at their pixel position, v1 tiles at tile x 64', async () => {
  const r = rig();
  await r.viewer.handleBinaryFrame(frameBytes2({ rects: [[5, 7, 40, 30], [256, 100, 44, 100]] }));
  await r.viewer.drain();
  assert.deepEqual(r.drawn.map((d) => [d.x, d.y]), [[5, 7], [256, 100]]);
  const r1 = rig();
  await r1.viewer.handleBinaryFrame(frameBytes({ flags: KEY, seq: 1, tiles: [[2, 1]], w: 256, h: 128 }));
  await r1.viewer.drain();
  assert.deepEqual(r1.drawn.map((d) => [d.x, d.y]), [[128, 64]]);
});

test('Viewer v2: a raw rectangle becomes an ImageData of exactly w x h', async () => {
  const r = rig();
  const sizes = [];
  globalThis.ImageData = class { constructor(data, w, h) { sizes.push([data.length, w, h]); } };
  await r.viewer.handleBinaryFrame(frameBytes2({ rects: [[0, 0, 5, 3]], codec: Codec.Raw }));
  await r.viewer.drain();
  delete globalThis.ImageData;
  assert.deepEqual(sizes, [[5 * 3 * 4, 5, 3]]);
});

test('Viewer connect: the token goes out as the first message, never in the URL (Step 40)', () => {
  const sockets = [];
  globalThis.WebSocket = class {
    constructor(url) { this.url = url; this.sent = []; this.readyState = 1; sockets.push(this); }
    send(d) { this.sent.push(d); }
    close() {}
  };
  try {
    const viewer = new ScreenViewer({});
    viewer.connect('wss://ccc/screen-gw/view', 'jwt-1');
    const ws = sockets[0];
    assert.equal(ws.url, 'wss://ccc/screen-gw/view');
    ws.onopen();
    assert.deepEqual(JSON.parse(ws.sent[0]), { type: 'auth', token: 'jwt-1' });
    assert.equal(typeof ws.sent[1], 'object'); // the keyframe request follows the auth message
    // single use: not kept for a later open
    assert.equal(viewer.lastToken, '');
    viewer.disconnect();
  } finally {
    delete globalThis.WebSocket;
  }
});

function monitorsBytes(active, flags, recs) {
  const b = new Uint8Array(HEADER_SIZE + recs.length * 8);
  const v = new DataView(b.buffer);
  v.setUint16(0, MAGIC, true);
  v.setUint8(2, VERSION);
  v.setUint8(3, MsgType.Monitors);
  v.setUint16(4, flags, true);
  v.setUint16(6, active, true);
  v.setUint16(24, recs.length, true);
  recs.forEach((r, i) => r.forEach((n, j) => v.setUint16(HEADER_SIZE + i * 8 + j * 2, n, true)));
  return b;
}

test('Monitors message: parsed with ids 1..N, malformed ones refused', () => {
  const ok = parseMessage(monitorsBytes(2, Flags.ALL_SCREENS, [[0, 0, 1920, 1080], [1920, 0, 1280, 1024]]));
  assert.equal(ok.type, MsgType.Monitors);
  assert.equal(ok.monitorId, 2);
  assert.deepEqual(ok.monitors.map((m) => [m.id, m.x, m.w, m.h]), [[1, 0, 1920, 1080], [2, 1920, 1280, 1024]]);
  assert.equal(parseMessage(monitorsBytes(0, 0, [[0, 0, 0, 10]])), null); // zero width
  const long = monitorsBytes(0, 0, [[0, 0, 10, 10]]);
  assert.equal(parseMessage(long.subarray(0, long.length - 1)), null); // length must match
  assert.equal(parseMessage(new Uint8Array([...long, 0])), null);
  const v2 = monitorsBytes(0, 0, [[0, 0, 10, 10]]);
  v2[2] = 2;
  assert.equal(parseMessage(v2), null);
});

test('Viewer: onMonitors, selectMonitor sends a KeyframeReq with the monitor, recoveries say "keep" (0)', () => {
  const sent = [];
  const viewer = new ScreenViewer({});
  viewer.ws = { readyState: 1, send: (d) => sent.push(d) };
  const seen = [];
  viewer.options.onMonitors = (m) => seen.push(m);
  viewer.handleBinaryFrame(monitorsBytes(0, Flags.ALL_SCREENS, [[0, 0, 640, 720], [640, 0, 640, 720]]).buffer);
  assert.equal(seen.length, 1);
  assert.equal(seen[0].allScreens, true);
  assert.equal(viewer.selectMonitor(3), false); // not in the list
  assert.equal(sent.length, 0);
  assert.equal(viewer.selectMonitor(2), true);
  const req = new DataView(sent[0].buffer ?? sent[0]);
  assert.equal(req.getUint8(3), MsgType.KeyframeReq);
  assert.equal(req.getUint16(6, true), 2);
  assert.equal(viewer.waitingForKeyframe, true);
  assert.equal(viewer.selectMonitor(ALL_SCREENS), true);
  assert.equal(new DataView(sent[1].buffer ?? sent[1]).getUint16(6, true), ALL_SCREENS);
});

test('Protocol: a frame with 5000 tiles parses (limit 16384), a count over the limit is refused', () => {
  const n = 5000;
  const b = new Uint8Array(HEADER_SIZE + n * 11);
  const v = new DataView(b.buffer);
  v.setUint16(0, MAGIC, true);
  v.setUint8(2, VERSION);
  v.setUint8(3, MsgType.Frame);
  v.setUint16(20, 8192, true);
  v.setUint16(22, 8192, true);
  v.setUint16(24, n, true);
  for (let i = 0; i < n; ++i) {
    const o = HEADER_SIZE + i * 11;
    v.setUint16(o, i % 128, true);
    v.setUint16(o + 2, Math.floor(i / 128), true);
    v.setUint8(o + 4, Codec.Raw);
    v.setUint32(o + 6, 1, true);
  }
  assert.equal(parseMessage(b).tiles.length, n);
  v.setUint16(24, 16385, true);
  assert.equal(parseMessage(b), null);
});
