import test from 'node:test';
import assert from 'node:assert/strict';
import {
  calibrationOffsetMs,
  detectSpike,
  encodeWav,
  framesAround,
  measureSentence,
  nearestFrameIndex,
  offsetText,
  selectHighestFrameRate,
  shownOffset,
  spikeBucket,
  statusLine,
  stepIndex,
  waveformPeaks,
} from './measure.mjs';

test('the sentence follows the sign of av_offset_ms', () => {
  assert.equal(measureSentence(18), 'audio is 18 ms late');
  assert.equal(measureSentence(-7.2), 'audio is 7 ms early');
  assert.equal(measureSentence(0), 'audio is in sync');
  assert.equal(measureSentence(null), 'no measurement');
  assert.equal(offsetText(18), '+18.0 ms');
  assert.equal(offsetText(-7), '-7.0 ms');
  assert.equal(offsetText(null), '—');
});

test('calibration offset is audio time minus the selected frame', () => {
  assert.equal(calibrationOffsetMs(1500, 1480), 20);
  assert.equal(calibrationOffsetMs(1500, 1540), -40);
  assert.equal(calibrationOffsetMs(1500, 1500), 0);
});

test('a saved calibration is subtracted from the raw reading', () => {
  assert.equal(shownOffset(30, { offset_ms: 12 }), 18);
  assert.equal(shownOffset(30, null), 30);
});

test('the status line names quality, calibration, and uncertainty', () => {
  assert.equal(
    statusLine({ quality: 'good', calibrated: true, uncertaintyMs: null }),
    'Quality: good · Calibration: set · Uncertainty: unknown',
  );
  assert.equal(
    statusLine({ quality: null, calibrated: false, uncertaintyMs: 0.4 }),
    'Quality: — · Calibration: missing · Uncertainty: ±0.4 ms',
  );
});

test('a click in a quiet buffer is the spike', () => {
  const rate = 1000;
  const samples = new Array(200).fill(0.01);
  samples[140] = 0.95;
  const spike = detectSpike(samples, rate);
  assert.equal(spike.index, 140);
  assert.equal(spike.timeMs, 140);
  assert.equal(detectSpike(new Array(50).fill(0.01), rate), null);
});

test('frames around the spike stay in order and the nearest one is selected', () => {
  const frames = [
    { timeMs: 900 },
    { timeMs: 1000 },
    { timeMs: 1100 },
    { timeMs: 1600 },
  ];
  const window = framesAround(frames, 1020, 150, 150);
  assert.deepEqual(window.map((frame) => frame.timeMs), [900, 1000, 1100]);
  assert.equal(nearestFrameIndex(window, 1020), 1);
  assert.equal(stepIndex(0, -1, 3), 0);
  assert.equal(stepIndex(2, 1, 3), 2);
  assert.equal(stepIndex(1, -1, 3), 0);
});

test('the waveform marks the spike bucket and the wav header is pcm', () => {
  const samples = [0, 0.2, 0.5, 1];
  const peaks = waveformPeaks(samples, 4);
  assert.equal(peaks.length, 4);
  assert.equal(peaks[3], 1);
  assert.equal(spikeBucket(3, 4, 4), 3);
  const wav = encodeWav(samples, 8000);
  assert.equal(String.fromCharCode(...wav.slice(0, 4)), 'RIFF');
  assert.equal(String.fromCharCode(...wav.slice(8, 12)), 'WAVE');
});

test('the camera opens on the fastest listed format', () => {
  const chosen = selectHighestFrameRate([
    { maxFps: 30, width: 1920 },
    { maxFps: 60, width: 1280 },
    { maxFps: 60, width: 1920 },
  ]);
  assert.equal(chosen.maxFps, 60);
  assert.equal(chosen.width, 1920);
  assert.equal(selectHighestFrameRate([]), null);
});
