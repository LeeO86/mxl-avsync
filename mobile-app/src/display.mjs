// Display rules for the phone app (SPECIFICATION §12). Pure JS so node can test it
// without an Expo install. Uncalibrated results stay orange and carry no grade colour.

export function gradeColour(grade) {
  if (grade === 'pass') return '#1f8a4c';
  if (grade === 'warn') return '#d9822b';
  if (grade === 'fail') return '#c4392a';
  return '#6b7280';
}

export function describeOffset(ms) {
  if (ms === null || ms === undefined || Number.isNaN(ms)) {
    return { offsetText: '—', sentence: 'no measurement' };
  }
  const sign = ms > 0 ? '+' : '';
  const rounded = Math.round(ms * 10) / 10;
  const mag = Math.round(Math.abs(ms));
  let sentence = 'audio and video in sync';
  if (ms > 0.05) sentence = `audio ${mag} ms after video`;
  else if (ms < -0.05) sentence = `audio ${mag} ms before video`;
  return { offsetText: `${sign}${rounded.toFixed(1)} ms`, sentence };
}

export function calibrationCurrent(record, osVersion, nowMs) {
  if (!record || !record.events || !record.model) return 'missing';
  if (record.os_version !== osVersion) return 'expired';
  const ninety = 90 * 24 * 60 * 60 * 1000;
  if (nowMs - record.calibrated_unix_ms > ninety) return 'expired';
  return 'valid';
}

export function displayMeasurement(report) {
  const calibration = report.calibration || 'missing';
  const showGrade = calibration === 'valid';
  const words = describeOffset(report.offset_ms);
  return {
    ...words,
    showGrade,
    colour: showGrade ? gradeColour(report.grade) : '#d9822b',
    banner: showGrade ? String(report.grade || 'invalid').toUpperCase() : 'UNCALIBRATED',
    calibrationLabel: calibration,
    uncertainty: showGrade && report.uncertainty_ms != null ? `±${report.uncertainty_ms} ms` : 'unknown',
  };
}

export function resultDocument(report) {
  return {
    schema: 'mxl-avsync-result/1',
    test_id: report.test_id || 'PHONE',
    mode: 'LIVE',
    graded_on: 'offset',
    reference: { type: 'none', domain: '', sync_state: 'unknown', accuracy_ns: 'unknown' },
    calibration: report.calibration || 'missing',
    show_grade_colour: report.calibration === 'valid',
    events: report.events || [],
  };
}
