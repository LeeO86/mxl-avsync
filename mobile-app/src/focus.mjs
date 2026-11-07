// Tap to focus (docs/phone-capture.md): the native view re-runs its 1.5 s auto step on the
// tapped spot, then locks again. The focus square shows that long, then fades.
export const REFOCUS_MS = 1500;
export const FADE_MS = 400;

// A tap inside the camera box as 0…1 across and down it; null when the box has no size yet.
export function tapPoint(x, y, width, height) {
 if (!(width > 0 && height > 0) || !Number.isFinite(x) || !Number.isFinite(y)) return null;
 const clamp = value => Math.min(1, Math.max(0, value));
 return { x: clamp(x / width), y: clamp(y / height) };
}
