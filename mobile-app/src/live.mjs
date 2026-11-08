import { referenceAt } from './time-reference.mjs';

// What the measure screen shows, from the latest native report (docs/mobile-native.md).
// Reports arrive about once a second; a reading stays visible, greyed, for HOLD_MS.
export const STALE_MS = 3000;
export const HOLD_MS = 8000;

const MESSAGES = {
 starting: 'Starting camera…',
 no_signal: 'Point the camera at the test signal',
 no_events: 'Test signal found · hold the phone steady',
 no_audio: 'Picture found · waiting for the test sound',
 pairing: 'Measuring…',
 measuring: '',
};

// Device-UTC latency plus the NTP correction at the report time; null without a usable clock.
export function clockCorrection(report, reference, maxUncertaintyMs = 20) {
 const mapping = report?.clockMapping;
 const estimate = referenceAt(reference, mapping, maxUncertaintyMs);
 if (estimate.state !== 'estimated' || !Number.isFinite(mapping?.deviceUnixMs)) return { state: estimate.state, correctionMs: null, uncertaintyMs: null };
 return { state: 'estimated', correctionMs: estimate.unixMs - mapping.deviceUnixMs, uncertaintyMs: estimate.uncertaintyMs };
}

export function liveView(report, { reference = null, maxUncertaintyMs = 20, calibrationMs = 0, ageMs = 0, held = null } = {}) {
 if (!report || report.kind !== 'status' || ageMs > STALE_MS) {
  return { stage: 'starting', message: report && ageMs > STALE_MS ? 'Waiting for the camera…' : MESSAGES.starting, reading: holdReading(held, ageMs) };
 }
 const stage = MESSAGES[report.stage] !== undefined ? report.stage : 'pairing';
 const clock = clockCorrection(report, reference, maxUncertaintyMs);
 let reading = null;
 if (report.reading && Number.isFinite(report.reading.offsetMs)) {
  const r = report.reading;
  const latency = value => clock.correctionMs === null || !Number.isFinite(value) ? null : value + clock.correctionMs;
  // Native offsetMs has the sender calibration (AVS4 QR) subtracted, rawOffsetMs is as measured;
  // this phone's analyser calibration is subtracted here (the native session does not apply it).
  reading = {
   offsetMs: r.offsetMs - calibrationMs,
   rawOffsetMs: Number.isFinite(r.rawOffsetMs) ? r.rawOffsetMs : r.offsetMs,
   senderCalibrationMs: Number.isFinite(r.senderCalibrationMs) ? r.senderCalibrationMs : null,
   spreadMs: r.spreadMs,
   events: r.events,
   videoLatencyMs: latency(r.videoLatencyMs),
   audioLatencyMs: latency(r.audioLatencyMs),
   videoSpreadMs: r.videoSpreadMs ?? null,
   audioSpreadMs: r.audioSpreadMs ?? null,
   clockUncertaintyMs: clock.uncertaintyMs,
   live: Boolean(report.signal?.live),
   sourceId: report.signal?.sourceId ?? null,
   testId: report.signal?.testId ?? null,
   title: report.signal?.name || null,
   atMs: Date.now(),
   fresh: true,
  };
 }
 return { stage, message: MESSAGES[stage], clock: clock.state, reading: reading || holdReading(held, ageMs) };
}

// Keep the last reading on screen for a moment when the signal drops, marked as held.
function holdReading(held, ageMs = 0) {
 if (!held) return null;
 const age = Date.now() - held.atMs;
 return age <= HOLD_MS ? { ...held, fresh: false, heldForMs: age } : null;
}

export function offsetText(ms) {
 if (!Number.isFinite(ms)) return '—';
 const sign = ms > 0.05 ? '+' : ms < -0.05 ? '−' : '±';
 return `${sign}${Math.abs(ms).toFixed(1)} ms`;
}

// Positive: audio later than video.
export function offsetSentence(ms) {
 if (!Number.isFinite(ms)) return '';
 if (Math.abs(ms) < 0.5) return 'Audio and video in sync';
 return ms > 0 ? `Audio ${Math.abs(ms).toFixed(1)} ms after video` : `Audio ${Math.abs(ms).toFixed(1)} ms before video`;
}

// Raw value and sender calibration next to a corrected result; '' without a sender calibration.
export function senderCalibrationText(result) {
 if (!Number.isFinite(result?.senderCalibrationMs)) return '';
 return `raw ${offsetText(result.rawOffsetMs)} · sender calibration ${offsetText(result.senderCalibrationMs)}`;
}

export function latencyText(ms) {
 return Number.isFinite(ms) ? `${ms.toFixed(1)} ms` : '—';
}

// The user sees the NTP check's own accuracy and its age; the growing holdover allowance stays internal.
export function clockText(estimate) {
 const s = Math.max(0, Math.round(estimate.ageMs / 1000));
 return `NTP ±${estimate.sampleUncertaintyMs.toFixed(1)} ms · checked ${s < 60 ? `${s} s` : `${Math.floor(s / 60)} min`} ago`;
}

export function savedResult(reading, { calibration = null, captureContext = null, now = Date.now() } = {}) {
 if (!reading) return null;
 return {
  id: String(now), at: now,
  offsetMs: reading.offsetMs, rawOffsetMs: reading.rawOffsetMs, spreadMs: reading.spreadMs, events: reading.events,
  videoLatencyMs: reading.videoLatencyMs, audioLatencyMs: reading.audioLatencyMs, clockUncertaintyMs: reading.clockUncertaintyMs,
  calibrationMs: calibration?.offset_ms ?? null, senderCalibrationMs: reading.senderCalibrationMs ?? null,
  held: !reading.fresh, sourceId: reading.sourceId, testId: reading.testId ?? null, title: reading.title ?? null, captureContext,
 };
}

export function formatWhen(ms) {
 const d = new Date(ms);
 const pad = n => String(n).padStart(2, '0');
 return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
}
