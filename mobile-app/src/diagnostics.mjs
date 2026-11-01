// Explicit metadata allowlist: calibration media, waveform bins, image paths
// and unrelated persisted state never enter the diagnostic share sheet.
const fields=['kind','stage','timeMs','diagnosticVersion','signalIdentity','requestedFps','configuredFps','fps',
 'captureStats','captureContext','analysisMode','analysisDelayMs','clockMapping','clockReference','latency',
 'audioPeakDbfs','audioHits','audioSamples','audioTimecode','chirps','events','offset_ms','raw_offset_ms',
 'eventCount','spread_ms','quality','bestCorrelation','videoEvents','lockedSeq','decodedSeq','validFrames',
 'locatedFrames','recentValidFrames','recentLocatedFrames','lastValidFrameAgeMs','lastVideoEventAgeMs',
 'sequenceRejectedFrames','phaseRejectedFrames','timingResets','calibrated','calibrationApplied','at','savedAt'];
function reportMetadata(report) {
 if(!report) return null;
 const result=Object.fromEntries(fields.filter(key=>report[key]!==undefined).map(key=>[key,report[key]]));
 if(report.picture) result.picture=Object.fromEntries(['found','points','width','height','codeDecoded','codeSeq'].filter(key=>report.picture[key]!==undefined).map(key=>[key,report.picture[key]]));
 return result;
}
export function diagnosticSnapshot({report,ageMs,active,reference,reading,readingAgeMs,exportedAt=Date.now()}) {
 return {kind:'avsync-diagnostic-snapshot',schemaVersion:1,exportedAt,
  currentReport:reportMetadata(report),currentReportAgeMs:ageMs,captureActive:active,
  currentClockReference:reference,lastAcceptedReading:reportMetadata(reading),lastAcceptedReadingAgeMs:readingAgeMs,
  notes:'Current capture and clock state are separate from the immutable last accepted reading. Drop counters overlap. No raw camera or microphone recording included.'};
}
