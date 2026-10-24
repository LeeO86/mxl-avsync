import test from 'node:test';
import assert from 'node:assert/strict';
import { calibrationCurrent, displayMeasurement, resultDocument } from './display.mjs';

test('uncalibrated measurement is orange and has no grade colour', () => {
  const view = displayMeasurement({ calibration: 'missing', grade: 'fail', offset_ms: 35 });
  assert.equal(view.showGrade, false);
  assert.equal(view.colour, '#d9822b');
  assert.equal(view.banner, 'UNCALIBRATED');
  assert.equal(view.sentence, 'audio 35 ms after video');
  assert.equal(view.offsetText.startsWith('+'), true);
});

test('a valid calibration shows the grade colour', () => {
  const view = displayMeasurement({ calibration: 'valid', grade: 'pass', offset_ms: 0, uncertainty_ms: 0.4 });
  assert.equal(view.showGrade, true);
  assert.equal(view.colour, '#1f8a4c');
  assert.equal(view.banner, 'PASS');
  assert.equal(view.uncertainty, '±0.4 ms');
});

test('calibration expires after 90 days or an OS change', () => {
  const record = { model: 'phone', os_version: '17.0', events: 10, calibrated_unix_ms: 0 };
  const day = 24 * 60 * 60 * 1000;
  assert.equal(calibrationCurrent(record, '17.0', 10 * day), 'valid');
  assert.equal(calibrationCurrent(record, '17.0', 91 * day), 'expired');
  assert.equal(calibrationCurrent(record, '18.0', day), 'expired');
  assert.equal(calibrationCurrent(null, '17.0', 0), 'missing');
});

test('export is a live result with no clock reference', () => {
  const doc = resultDocument({ test_id: 'PHONE', calibration: 'missing', events: [{ event_id: 'EVT-000001' }] });
  assert.equal(doc.mode, 'LIVE');
  assert.equal(doc.reference.type, 'none');
  assert.equal(doc.show_grade_colour, false);
  assert.equal(doc.schema, 'mxl-avsync-result/1');
});
