import test from 'node:test';
import assert from 'node:assert/strict';
import { ViewerStats, STATS_WINDOW_MS } from './stats.js';

function clock() {
  let t = 0;
  const c = () => t;
  c.advance = (ms) => { t += ms; };
  return c;
}

test('ViewerStats: fps, kbit/s and paint lag over the window', () => {
  const now = clock();
  const s = new ViewerStats(now);
  for (let i = 0; i < 10; i++) {
    const rx = s.recordReceived(1250); // 10 000 bits each
    now.advance(30);
    s.recordPaint(rx);
    now.advance(70);
  }
  const snap = s.snapshot();
  assert.equal(snap.fps, 10 / (STATS_WINDOW_MS / 1000));
  assert.equal(snap.kbps, (10 * 10000) / 1000 / (STATS_WINDOW_MS / 1000));
  assert.equal(snap.lagMs, 30);
  assert.equal(snap.lagMaxMs, 30);
});

test('ViewerStats: old samples leave the window and an idle viewer reads zero', () => {
  const now = clock();
  const s = new ViewerStats(now);
  s.recordPaint(s.recordReceived(500));
  now.advance(STATS_WINDOW_MS + 1);
  const snap = s.snapshot();
  assert.deepEqual([snap.fps, snap.kbps, snap.lagMs, snap.lagMaxMs], [0, 0, 0, 0]);
});

test('ViewerStats: format shows counters and holds no identity', () => {
  const s = new ViewerStats(clock());
  s.keyframeRequests = 2;
  s.queueDrops = 1;
  const text = s.format(3);
  assert.match(text, /fps/);
  assert.match(text, /queue 3/);
  assert.match(text, /keyframe req 2  queue drops 1/);
});
