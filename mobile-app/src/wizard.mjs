// One calibration take. Recording stops after the spike and a few frames
// that follow it, so the operator can pick a picture from before and after.

import { calibrationOffsetMs, detectSpike, framesAround, nearestFrameIndex, stepIndex } from './measure.mjs';

export const FRAMES_AFTER_SPIKE = 4;

export function createRecording(sampleRate, audioOriginMs) {
  return {
    step: 'recording',
    sampleRate,
    audioOriginMs,
    samples: [],
    frames: [],
    spike: null,
    selected: 0,
  };
}

export function spikeAbsoluteMs(recording) {
  if (!recording.spike) return null;
  return recording.audioOriginMs + recording.spike.timeMs;
}

export function appendAudio(recording, chunk) {
  if (recording.step !== 'recording') return recording;
  const samples = recording.samples.concat(Array.from(chunk));
  if (recording.spike) return { ...recording, samples };
  const spike = detectSpike(samples, recording.sampleRate);
  return { ...recording, samples, spike };
}

export function appendFrame(recording, frame) {
  const frames = recording.frames.concat(frame);
  if (recording.step !== 'recording' || !recording.spike) return { ...recording, frames };
  const spikeAt = spikeAbsoluteMs({ ...recording, frames });
  const after = frames.filter((item) => item.timeMs >= spikeAt).length;
  if (after < FRAMES_AFTER_SPIKE) return { ...recording, frames };
  const window = framesAround(frames, spikeAt);
  return {
    ...recording,
    step: 'pick',
    frames: window,
    selected: nearestFrameIndex(window, spikeAt),
  };
}

export function stepSelection(recording, delta) {
  if (recording.step !== 'pick') return recording;
  return { ...recording, selected: stepIndex(recording.selected, delta, recording.frames.length) };
}

export function selectedOffsetMs(recording) {
  const frame = recording.frames[recording.selected];
  const audioAt = spikeAbsoluteMs(recording);
  if (!frame || audioAt === null) return null;
  return calibrationOffsetMs(audioAt, frame.timeMs);
}
