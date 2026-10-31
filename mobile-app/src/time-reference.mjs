// A reference maps one native monotonic clock to UTC. Never persist it across app restarts.
// A network/GNSS estimate corrects the timebase, not the camera/microphone or source output.
export const DEFAULT_NTP_SERVER = 'time.cloudflare.com';
export const CLOCK_MAX_AGE_MS = 300000;
export const CLOCK_DRIFT_PPM = 50; // explicit holdover assumption, not a device accuracy guarantee

export function clockSettings(session = {}) {
 const server = String(session.ntpServer || DEFAULT_NTP_SERVER).trim();
 if (!/^[A-Za-z0-9:.\-]{1,253}$/.test(server) || (server.includes(':') && server.split(':').length < 3)) throw new Error('Enter an NTP hostname or IP address without a URL or port.');
 const limit = Number(session.clockMaxUncertaintyMs ?? 20);
 if (!Number.isFinite(limit) || limit < 1 || limit > 500) throw new Error('Clock uncertainty limit must be 1–500 ms.');
 const emit = key => {
  const text = String(session[key] ?? '').trim();
  if (!text) return null;
  const n = Number(text);
  if (!Number.isFinite(n) || Math.abs(n) > 1000) throw new Error('Source emit offsets must be empty or between −1000 and +1000 ms.');
  return n;
 };
 return { server, maxUncertaintyMs: limit, videoEmitMs: emit('sourceEmitVideoMs'), audioEmitMs: emit('sourceEmitAudioMs'), emitSourceId: typeof session.emitSourceId === 'string' ? session.emitSourceId : null };
}

export function bindReference(sample, nativeNow, jsMono = performance.now()) {
 const finite = key => Number.isFinite(sample?.[key]);
 if (!['ntp', 'gnss'].includes(sample?.source) || !finite('referenceUnixMs') || !finite('referenceMonotonicMs') || !finite('uncertaintyMs') ||
     sample.referenceUnixMs < 1e12 || sample.referenceUnixMs > 4e12 || sample.uncertaintyMs < 0 || sample.uncertaintyMs > 1500 ||
     !sample.clockId || !['ios-host','android-elapsed-realtime'].includes(sample.timebase) ||
     !Number.isFinite(nativeNow?.monotonicMs) || nativeNow.clockId !== sample.clockId || nativeNow.timebase !== sample.timebase) {
  throw new Error('Native clock reference is incomplete or belongs to a different clock.');
 }
 const ageMs = nativeNow.monotonicMs-sample.referenceMonotonicMs;
 if (ageMs < 0 || ageMs > CLOCK_MAX_AGE_MS) throw new Error('Native clock reference is stale.');
 return { ...sample, receivedJsMonoMs: jsMono, ageAtReceiptMs: ageMs, maxAgeMs: CLOCK_MAX_AGE_MS,
  holdoverDriftPpm: CLOCK_DRIFT_PPM, estimated: true, physicalAccuracy: 'unvalidated' };
}

export async function synchronizeClock(native, session, jsMono = () => performance.now()) {
 if (!native?.synchronizeClock || !native?.clockNow) throw new Error('A new native AVsync build is required for clock synchronization.');
 const settings = clockSettings(session);
 const sample = await native.synchronizeClock(settings.server);
 return bindReference(sample, native.clockNow(), jsMono());
}

export function referenceAt(reference, mapping, maxUncertaintyMs = 20) {
 if (!reference || !mapping || mapping.clockId !== reference.clockId || mapping.timebase !== reference.timebase ||
     !Number.isFinite(mapping.monotonicMs)) return { state: 'unavailable' };
 const ageMs = mapping.monotonicMs-reference.referenceMonotonicMs;
 if (ageMs < 0 || ageMs > CLOCK_MAX_AGE_MS) return { state: 'expired', ageMs };
 const uncertaintyMs = reference.uncertaintyMs + ageMs * CLOCK_DRIFT_PPM / 1e6;
 const unixMs = reference.referenceUnixMs + ageMs;
 return { state: uncertaintyMs <= maxUncertaintyMs ? 'estimated' : 'uncertain', ageMs, uncertaintyMs, unixMs };
}

export function clockRadiusText(ms) {
 if (!Number.isFinite(ms) || ms < 0) return 'unknown';
 return ms < 0.1 ? '<0.1 ms' : `±${ms.toFixed(1)} ms`;
}

export function referenceSummary(reference, jsMono = performance.now(), limitMs = 20) {
 if (!reference) return 'Clock unchecked · device UTC estimate only';
 const elapsed = jsMono-reference.receivedJsMonoMs;
 if (elapsed < 0) return 'Clock reference expired · synchronize again';
 const age = reference.ageAtReceiptMs+elapsed;
 const radius = reference.uncertaintyMs+age*CLOCK_DRIFT_PPM/1e6;
 const name = reference.source === 'gnss' ? 'GNSS' : `NTP ${reference.server}`;
 if (age > CLOCK_MAX_AGE_MS) return `${name} · expired · synchronize again`;
 return `${name} · estimated clock ${clockRadiusText(radius)}${radius > limitMs ? ' · above limit' : ''} · ${Math.floor(age/1000)} s old`;
}

export function generatorClockOptions(reference, nativeNow, limitMs = 20) {
 const value = referenceAt(reference, nativeNow, limitMs);
 if (value.state !== 'estimated') return {};
 return { clockUnixMs: String(reference.referenceUnixMs), clockMonotonicMs: String(reference.referenceMonotonicMs),
  clockUncertaintyMs: String(reference.uncertaintyMs), clockSource: reference.source, clockId: reference.clockId, clockLimitMs:String(limitMs) };
}

export function measurementSourceKey(report) {
 const id=report?.audioTimecode?.valid && report.audioTimecode.crcOk ? report.audioTimecode.sourceId : null;
 if (Number.isFinite(id)) return `source:${id}`;
 const matched=report?.signalIdentity?.match(/:(\d+)$/);
 return matched ? `source:${matched[1]}` : report?.signalIdentity || null;
}

export function applyTimeReference(report, reference, settings = clockSettings()) {
 if (!report?.latency) return report;
 const raw = report.latency;
 const mapping = report.clockMapping;
 const estimate = referenceAt(reference, mapping, settings.maxUncertaintyMs);
 const usable = estimate.state === 'estimated' && Number.isFinite(mapping?.deviceUnixMs);
 const correction = usable ? estimate.unixMs-mapping.deviceUnixMs : 0;
 const clock = { state: usable ? 'estimated' : estimate.state, source: usable ? reference.source : 'device-utc',
  correctionMs: usable ? correction : null, uncertaintyMs: usable ? estimate.uncertaintyMs : null,
  ageMs: estimate.ageMs ?? null, utcAtReportMs: usable ? estimate.unixMs : null, roundTripMs: usable && Number.isFinite(reference.roundTripMs) ? reference.roundTripMs : null,
  server: usable ? reference.server : null, reference: usable ? reference : null,
  authenticated: false, captureUncertaintyMs: null, sourceClockUncertaintyMs: null, physicalAccuracy: 'unvalidated' };
 const latency = { ...raw, clock, estimated: true, clockAccuracy: usable ? 'estimated' : 'unknown',
  uncertaintyScope: 'clock only; capture and source timing unknown', flags: [], basis: {} };
 for (const essence of ['video','audio']) {
  // Idempotent: a retained reading is never corrected a second time.
  const original = raw[`raw_${essence}_ms`] ?? raw[`${essence}_ms`];
  if (!Number.isFinite(original)) continue;
  latency[`raw_${essence}_ms`] = original;
  const arrival = original+correction;
  latency[`${essence}_arrival_vs_timestamp_ms`] = arrival;
  const sourceMatches = !settings.emitSourceId || settings.emitSourceId === measurementSourceKey(report);
  const emit = sourceMatches ? settings[`${essence}EmitMs`] : null;
  if (!sourceMatches && !latency.flags.includes('source_emit_offsets_belong_to_another_source')) latency.flags.push('source_emit_offsets_belong_to_another_source');
  latency[`${essence}_source_emit_offset_ms`] = emit;
  latency.basis[essence] = 'relative to source timestamp';
  latency[`${essence}_ms`] = arrival;
  delete latency[`${essence}_latency_abs_ms`];
  delete latency[`${essence}_chain_latency_ms`];
  if (emit !== null && usable) {
   const chain = arrival-emit;
   latency[`${essence}_chain_latency_ms`] = chain;
   latency.basis[essence] = 'emission to capture (estimated)';
   if (chain >= -estimate.uncertaintyMs) {
    latency[`${essence}_latency_abs_ms`] = Math.max(0,chain);
    latency[`${essence}_ms`] = Math.max(0,chain);
    if (chain < 0) latency.flags.push(`${essence}_within_clock_uncertainty`);
   } else {
    latency[`${essence}_ms`] = null;
    latency.flags.push(`${essence}_clock_or_emit_offset_mismatch`);
   }
  }
 }
 return { ...report, latency, clockReference: clock, reference: { type: 'none', phone_clock: clock.source }, accuracy: 'unvalidated' };
}

// A held reading keeps the clock it actually used, even after a later sync.
export function measurementClockLabel(clock) {
 if (!clock || clock.source === 'device-utc') return 'Device UTC · uncorrected';
 const name=clock.source==='gnss' ? 'GNSS' : `NTP ${clock.server || ''}`.trim();
 return `${name} · ${clockRadiusText(clock.uncertaintyMs)} at acquisition`;
}
