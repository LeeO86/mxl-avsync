// Keep appearance independent of source/timecode identity and capture calibration.
const label = (value, max) => Array.from(typeof value === 'string' ? value.replace(/\s+/gu, ' ').trim() : '').slice(0, max).join('');
export function generatorOptions(session) {
 const id = typeof session.generatorImageId === 'string' && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/iu.test(session.generatorImageId) ? session.generatorImageId : '';
 return { name: label(session.generatorName, 64), subtitle: label(session.generatorSubtitle, 128), imageId: id };
}
