import { prepareReading } from './workflow.mjs';

function sourceKeys(report) {
 const packet = report?.audioTimecode;
 return { audio: packet?.valid && packet.crcOk && Number.isFinite(packet.sourceId) ? String(packet.sourceId) : null,
  qr: report?.signalIdentity || null };
}
function newestPairTime(report) {
 const times = (report.events || []).filter(e => e.valid && Number.isFinite(e.offset_ms))
  .map(e => Math.max(e.videoTimeMs, e.audioTimeMs)).filter(Number.isFinite);
 return times.length ? Math.max(...times) : Number.isFinite(report.timeMs) ? report.timeMs : null;
}

// Keep the last accepted snapshot through status/loss reports. Never turn a
// repeated report of the same AV pair into a newly acquired measurement.
export function receiveReading(previous, report, calibration, wallMs, monoMs) {
 const keys = sourceKeys(report);
 if (previous && Object.keys(keys).some(k => keys[k] && previous.sourceKeys[k] && keys[k] !== previous.sourceKeys[k])) previous = null;
 if (previous && Object.keys(keys).some(k => keys[k] && !previous.sourceKeys[k])) {
  previous = { ...previous, sourceKeys: { audio: keys.audio || previous.sourceKeys.audio, qr: keys.qr || previous.sourceKeys.qr } };
 }
 if (report?.kind !== 'reading') return previous;
 if (!Number.isFinite(report.offset_ms) || !report.eventCount) return previous;
 const eventTimeMs = newestPairTime(report);
 if (previous && eventTimeMs !== null && previous.eventTimeMs !== null && eventTimeMs <= previous.eventTimeMs) return previous;
 const eventAgeMs = (Number.isFinite(report.timeMs) && eventTimeMs !== null ? Math.max(0, report.timeMs - eventTimeMs) : 0) + Math.max(0, report.analysisDelayMs || 0);
 return { reading: prepareReading(report, calibration, wallMs), sourceKeys: keys,
  receivedMonoMs: monoMs, eventTimeMs, eventAgeMs };
}

export function readingPresentation(snapshot, report, reportAgeMs, monoMs, active = true) {
 if (!snapshot) return { state: 'empty', ageMs: null, label: 'No valid measurement yet' };
 const ageMs = snapshot.eventAgeMs + Math.max(0, monoMs - snapshot.receivedMonoMs);
 const fresh = active && report?.kind === 'reading' && reportAgeMs <= 1600 && ageMs <= 1800;
 const state = !active ? 'paused' : fresh ? 'live' : 'held';
 const age = ageMs < 1000 ? '<1 s' : `${Math.floor(ageMs / 1000)} s`;
 return { state, ageMs, label: state === 'live' ? 'Live measurement' : `${state === 'paused' ? 'Paused · last valid' : 'Last valid reading'} · ${age} ago` };
}

export function savedReading(snapshot, presentation, savedAt) {
 if (!snapshot) return null;
 return { ...snapshot.reading, id: String(savedAt), savedAt,
  measurementAgeMsAtSave: presentation.ageMs, savedDisplayState: presentation.state };
}
