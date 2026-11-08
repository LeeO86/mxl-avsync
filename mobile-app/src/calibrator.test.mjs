import test from 'node:test';
import assert from 'node:assert/strict';
import { calibratorText, takeProgress, makePulseCalibration, makeSenderCalibration, validSenderCalibration, senderCalibrationOptions, senderCalibrationLabel } from './calibrator.mjs';
import { validCalibration, calibrationSummary, prepareReading } from './workflow.mjs';

const context = { platform: 'ios', model: 'iPhone17,1', os: '26.6', audioRoute: 'MicrophoneBuiltIn/Speaker', sampleRate: 48000, cameraMode: '1920x1080@240' };

test('result text and progress ring', () => {
  assert.equal(calibratorText(12.34, 0.41), '+12.3 ms ± 0.4');
  assert.equal(calibratorText(-38, 1), '−38.0 ms ± 1.0');
  assert.equal(calibratorText(0, 0.01), '0.0 ms ± 0.1');
  assert.equal(calibratorText(null, 1), '—');
  assert.deepEqual(takeProgress({ pairs: 5, wanted: 20 }), { pairs: 5, wanted: 20, share: 0.25 });
  assert.deepEqual(takeProgress({ pairs: 30 }), { pairs: 20, wanted: 20, share: 1 });
  assert.equal(takeProgress(null).share, 0);
});

test('camera mode gives the same analyser calibration record as the clap, applied the same way', () => {
  const report = { kind: 'calibrator', mode: 'camera', offsetMs: 12.3, uncertaintyMs: 0.4, pairs: 21, fps: 240, captureContext: context };
  const record = makePulseCalibration(report, '26.6', 1000);
  assert.equal(record.method, 'calibrator-pulse');
  assert.equal(record.capturePath, 'native-raw-v1');
  assert.equal(record.uncertainty_ms, 0.4);
  assert.equal(validCalibration(record, '26.6', 2000, context), record);
  assert.equal(validCalibration(record, '26.6', 2000, { ...context, cameraMode: '1920x1080@60' }), null);
  assert.equal(calibrationSummary(record, '26.6', 2000, context).appliedOffsetMs, 12.3);
  assert.equal(prepareReading({ offset_ms: 50, eventCount: 3 }, record, 3000).offset_ms, 50 - 12.3);
  assert.throws(() => makePulseCalibration({ kind: 'calibrator', mode: 'display', offsetMs: 1 }, '26.6', 1));
  assert.throws(() => makePulseCalibration({ kind: 'error', message: 'x' }, '26.6', 1));
});

test('display mode gives a transponder sender calibration in AVS4 steps', () => {
  const now = Date.UTC(2026, 10, 8, 12);
  const record = makeSenderCalibration({ calibration: { ok: true, offsetMs: -38.04, uncertaintyMs: 0.12, pairs: 20, audioRoute: 'Speaker', directionMs: 2.9 } }, '26.6', now);
  assert.equal(record.method, 'transponder');
  assert.equal(validSenderCalibration(record, '26.6', now + 1), record);
  assert.equal(validSenderCalibration(record, '26.7', now + 1), null);
  assert.equal(validSenderCalibration(record, '26.6', now + 91 * 86400000), null);
  assert.deepEqual(senderCalibrationOptions(record, '26.6', now), {
    senderCalibrationMethod: 'transponder', senderCalibrationOffsetMs: '-38.0', senderCalibrationUncertaintyMs: '0.5',
    senderCalibrationDay: String(Math.floor((now - Date.UTC(2020, 0, 1)) / 86400000)), senderCalibrationRoute: 'Speaker' });
  assert.equal(senderCalibrationOptions({ ...record, uncertainty_ms: 1.26 }, '26.6', now).senderCalibrationUncertaintyMs, '1.5');
  assert.deepEqual(senderCalibrationOptions(record, '26.7', now), {});
  assert.deepEqual(senderCalibrationOptions(null, '26.6', now), {});
  assert.equal(senderCalibrationLabel(record, '26.6', now, () => 'today'), '−38.0 ms ± 0.1 · today');
  assert.equal(senderCalibrationLabel(record, '26.7', now, () => 'today'), 'Not valid any more');
  assert.equal(senderCalibrationLabel(null, '26.6', now, () => 'today'), 'Not calibrated');
  assert.throws(() => makeSenderCalibration({ calibrationError: 'No clicks heard.' }, '26.6', now), /No clicks heard/);
  assert.throws(() => makeSenderCalibration({}, '26.6', now));
});
