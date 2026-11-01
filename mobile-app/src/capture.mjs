export const CAPTURE_RATES = [60,120,240,0];
export function captureRate(session = {}) {
 return CAPTURE_RATES.includes(session.captureFps) ? session.captureFps : 120;
}
export function captureRateLabel(value) { return value === 0 ? 'Maximum' : `${value} fps`; }
export function captureReportAge(report, receivedMonoMs, nowMonoMs) {
 return Math.max(0,nowMonoMs-receivedMonoMs)+Math.max(0,report?.analysisDelayMs || 0);
}
export function captureStatus(report = {}) {
 const delivered=report.captureStats?.deliveredFps ?? report.fps;
 const rate=Number.isFinite(report.configuredFps) ? `${Math.round(report.configuredFps)} fps camera` : 'Camera starting';
 const processing=Number.isFinite(delivered) ? `${Math.round(delivered)} fps delivered` : 'collecting frames';
 return `${rate} · ${processing}`;
}

export function captureDiagnostics(report = {}, ageMs = Infinity, active = true) {
 const stats=report.captureStats;
 if (!stats) return {lines:['Capture statistics unavailable'],warning:null};
 const ms=value=>Number.isFinite(value) ? `${value.toFixed(1)} ms` : 'unknown';
 const frameMs=Number.isFinite(report.configuredFps) && report.configuredFps>0 ? 1000/report.configuredFps : null;
 const stale=!active || ageMs>1600;
 const events=(report.events || []).filter(e=>e.valid && Number.isFinite(e.videoTimeMs));
 const latest=events.reduce((best,e)=>!best || e.videoTimeMs>best.videoTimeMs ? e : best,null);
 const edge=latest && stats.eventFrameGaps?.find(e=>e.seq===latest.seq && Math.abs(e.eventTimeMs-latest.videoTimeMs)<1);
 const lines=[`${stale ? 'Last report · ' : ''}${captureStatus(report)}`,
  `Frame gaps · p95 ${ms(stats.p95FrameGapMs)} · max ${ms(stats.maxFrameGapMs)} · ${(Math.max(0,stats.windowMs || 0)/1000).toFixed(1)} s window`,
  `Capture callback max · video ${ms(stats.videoProcessingMaxMs)} · audio ${ms(stats.audioProcessingMaxMs)}`,
  `Snapshot copy max ${ms(stats.snapshotProcessingMaxMs)} · analysis max ${ms(stats.pollProcessingMaxMs)}`,
  `Analysis delivery delay ${ms(report.analysisDelayMs)} · ${report.analysisMode || 'mode unreported'}`,
  `AVFoundation drops ${stats.dropCallbacks ?? 'unknown'} · inferred missing ${stats.estimatedMissingFramesFromPts ?? 'unknown'} (overlap; do not add)`];
 if(edge) lines.push(`Latest paired video #${latest.seq} · capture bracket ${ms(edge.frameGapMs)}`);
 const warning=!stale && frameMs && Number.isFinite(stats.maxFrameGapMs) && stats.maxFrameGapMs>frameMs*2.5 ?
  'Capture gaps exceed two frame intervals. Missing camera samples can increase video timing variation; this does not identify WebRTC/display jitter.' : null;
 return {lines,warning};
}
