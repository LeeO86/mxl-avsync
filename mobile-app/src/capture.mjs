export const CAPTURE_RATES = [60,120,240,0];
export function captureRate(session = {}) {
 return CAPTURE_RATES.includes(session.captureFps) ? session.captureFps : 120;
}
export function captureRateLabel(value) { return value === 0 ? 'Maximum' : `${value} fps`; }
export function captureStatus(report = {}) {
 const delivered=report.captureStats?.deliveredFps ?? report.fps;
 const rate=Number.isFinite(report.configuredFps) ? `${Math.round(report.configuredFps)} fps camera` : 'Camera starting';
 const processing=Number.isFinite(delivered) ? `${Math.round(delivered)} fps analysed` : 'collecting frames';
 return `${rate} · ${processing}`;
}
