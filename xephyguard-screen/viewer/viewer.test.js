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
  MsgType,
  Flags,
  Codec
} from './viewer.js';

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
