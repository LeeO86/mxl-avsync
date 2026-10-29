// Gray: absent; orange: detected; green: decoded/associated identity.
export function signalFeedback(report = {}, ageMs = Infinity, active = true) {
 const empty = { audio: { tone: 'gray', label: 'No chirp detected' }, picture: { tone: 'gray', label: 'No picture detected' } };
 if (!active || ageMs > 1600) return empty;
 const now = report.timeMs;
 const recent = t => Number.isFinite(now) && Number.isFinite(t) && now - t >= -100 && now - t <= 1200;
 const chirps = (report.chirps || []).filter(hit => recent(hit.timeMs));
 const timecode = report.audioTimecode;
 const decoded = timecode?.valid === true && timecode.crcOk === true &&
  (Number.isFinite(timecode.validUntilMs) ? Number.isFinite(now) && now >= timecode.receivedTimeMs - 100 && now <= timecode.validUntilMs : recent(timecode.receivedTimeMs));
 const audio = decoded ? { tone: 'green', label: Number.isFinite(timecode.eventSeq) ? `Audio timecode · #${timecode.eventSeq}` : 'Audio timecode decoded' }
  : chirps.length ? { tone: 'orange', label: chirps.every(hit => hit.quality === 'invalid') ? 'Chirp ambiguous' : 'Chirp detected · no timecode' } : empty.audio;
 const picture = !report.picture?.found ? empty.picture
  : report.picture.codeDecoded ? { tone: 'green', label: `Picture code · #${report.picture.codeSeq ?? report.decodedSeq ?? 'decoded'}` }
  : { tone: 'orange', label: 'Picture detected' };
 return { audio, picture };
}

export function latencyValues(report, ageMs = Infinity, active = true) {
 const value = report?.latency;
 if (!active || ageMs > 1600 || value?.state !== 'estimated') return { video: null, audio: null };
 return { video: Number.isFinite(value.video_ms) ? value.video_ms : null, audio: Number.isFinite(value.audio_ms) ? value.audio_ms : null };
}

export function latencyHint(value) {
 const state = typeof value === 'string' ? value : value?.videoState || value?.state;
 return ({ file_signal: 'File test: no absolute source time', qr_expired: 'Live QR expired — keep its code visible', qr_event_mismatch: 'QR and picture event do not match', waiting_for_video_event: 'Live QR decoded — waiting for a video event', waiting_for_device_clock: 'Capture clock mapping unavailable', unsupported_signal: 'Source format/rate unsupported for latency', estimated: 'Waiting for a fresh event' })[state] || 'Waiting for a matching live QR timestamp';
}
