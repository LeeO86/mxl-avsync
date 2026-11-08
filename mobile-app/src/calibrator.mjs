// The calibrator board wizard (docs/calibration.md, docs/calibrator.md). Pure logic, no React.
const DAY_MS = 86400000;
const AVS4_EPOCH_MS = Date.UTC(2020, 0, 1); // AVS4 calibration day 0
export const STEPS = ['plug', 'camera', 'display'];

// "+12.3 ms ± 0.4"
export function calibratorText(offsetMs, uncertaintyMs) {
  if (!Number.isFinite(offsetMs)) return '—';
  const sign = offsetMs > 0 ? '+' : offsetMs < 0 ? '−' : '±';
  const spread = Number.isFinite(uncertaintyMs) ? ` ± ${Math.max(0.1, uncertaintyMs).toFixed(1)}` : '';
  return `${offsetMs === 0 ? '' : sign}${Math.abs(offsetMs).toFixed(1)} ms${spread}`;
}

// Progress of a take: pairs found so far, 0..1 for the ring.
export function takeProgress(report) {
  const wanted = report?.wanted > 0 ? report.wanted : 20;
  const pairs = Math.max(0, Math.min(wanted, Number(report?.pairs) || 0));
  return { pairs, wanted, share: pairs / wanted };
}

// Step 1 result (camera mode): this phone's analyser calibration. Same record and validity
// rules as the clap calibration (workflow.mjs), with the pulse spread as uncertainty.
export function makePulseCalibration(report, os, now) {
  if (report?.kind !== 'calibrator' || report.mode !== 'camera' || !Number.isFinite(report.offsetMs)) throw new Error('No calibrator result.');
  return { offset_ms: report.offsetMs, savedAt: now, os, method: 'calibrator-pulse', capturePath: 'native-raw-v1', fps: report.fps ?? null,
    captureContext: report.captureContext || null, uncertainty_ms: Number.isFinite(report.uncertaintyMs) ? report.uncertaintyMs : null, pulses: report.pairs ?? null };
}

// Step 2 result (display mode on this phone): the sender calibration the generator sends in the
// AVS4 QR. Method "transponder": the calibrator answers each screen swap with a click
// (SPECIFICATION AVS4; "pulse-train" is the camera mode's flash train, not a sender method here).
export function makeSenderCalibration(result, os, now) {
  const c = result?.calibration;
  if (!c || c.ok === false || !Number.isFinite(c.offsetMs)) throw new Error(result?.calibrationError || 'No calibration result.');
  return { offset_ms: c.offsetMs, uncertainty_ms: Number.isFinite(c.uncertaintyMs) ? c.uncertaintyMs : null, savedAt: now, os,
    method: 'transponder', audioRoute: c.audioRoute ?? null, pairs: c.pairs ?? null, directionMs: c.directionMs ?? null, outputLatencyMs: c.outputLatencyMs ?? null };
}

// Valid for this OS version for 90 days, like the analyser calibration.
export function validSenderCalibration(record, os, now) {
  if (!record || record.method !== 'transponder' || !Number.isFinite(record.offset_ms) || !Number.isFinite(record.savedAt) || record.os !== os || now < record.savedAt || now - record.savedAt > 90 * DAY_MS) return null;
  return record;
}

// Generator options (strings) for a valid sender calibration; {} otherwise (the QR says "not calibrated").
// AVS4 steps: offset 0.1 ms, uncertainty 0.5 ms (rounded up, at least 0.5), day since 2020-01-01.
export function senderCalibrationOptions(record, os, now) {
  const valid = validSenderCalibration(record, os, now);
  if (!valid || Math.abs(valid.offset_ms) > 3276) return {};
  const uncertainty = Math.min(127.5, Math.max(0.5, Math.ceil((valid.uncertainty_ms ?? 0) * 2) / 2));
  const options = { senderCalibrationMethod: valid.method, senderCalibrationOffsetMs: valid.offset_ms.toFixed(1),
    senderCalibrationUncertaintyMs: uncertainty.toFixed(1), senderCalibrationDay: String(Math.max(0, Math.floor((valid.savedAt - AVS4_EPOCH_MS) / DAY_MS))) };
  if (valid.audioRoute) options.senderCalibrationRoute = valid.audioRoute;
  return options;
}

// One line for Settings.
export function senderCalibrationLabel(record, os, now, when) {
  const valid = validSenderCalibration(record, os, now);
  if (valid) return `${calibratorText(valid.offset_ms, valid.uncertainty_ms)} · ${when(valid.savedAt)}`;
  return record ? 'Not valid any more' : 'Not calibrated';
}
