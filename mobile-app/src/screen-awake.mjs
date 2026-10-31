export function screenAwakeEnabled(session = {}) { return session.keepScreenAwake !== false; }
export function measurementNeedsScreen({enabled,foreground,flow,tab,running,captureAvailable,cameraGranted,micGranted}) {
 return Boolean(enabled && foreground && (flow || (tab === 'measure' && running && captureAvailable && cameraGranted && micGranted)));
}
