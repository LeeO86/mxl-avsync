import test from 'node:test';
import assert from 'node:assert/strict';
import { liveView, offsetText, offsetSentence, savedResult, clockCorrection, clockText, HOLD_MS } from './live.mjs';

const status = extra => ({ kind: 'status', stage: 'measuring', timeMs: 5000, signal: { live: true, sourceId: 7, testId: 'phone-ab' },
 reading: { offsetMs: 12.5, spreadMs: 0.8, events: 8, videoLatencyMs: 70, audioLatencyMs: 82.5 }, ...extra });
const reference = { source: 'ntp', clockId: 'c', timebase: 'ios-host', referenceUnixMs: 1.8e12, referenceMonotonicMs: 1000, uncertaintyMs: 2 };
const mapping = { clockId: 'c', timebase: 'ios-host', monotonicMs: 3000, deviceUnixMs: 1.8e12 + 2000 - 15 };

test('a measuring report shows the calibrated AV offset and no latency without a clock', () => {
 const view = liveView(status(), { calibrationMs: 2.5 });
 assert.equal(view.stage, 'measuring');
 assert.equal(view.reading.offsetMs, 10);
 assert.equal(view.reading.rawOffsetMs, 12.5);
 assert.equal(view.reading.videoLatencyMs, null);
});

test('the NTP reference corrects device-UTC latencies', () => {
 const corrected = clockCorrection({ clockMapping: mapping }, reference);
 assert.equal(corrected.state, 'estimated');
 assert.equal(corrected.correctionMs, 15);
 const view = liveView(status({ clockMapping: mapping }), { reference });
 assert.equal(view.reading.videoLatencyMs, 85);
 assert.equal(view.reading.audioLatencyMs, 97.5);
});

test('stages map to short messages and a stale report waits for the camera', () => {
 assert.equal(liveView(status({ stage: 'no_signal', reading: undefined })).message, 'Point the camera at the test signal');
 assert.equal(liveView(status(), { ageMs: 4000 }).message, 'Waiting for the camera…');
 assert.equal(liveView(null).stage, 'starting');
});

test('a dropped signal keeps the last reading for a while, marked as held', () => {
 const held = { ...liveView(status()).reading };
 const view = liveView(status({ stage: 'no_signal', reading: undefined }), { held });
 assert.equal(view.reading.offsetMs, 12.5);
 assert.equal(view.reading.fresh, false);
 assert.equal(liveView(status({ stage: 'no_signal', reading: undefined }), { held: { ...held, atMs: Date.now() - HOLD_MS - 1 } }).reading, null);
});

test('texts and saved results', () => {
 assert.equal(offsetText(12.34), '+12.3 ms');
 assert.equal(offsetText(-3), '−3.0 ms');
 assert.equal(offsetText(0.01), '±0.0 ms');
 assert.equal(offsetText(undefined), '—');
 assert.equal(offsetSentence(12), 'Audio 12.0 ms after video');
 assert.equal(offsetSentence(-1), 'Audio 1.0 ms before video');
 assert.equal(offsetSentence(0.2), 'Audio and video in sync');
 const saved = savedResult(liveView(status()).reading, { calibration: { offset_ms: 1 }, now: 42 });
 assert.deepEqual([saved.id, saved.offsetMs, saved.calibrationMs, saved.held], ['42', 12.5, 1, false]);
 const withContext = savedResult(liveView(status()).reading, { captureContext: { cameraMode: 'm' }, now: 43 });
 assert.deepEqual([withContext.sourceId, withContext.testId, withContext.captureContext], [7, 'phone-ab', { cameraMode: 'm' }]);
});

test('the clock label shows the check accuracy and its age, not the holdover allowance', () => {
 assert.equal(clockText({ ageMs: 42400, sampleUncertaintyMs: 1.23, uncertaintyMs: 3.35 }), 'NTP ±1.2 ms · checked 42 s ago');
 assert.equal(clockText({ ageMs: 185000, sampleUncertaintyMs: 0.8, uncertaintyMs: 10 }), 'NTP ±0.8 ms · checked 3 min ago');
});
