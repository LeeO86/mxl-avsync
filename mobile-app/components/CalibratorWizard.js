import { useEffect, useRef, useState } from 'react';
import { Animated, Easing, ScrollView, Text, View } from 'react-native';
import { Button, colors, small, text } from './ui';
import { calibratorText, takeProgress } from '../src/calibrator.mjs';

// The calibrator board wizard (docs/calibration.md): plug in, camera, screen.
// It only shows the step; App starts the takes and saves the results.

const board = { width: 66, height: 22 }; // mm, drawn at 1.4 px per mm
const BOARD_W = board.width * 1.4, BOARD_H = board.height * 1.4;

function useLoop(duration, delay = 0) {
 const value = useRef(new Animated.Value(0)).current;
 useEffect(() => {
  const loop = Animated.loop(Animated.sequence([
   Animated.timing(value, { toValue: 1, duration, easing: Easing.inOut(Easing.quad), useNativeDriver: true }),
   Animated.delay(delay),
   Animated.timing(value, { toValue: 0, duration: 0, useNativeDriver: true }),
  ]));
  loop.start();
  return () => loop.stop();
 }, [value, duration, delay]);
 return value;
}

function useSwap(ms = 500) {
 const [white, setWhite] = useState(true);
 useEffect(() => { const t = setInterval(() => setWhite(value => !value), ms); return () => clearInterval(t); }, [ms]);
 return white;
}

function Phone({ children, back }) {
 return <View style={{ width: 96, height: 190, borderRadius: 16, borderWidth: 3, borderColor: colors.muted, backgroundColor: back ? colors.card : '#000', overflow: 'hidden' }}>{children}</View>;
}

function Board({ style, led }) {
 return <Animated.View style={[{ width: BOARD_W, height: BOARD_H, borderRadius: 3, backgroundColor: '#111', borderWidth: 1, borderColor: '#c9a227', justifyContent: 'center' }, style]}>
  <View style={{ position: 'absolute', left: 4, top: BOARD_H / 2 - 4, width: 8, height: 8, borderRadius: 4, backgroundColor: '#eee' }} />
  {led ? <Animated.View style={{ position: 'absolute', left: 0, top: BOARD_H / 2 - 8, width: 16, height: 16, borderRadius: 8, backgroundColor: '#fff', opacity: led }} /> : null}
  <View style={{ position: 'absolute', left: 0, right: 0, top: BOARD_H / 2, height: 1, backgroundColor: '#ffffff66' }} />
 </Animated.View>;
}

// Step 0: a cable grows from the phone's port to the board.
function PlugAnimation() {
 const t = useLoop(1400, 900);
 return <View style={{ height: 210, flexDirection: 'row', alignItems: 'flex-end', justifyContent: 'center', gap: 28 }}>
  <Phone />
  <View style={{ alignItems: 'center', marginBottom: 10 }}>
   <Board led={t.interpolate({ inputRange: [0, 0.85, 0.9, 1], outputRange: [0, 0, 1, 0] })} />
  </View>
  <Animated.View style={{ position: 'absolute', bottom: 6, left: '50%', marginLeft: -60, width: 76, height: 3, backgroundColor: colors.accent,
   transform: [{ translateX: t.interpolate({ inputRange: [0, 1], outputRange: [-38, 0] }) }, { scaleX: t }] }} />
 </View>;
}

// Step 1: the board comes down LED first onto the rear camera; the LED flashes.
function CameraAnimation() {
 const t = useLoop(1600, 700);
 return <View style={{ height: 210, alignItems: 'center', justifyContent: 'center' }}>
  <Phone back>
   <View style={{ position: 'absolute', left: 8, top: 8, width: 38, height: 38, borderRadius: 9, backgroundColor: colors.line }}>
    <View style={{ position: 'absolute', left: 4, top: 4, width: 14, height: 14, borderRadius: 7, backgroundColor: '#000' }} />
   </View>
  </Phone>
  <Board led={t.interpolate({ inputRange: [0, 0.7, 0.75, 0.85, 1], outputRange: [0, 0, 1, 0, 0] })}
   style={{ position: 'absolute', top: 18, left: '50%', marginLeft: -48 + 4,
    transform: [{ translateY: t.interpolate({ inputRange: [0, 0.6, 1], outputRange: [-70, 0, 0] }) }], opacity: t.interpolate({ inputRange: [0, 0.2, 1], outputRange: [0.2, 1, 1] }) }} />
 </View>;
}

// Step 2: the board turns over and lies on the outline across the swapping halves.
function DisplayAnimation() {
 const t = useLoop(1800, 1200), white = useSwap();
 return <View style={{ height: 210, alignItems: 'center', justifyContent: 'center' }}>
  <Phone>
   <View style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: 100, flexDirection: 'row' }}>
    <View style={{ flex: 1, backgroundColor: white ? '#fff' : '#000' }} />
    <View style={{ flex: 1, backgroundColor: white ? '#000' : '#fff' }} />
   </View>
   <View style={{ position: 'absolute', bottom: 8, left: 48 - BOARD_H / 2, width: BOARD_H, height: 90, borderWidth: 1.5, borderColor: '#ff453a', borderRadius: 3 }} />
  </Phone>
  <Board style={{ position: 'absolute', bottom: 34, left: '50%', marginLeft: -BOARD_W / 2,
   transform: [{ translateY: t.interpolate({ inputRange: [0, 0.4, 0.75, 1], outputRange: [-110, -110, 0, 0] }) },
    { rotate: '90deg' }, { scaleY: t.interpolate({ inputRange: [0, 0.4, 1], outputRange: [-1, 1, 1] }) }],
   opacity: t.interpolate({ inputRange: [0, 0.1, 0.75, 1], outputRange: [0.3, 1, 0.85, 0.85] }) }} />
 </View>;
}

// Progress: one dot per wanted pulse or swap around a circle.
function Ring({ progress, label }) {
 const { pairs, wanted } = takeProgress(progress);
 const r = 72;
 return <View style={{ height: 210, alignItems: 'center', justifyContent: 'center' }}>
  {Array.from({ length: wanted }, (_, i) => {
   const a = (i / wanted) * 2 * Math.PI - Math.PI / 2;
   return <View key={i} style={{ position: 'absolute', width: 12, height: 12, borderRadius: 6, left: '50%', top: 105 - 6 + r * Math.sin(a),
    marginLeft: -6 + r * Math.cos(a), backgroundColor: i < pairs ? colors.accent : colors.line }} />;
  })}
  <Text style={{ ...text, fontSize: 28 }}>{pairs}</Text>
  <Text style={small}>{label}</Text>
 </View>;
}

const STEPS = {
 plug: ['Plug in the calibrator', 'Connect the calibrator to this phone with a USB-C cable. The phone powers it; it has no battery.', PlugAnimation],
 camera: ['Camera and microphone', 'Lay the calibrator LED-down on the main rear camera, with the buzzer next to the microphone. Put the phone screen-down on the table and keep it still. After a second the calibrator flashes and clicks.', CameraAnimation],
 display: ['Screen and speaker', 'Turn the calibrator over and lay it on the red outline: the line between its two sensors on the line between black and white. Turn the volume up.', DisplayAnimation],
 tv: ['TV or monitor', 'Show the calibration pattern of the container generator on the screen (web UI → Signal → Picture). Lay the calibrator on its centre line and keep this phone close to the screen’s speaker.', DisplayAnimation],
};

export default function CalibratorWizard({ step, progress, result, error, onNext, onStart, onSave, onRepeat, onClose }) {
 const base = step.replace(/-(run|done)$/, '');
 const [title, body, Illustration] = STEPS[base];
 const running = step.endsWith('-run'), done = step.endsWith('-done');
 const label = base === 'camera' ? `Camera vs mic: ${calibratorText(result?.offsetMs, result?.uncertaintyMs)}` : `Screen vs speaker: ${calibratorText(result?.offsetMs, result?.uncertaintyMs)}`;
 return <ScrollView contentContainerStyle={{ padding: 20 }}>
  <Text style={small}>{base === 'tv' ? 'With calibrator' : `With calibrator · step ${['plug', 'camera', 'display'].indexOf(base) + 1} of 3`}</Text>
  <Text accessibilityRole="header" style={{ ...text, fontSize: 22, marginTop: 4 }}>{title}</Text>
  {running ? <Ring progress={progress} label={base === 'camera' ? 'flashes with a click' : 'swaps with a click and a beep'} /> : done ? null : <Illustration />}
  {running ? <Text style={{ ...small, textAlign: 'center' }}>{base === 'camera' ? 'Keep the phone still. This takes about 15 s.' : 'Keep the calibrator on the screen. This takes about 12 s.'}</Text>
   : done && result ? <View style={{ paddingVertical: 24 }}>
    <Text style={{ ...text, fontSize: 24 }}>{label}</Text>
    <Text style={{ ...small, marginTop: 6 }}>{`${result.pairs ?? '—'} ${base === 'camera' ? 'flashes' : 'swaps'}`}{base === 'display' ? ' · positive: the sound comes out after the picture' : ''}</Text>
    <Text style={{ ...small, marginTop: 6 }}>{base === 'camera' ? 'Save corrects this phone’s measurements (like the clap calibration).' : base === 'display' ? 'Save puts it into this phone’s test signal (AVS4 QR), so analysers can take it into account.' : 'Not saved: it belongs to that screen and its sound path.'}</Text>
   </View> : <Text style={{ ...text, color: colors.muted, marginBottom: 6 }}>{body}</Text>}
  {error ? <Text style={{ ...small, color: colors.accent, marginTop: 8 }}>{error}</Text> : null}
  {step === 'plug' && <Button primary title="Next" onPress={() => onNext('camera')} />}
  {(base === 'camera' || base === 'display' || base === 'tv') && !running && !done && <Button primary title="Start" onPress={() => onStart(base)} />}
  {done && base !== 'tv' && <Button primary title="Save" onPress={() => onSave(base)} />}
  {done && <Button title="Repeat" onPress={() => onRepeat(base)} />}
  {base === 'camera' && !running && <Button title={done ? 'Next without saving' : 'Skip'} onPress={() => onNext('display')} />}
  {step === 'display' && <Button title="Measure a TV or monitor instead" onPress={() => onNext('tv')} />}
  <Button title={running ? 'Cancel' : done && base !== 'camera' ? 'Close' : 'Cancel'} onPress={onClose} />
 </ScrollView>;
}
