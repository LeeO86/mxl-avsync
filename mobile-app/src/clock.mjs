export const CLOCK_URL = 'https://worldtime.timezone.io/api/timezone/Etc/UTC';

export function clockSample(body, startMs, endMs, elapsedMs, ageHeader = null) {
 if (![startMs, endMs, elapsedMs].every(Number.isFinite) || elapsedMs < 0 || elapsedMs > 5000 || Math.abs(endMs - startMs - elapsedMs) > 50) throw new Error('Phone clock changed or request was too slow.');
 if (ageHeader !== null && (!Number.isFinite(Number(ageHeader)) || Number(ageHeader) > 0)) throw new Error('Cached time response rejected.');
 let serverMs = Date.parse(String(body.utc_datetime).replace(/(\.\d{3})\d+/, '$1')), resolutionMs = 1;
 if (!Number.isFinite(serverMs) && Number.isFinite(body.unixtime)) { serverMs = body.unixtime * 1000; resolutionMs = 1000; }
 if (!Number.isFinite(serverMs) || serverMs < 1e12 || serverMs > 4e12) throw new Error('No usable UTC timestamp returned.');
 // The timestamp was generated somewhere inside this request. No symmetric-path assumption.
 return { lowerMs: serverMs - endMs - 1, upperMs: serverMs + resolutionMs - startMs, roundTripMs: elapsedMs };
}

export async function checkClock({ fetchImpl = fetch, wallNow = Date.now, monoNow = () => performance.now() } = {}) {
 const samples = []; let lastError;
 for (let i = 0; i < 3; i++) {
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 5000);
  try {
   const startMs = wallNow(), startMono = monoNow();
   const response = await fetchImpl(CLOCK_URL, { signal: controller.signal, cache: 'no-store', headers: { 'Cache-Control': 'no-cache' } });
   if (!response.ok) throw new Error(`Time service returned HTTP ${response.status}.`);
   const body = await response.json(), endMs = wallNow(), endMono = monoNow();
   samples.push(clockSample(body, startMs, endMs, endMono - startMono, response.headers.get('age')));
  } catch (error) { lastError = error; } finally { clearTimeout(timer); }
 }
 if (!samples.length) throw lastError || new Error('Time service unavailable.');
 const lowerMs = Math.max(...samples.map(s => s.lowerMs)), upperMs = Math.min(...samples.map(s => s.upperMs));
 if (lowerMs > upperMs) throw new Error('Time responses disagree; clock check inconclusive.');
 return { lowerMs, upperMs, roundTripMs: Math.min(...samples.map(s => s.roundTripMs)), samples: samples.length, checkedAt: wallNow(), checkedMono: monoNow(), provider: 'worldtime.timezone.io', coarseOnly: true };
}

export function clockSummary(check, wallMs = Date.now(), monoMs = performance.now()) {
 if (!check) return 'Phone clock unchecked · accuracy unknown';
 const elapsed = monoMs - check.checkedMono;
 if (elapsed < 0 || elapsed > 300000 || Math.abs(wallMs - check.checkedAt - elapsed) > 100) return 'Clock check expired · check again';
 const gross = check.lowerMs > 1000 || check.upperMs < -1000;
 const signed = n => `${n >= 0 ? '+' : ''}${Math.round(n)}`;
 return `${gross ? 'Large clock mismatch' : 'Coarse clock check'}: API−phone ${signed(check.lowerMs)}…${signed(check.upperMs)} ms · RTT ${Math.round(check.roundTripMs)} ms`;
}
