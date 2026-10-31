import test from 'node:test';
import assert from 'node:assert/strict';
import { validCalibration, calibrationSummary, makeCalibration, prepareReading, nearestFrame } from './workflow.mjs';

test('calibration uses real frame timestamps and expires on OS update or after 90 days', () => {
  const take = { frames: [{ timeMs: 960 }, { timeMs: 980 }, { timeMs: 1000 }], spikeTimeMs: 1015, fps: 50 };
  assert.equal(nearestFrame(take.frames, 995), 2);
  const record = makeCalibration(take, 1, '26.6', 1000);
  assert.equal(record.offset_ms, 35);
  assert.equal(validCalibration(record, '26.6', 2000), record);
  assert.equal(validCalibration(record, '26.7', 2000), null);
  assert.equal(validCalibration(record, '26.6', 1001 + 90 * 86400000), null);
  assert.throws(() => makeCalibration(take, 4, '26.6', 1000));
});

test('analysis subtracts the capture correction once and preserves raw evidence', () => {
  const report = { offset_ms: 55, eventCount: 4, spread_ms: 2, events: [{ offset_ms: 55 }] };
  const result = prepareReading(report, { offset_ms: 20 }, 1000);
  assert.equal(result.offset_ms, 35);
  assert.equal(result.raw_offset_ms, 55);
  assert.equal(result.events[0].offset_ms, 55);
  assert.equal(result.uncertainty_ms, null);
  assert.equal(prepareReading(report, null, 1000).calibrated, false);
  assert.equal(prepareReading(report, null, 1000).offset_ms, 55);
  assert.throws(() => prepareReading({ offset_ms: null, eventCount: 0 }, null, 1000));
});


test('settings reports the applied correction, and distinguishes stale saved values from zero', () => {
  const record = makeCalibration({ frames: [{ timeMs: 120 }], spikeTimeMs: 108, fps: 60 }, 0, '26.6', 1000);
  assert.deepEqual(calibrationSummary(record, '26.6', 2000), { state: 'active', appliedOffsetMs: -12, storedOffsetMs: -12, savedAt: 1000 });
  for (const [os, now] of [['26.7', 2000], ['26.6', 1001 + 90 * 86400000]]) {
    const summary = calibrationSummary(record, os, now);
    assert.equal(summary.state, 'inactive');
    assert.equal(summary.appliedOffsetMs, 0);
    assert.equal(summary.storedOffsetMs, -12);
  }
  assert.deepEqual(calibrationSummary(null, '26.6', 2000), { state: 'missing', appliedOffsetMs: 0, storedOffsetMs: null, savedAt: null });
  assert.equal(validCalibration({ ...record, savedAt: undefined }, '26.6', 2000), null);
  assert.equal(calibrationSummary({ ...record, offset_ms: 0 }, '26.6', 2000).state, 'active');
});

test('calibration is scoped to model, route, sample rate and camera mode without deleting the stored correction', () => {
 const context={platform:'ios',model:'iPhone18,1',os:'26.6',audioRoute:'MicrophoneBuiltIn/Speaker',sampleRate:48000,cameraMode:'1920x1080@240'};
 const record=makeCalibration({frames:[{timeMs:100}],spikeTimeMs:112,fps:240,captureContext:context},0,'26.6',1000);
 assert.equal(validCalibration(record,'26.6',2000,context),record);
 for (const changed of [{audioRoute:'BluetoothHFP/BluetoothHFP'},{sampleRate:44100},{cameraMode:'1920x1080@60'},{model:'iPhone19,1'}]) {
  assert.equal(validCalibration(record,'26.6',2000,{...context,...changed}),null);
 }
 assert.equal(validCalibration({...record,captureContext:null},'26.6',2000,context),null);
 assert.equal(calibrationSummary(record,'26.6',2000,{...context,sampleRate:44100}).storedOffsetMs,12);
});
