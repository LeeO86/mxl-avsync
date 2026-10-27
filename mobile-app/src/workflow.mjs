export function validCalibration(record, os, now) {
  if (!record || record.capturePath !== 'native-raw-v1' || !Number.isFinite(record.offset_ms) || record.os !== os || now < record.savedAt || now - record.savedAt > 90 * 86400000) return null;
  return record;
}

export function nearestFrame(frames, time) {
  return frames.reduce((best, frame, i) => Math.abs(frame.timeMs - time) < Math.abs(frames[best].timeMs - time) ? i : best, 0);
}

export function makeCalibration(take, selected, os, now) {
  const frame = take.frames[selected];
  if (!frame || !Number.isFinite(take.spikeTimeMs)) throw new Error('A real video frame and sound are required.');
  return { offset_ms: take.spikeTimeMs - frame.timeMs, savedAt: now, os, method: 'manual-contact-raw', capturePath: 'native-raw-v1', fps: take.fps, uncertainty_ms: null };
}

export function prepareReading(report, calibration, now) {
  if (!Number.isFinite(report.offset_ms) || !report.eventCount) throw new Error('No paired AV events were detected.');
  return { ...report, id: String(now), at: now, raw_offset_ms: report.offset_ms, offset_ms: report.offset_ms - (calibration?.offset_ms || 0), calibrated: Boolean(calibration), calibrationApplied: calibration, uncertainty_ms: null };
}
