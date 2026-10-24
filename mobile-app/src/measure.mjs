// Phone measure screen and calibration take. Pure JS so node can test it
// without a device. av_offset_ms stays audio time minus video time:
// positive means the mic timestamp is later (audio late).

const IN_SYNC_MS = 0.05;

export function measureSentence(ms) {
  if (ms === null || ms === undefined || Number.isNaN(ms)) return 'no measurement';
  if (Math.abs(ms) < IN_SYNC_MS) return 'audio is in sync';
  const mag = Math.round(Math.abs(ms));
  return ms > 0 ? `audio is ${mag} ms late` : `audio is ${mag} ms early`;
}

export function offsetText(ms) {
  if (ms === null || ms === undefined || Number.isNaN(ms)) return '—';
  const sign = ms > 0 ? '+' : '';
  const rounded = Math.round(ms * 10) / 10;
  return `${sign}${rounded.toFixed(1)} ms`;
}

export function statusLine({ quality, calibrated, uncertaintyMs }) {
  const qualityText = quality || '—';
  const calibrationText = calibrated ? 'set' : 'missing';
  const uncertaintyText = uncertaintyMs == null || Number.isNaN(uncertaintyMs)
    ? 'unknown'
    : `±${uncertaintyMs} ms`;
  return `Quality: ${qualityText} · Calibration: ${calibrationText} · Uncertainty: ${uncertaintyText}`;
}

export function formatResultWhen(unixMs) {
  const date = new Date(unixMs);
  const pad = (n) => String(n).padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

// Saved offset: audio spike time minus the frame the operator says matches it.
export function calibrationOffsetMs(audioTimeMs, frameTimeMs) {
  return audioTimeMs - frameTimeMs;
}

export function shownOffset(rawOffsetMs, calibration) {
  if (rawOffsetMs === null || rawOffsetMs === undefined || Number.isNaN(rawOffsetMs)) return null;
  if (!calibration || calibration.offset_ms === null || calibration.offset_ms === undefined) return rawOffsetMs;
  return rawOffsetMs - calibration.offset_ms;
}

// Highest frame rate the device lists. Tie goes to the larger picture.
export function selectHighestFrameRate(formats) {
  if (!formats || formats.length === 0) return null;
  const scored = formats.map((format) => ({
    format,
    fps: Number(format.maxFps ?? format.fps ?? format.max ?? 0),
    width: Number(format.width ?? 0),
  }));
  scored.sort((a, b) => (b.fps - a.fps) || (b.width - a.width));
  return scored[0].format;
}

export function detectSpike(samples, sampleRate, options = {}) {
  const thresholdRatio = options.thresholdRatio ?? 8;
  const minAbs = options.minAbs ?? 0.2;
  if (!samples || samples.length < 8 || !sampleRate) return null;
  const abs = new Array(samples.length);
  for (let i = 0; i < samples.length; i += 1) abs[i] = Math.abs(samples[i]);
  const sorted = abs.slice().sort((a, b) => a - b);
  const median = sorted[Math.floor(sorted.length / 2)] || 0;
  let peak = 0;
  let index = -1;
  for (let i = 0; i < abs.length; i += 1) {
    if (abs[i] > peak) {
      peak = abs[i];
      index = i;
    }
  }
  const floor = Math.max(median, 1e-4);
  if (index < 0 || peak < minAbs || peak < floor * thresholdRatio) return null;
  return { index, timeMs: (index / sampleRate) * 1000, peak };
}

export function waveformPeaks(samples, buckets = 48) {
  if (!samples || samples.length === 0 || buckets < 1) return [];
  const size = Math.ceil(samples.length / buckets);
  const peaks = [];
  for (let i = 0; i < buckets; i += 1) {
    let peak = 0;
    const start = i * size;
    const end = Math.min(samples.length, start + size);
    if (start >= samples.length) break;
    for (let j = start; j < end; j += 1) peak = Math.max(peak, Math.abs(samples[j]));
    peaks.push(peak);
  }
  return peaks;
}

export function spikeBucket(spikeIndex, sampleCount, buckets) {
  if (!sampleCount || buckets < 1) return 0;
  return Math.min(buckets - 1, Math.max(0, Math.floor((spikeIndex / sampleCount) * buckets)));
}

export function framesAround(frames, spikeTimeMs, beforeMs = 250, afterMs = 250) {
  return frames
    .filter((frame) => frame.timeMs >= spikeTimeMs - beforeMs && frame.timeMs <= spikeTimeMs + afterMs)
    .slice()
    .sort((a, b) => a.timeMs - b.timeMs);
}

export function nearestFrameIndex(frames, timeMs) {
  if (!frames.length) return 0;
  let best = 0;
  let bestAbs = Infinity;
  frames.forEach((frame, index) => {
    const distance = Math.abs(frame.timeMs - timeMs);
    if (distance < bestAbs) {
      bestAbs = distance;
      best = index;
    }
  });
  return best;
}

export function stepIndex(index, delta, length) {
  if (length <= 0) return 0;
  return Math.max(0, Math.min(length - 1, index + delta));
}

export function encodeWav(samples, sampleRate) {
  const count = samples.length;
  const buffer = new ArrayBuffer(44 + count * 2);
  const view = new DataView(buffer);
  const write = (offset, text) => {
    for (let i = 0; i < text.length; i += 1) view.setUint8(offset + i, text.charCodeAt(i));
  };
  write(0, 'RIFF');
  view.setUint32(4, 36 + count * 2, true);
  write(8, 'WAVE');
  write(12, 'fmt ');
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, 1, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * 2, true);
  view.setUint16(32, 2, true);
  view.setUint16(34, 16, true);
  write(36, 'data');
  view.setUint32(40, count * 2, true);
  for (let i = 0; i < count; i += 1) {
    const clamped = Math.max(-1, Math.min(1, samples[i]));
    view.setInt16(44 + i * 2, Math.round(clamped * 32767), true);
  }
  return new Uint8Array(buffer);
}

export const AIM_TEXT = 'Aim the camera at something that makes a sharp sound: a marble on a table, a clap, or an LED and a piezo click.';
