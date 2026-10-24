import test from 'node:test';
import assert from 'node:assert/strict';
import {
  clearCalibration,
  clearLive,
  clearResults,
  completeMeasurement,
  createSession,
  saveCalibration,
} from './session.mjs';
import { appendAudio, appendFrame, createRecording, selectedOffsetMs, stepSelection } from './wizard.mjs';

test('a completed measurement is saved newest first and clearing the live reading keeps the list', () => {
  let session = createSession();
  session = completeMeasurement(session, { offset_ms: 10, quality: 'good' }, 1_000);
  session = completeMeasurement(session, { offset_ms: -4, quality: 'good' }, 2_000);
  assert.deepEqual(session.results.map((item) => item.at), [2_000, 1_000]);
  assert.equal(session.live.offset_ms, -4);
  session = clearLive(session);
  assert.equal(session.live, null);
  assert.equal(session.results.length, 2);
});

test('calibration changes the shown offset and survives a cleared results list', () => {
  let session = saveCalibration(createSession(), { offset_ms: 5 });
  session = completeMeasurement(session, { offset_ms: 20, quality: 'good' }, 3_000);
  assert.equal(session.results[0].offset_ms, 15);
  session = clearResults(session);
  assert.equal(session.results.length, 0);
  assert.equal(session.calibration.offset_ms, 5);
  session = clearCalibration(session);
  assert.equal(session.calibration, null);
});

test('the wizard stops after the spike and the chosen frame sets the offset', () => {
  let recording = createRecording(1000, 0);
  const quiet = new Array(80).fill(0.01);
  recording = appendAudio(recording, quiet);
  assert.equal(recording.spike, null);
  const click = quiet.slice();
  click[40] = 0.9;
  recording = appendAudio(recording, click);
  assert.ok(recording.spike);
  const spikeAt = recording.audioOriginMs + recording.spike.timeMs;
  recording = appendFrame(recording, { timeMs: spikeAt - 40 });
  for (let i = 0; i < 3; i += 1) {
    recording = appendFrame(recording, { timeMs: spikeAt + i * 20 });
    assert.equal(recording.step, 'recording');
  }
  recording = appendFrame(recording, { timeMs: spikeAt + 80 });
  assert.equal(recording.step, 'pick');
  assert.ok(recording.frames.length >= 2);
  const atEnd = stepSelection(recording, 99);
  assert.equal(atEnd.selected, atEnd.frames.length - 1);
  const offset = selectedOffsetMs(atEnd);
  const frame = atEnd.frames[atEnd.selected];
  assert.equal(offset, spikeAt - frame.timeMs);
});
