import { useEffect, useRef, useState } from 'react';
import { Image, Platform, Pressable, SafeAreaView, ScrollView, Share, StatusBar, Text, TextInput, View } from 'react-native';
import { useCameraPermissions, useMicrophonePermissions } from 'expo-camera';
import { Audio } from 'expo-av';
import { requireNativeViewManager } from 'expo-modules-core';
import Ionicons from '@expo/vector-icons/Ionicons';
import native from './modules/avsync';
import { signalFeedback, latencyValues, latencyHint } from './src/feedback.mjs';
import { checkClock, clockSummary } from './src/clock.mjs';
import { offsetText, measureSentence, formatResultWhen } from './src/measure.mjs';
import { validCalibration, prepareReading, makeCalibration, nearestFrame } from './src/workflow.mjs';

const NativeCamera = native?.supportsLiveCapture ? requireNativeViewManager('Avsync') : null;
const bg = '#121417', ink = '#e7ecf1', muted = '#9aa7b5', orange = '#ffc26b';
const text = { color: ink, fontSize: 16, lineHeight: 23 };
const button = { padding: 12, borderRadius: 8, backgroundColor: '#2a3644', marginTop: 10, alignItems: 'center' };
const round = { width: 44, height: 44, borderRadius: 22, backgroundColor: '#2a3644', alignItems: 'center', justifyContent: 'center' };
function Button({ title, onPress, disabled }) { return <Pressable accessibilityRole="button" disabled={disabled} onPress={onPress} style={[button, disabled && { opacity: 0.35 }]}><Text style={text}>{title}</Text></Pressable>; }
function SignalPill({ value, icon }) {
 const color = value.tone === 'green' ? '#76e2a3' : value.tone === 'orange' ? orange : muted;
 return <View accessible accessibilityLabel={value.label} style={{ flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: 10, paddingVertical: 7, borderRadius: 18, backgroundColor: '#121417e8', borderWidth: 1, borderColor: `${color}66` }}><Ionicons name={icon} size={15} color={color} /><Text style={{ color, fontSize: 12, fontWeight: '600' }}>{value.label}</Text></View>;
}
function LatencyPanel({ values, hint, clock, onCheck, checking, error }) {
 const format = value => value === null ? '—' : `${value < 0 ? '−' : ''}${Math.abs(value).toFixed(0)} ms`;
 return <View style={{ marginTop: 10, backgroundColor: '#1c2128', borderRadius: 10, padding: 10 }}>
  <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}><Text style={{ color: orange, fontSize: 12, fontWeight: '600' }}>ESTIMATED LATENCY</Text><Pressable accessibilityRole="button" accessibilityLabel="Check phone clock" disabled={checking} onPress={onCheck}><Text style={{ color: muted, fontSize: 12 }}>{checking ? 'Checking…' : 'Check clock'}</Text></Pressable></View>
  <View style={{ flexDirection: 'row', gap: 24, marginTop: 5 }}><Text style={{ color: ink, fontSize: 17 }}>Video {format(values.video)}</Text><Text style={{ color: ink, fontSize: 17 }}>Audio {format(values.audio)}</Text></View>
  <Text style={{ color: muted, fontSize: 10, marginTop: 4 }}>{values.video === null ? hint : 'Live QR → phone capture · includes phone capture delay'}</Text>
  <Text style={{ color: error ? orange : muted, fontSize: 10, marginTop: 4 }}>{error || clock}</Text>
 </View>;
}
function GeneratorPanel({ rate, onRate, onStart, busy, clock, checking, clockError, onCheck, notice }) {
 return <ScrollView contentContainerStyle={{ padding: 20 }}>
  <View style={{ padding: 20, backgroundColor: '#1c2128', borderRadius: 16 }}>
   <Ionicons name="phone-landscape-outline" size={52} color={orange} />
   <Text style={{ ...text, fontSize: 24, marginTop: 14 }}>Generate a test signal</Text>
   <Text style={{ color: muted, lineHeight: 22, marginTop: 10 }}>Show the full picture to an external camera and keep this phone's speaker audible. Route that camera's picture and sound to the analyzer.</Text>
   <View style={{ flexDirection: 'row', gap: 10, marginTop: 16 }}>{[1, 2].map(value => <Pressable key={value} accessibilityRole="radio" accessibilityState={{ checked: rate === value }} onPress={() => onRate(value)} style={{ flex: 1, padding: 13, borderRadius: 10, borderWidth: 1, borderColor: rate === value ? orange : '#394454', alignItems: 'center' }}><Text style={{ color: rate === value ? orange : ink }}>{value} chirp{value === 1 ? '' : 's'} / second</Text></Pressable>)}</View>
   <Text style={{ color: muted, marginTop: 12, fontSize: 12 }}>1200 baud · full audio timecode · white flash at chirp onset</Text>
   <Button title={busy ? 'Signal is open…' : 'Start fullscreen signal'} disabled={busy || !native?.supportsGenerator} onPress={onStart} />
   {!native?.supportsGenerator && <Text style={{ color: orange, marginTop: 10 }}>Install the updated native development build. Expo Go cannot generate this signal.</Text>}
  </View>
  <View style={{ marginTop: 20, padding: 16, borderRadius: 12, backgroundColor: '#1c2128' }}>
   <Text style={{ color: orange, fontWeight: '600' }}>ESTIMATED LATENCY</Text>
   <Text style={{ color: muted, lineHeight: 21, marginTop: 8 }}>AV sync includes this phone's display and speaker plus the camera and transport path. Absolute latency uses the phone clock and scheduled output. Its physical accuracy is unknown.</Text>
   <Text style={{ color: muted, lineHeight: 21, marginTop: 8 }}>Camera/microphone calibration in Measure does not calibrate this source. Keep all four corner markers visible and use the phone speaker.</Text>
   <Text style={{ color: clockError ? orange : muted, fontSize: 12, marginTop: 10 }}>{clockError || clock}</Text>
   <Button title={checking ? 'Checking clock…' : 'Check phone clock'} disabled={checking} onPress={onCheck} />
  </View>
  {notice && <Text style={{ color: orange, marginTop: 12 }}>{notice}</Text>}
 </ScrollView>;
}
function initialState() { try { return JSON.parse(native?.loadState() || 'null') || { calibration: null, results: [] }; } catch { return { calibration: null, results: [] }; } }

function Waveform({ take }) {
 const values = take.waveform || [], max = Math.max(0.001, ...values);
 return <View style={{ height: 60, marginTop: 10, justifyContent: 'center' }}>
  <View style={{ flexDirection: 'row', alignItems: 'center', height: 56 }}>{values.map((v, i) => <View key={i} style={{ flex: 1, height: Math.max(2, v / max * 54), backgroundColor: '#7aa2ff', marginRight: 1 }} />)}</View>
  <View style={{ position: 'absolute', left: `${Math.min(100, Math.max(0, (take.spikeTimeMs - (take.waveformStartMs || 0)) / (take.waveformDurationMs || take.durationMs) * 100))}%`, width: 2, top: 0, bottom: 0, backgroundColor: orange }} />
 </View>;
}

function CalibrationReview({ take, onSave, onRepeat, onCancel }) {
 const [selected, setSelected] = useState(() => nearestFrame(take.frames, take.spikeTimeMs));
 const sound = useRef(null);
 useEffect(() => () => { sound.current?.unloadAsync(); }, []);
 const frame = take.frames[selected], delta = frame.timeMs - take.spikeTimeMs;
 const replay = async () => { await sound.current?.unloadAsync(); const result = await Audio.Sound.createAsync({ uri: take.uri }, { shouldPlay: true }); sound.current = result.sound; };
 return <ScrollView contentContainerStyle={{ padding: 20 }}>
  <Text style={{ ...text, fontSize: 22 }}>Match the sound</Text>
  <Text style={{ color: muted, lineHeight: 21, marginVertical: 10 }}>Select the first frame where the hands or object make contact. There are frames on BOTH sides of the sound.</Text>
  <Image source={{ uri: frame.uri }} resizeMode="contain" style={{ width: '100%', height: 280, backgroundColor: '#000', borderRadius: 12 }} />
  <View style={{ flexDirection: 'row', gap: 12 }}><View style={{ flex: 1 }}><Button title="← Previous" disabled={!selected} onPress={() => setSelected(i => i - 1)} /></View><View style={{ flex: 1 }}><Button title="Next →" disabled={selected === take.frames.length - 1} onPress={() => setSelected(i => i + 1)} /></View></View>
  <Text style={{ ...text, marginTop: 10 }}>Frame {selected + 1}/{take.frames.length} · {delta >= 0 ? '+' : ''}{delta.toFixed(1)} ms from sound</Text>
  <Text style={{ color: muted }}>{take.framesBefore ?? take.frames.filter(f => f.timeMs < take.spikeTimeMs).length} before · {take.framesAfter ?? take.frames.filter(f => f.timeMs > take.spikeTimeMs).length} after · {take.fps.toFixed(0)} fps</Text>
  <Waveform take={take} />
  <Text style={text}>Correction {offsetText(take.spikeTimeMs - frame.timeMs)}</Text>
  <Button title="Replay sound" onPress={replay} /><Button title="Save calibration" onPress={() => onSave(selected)} /><Button title="Repeat" onPress={onRepeat} /><Button title="Cancel" onPress={onCancel} />
 </ScrollView>;
}

export default function App() {
 const [session, setSession] = useState(initialState);
 const [live, setLive] = useState(null), [tab, setTab] = useState('measure');
 const [generating, setGenerating] = useState(false);
 const [settings, setSettings] = useState(false), [flow, setFlow] = useState(null), [take, setTake] = useState(null);
 const [running, setRunning] = useState(true), [status, setStatus] = useState({ message: 'Looking for test signal…' }), [notice, setNotice] = useState(null);
 const [cameraPermission, requestCamera] = useCameraPermissions(), [micPermission, requestMic] = useMicrophonePermissions();
 const camera = useRef(null), lastReportAt = useRef(-Infinity);
 const [tick, setTick] = useState(() => performance.now());
 const [clockCheck, setClockCheck] = useState(null), [clockBusy, setClockBusy] = useState(false), [clockError, setClockError] = useState(null);
 useEffect(() => { const timer = setInterval(() => setTick(performance.now()), 200); return () => clearInterval(timer); }, []);
 const age = Math.max(0, tick - lastReportAt.current);
 const feedback = signalFeedback(status, age, running && !flow), latency = latencyValues(status, age, running && !flow);
 const taiUtcSeconds = Number.isFinite(session.taiUtcSeconds) ? session.taiUtcSeconds : 37;
 const [taiDraft, setTaiDraft] = useState(String(taiUtcSeconds));
 useEffect(() => { if (settings) setTaiDraft(String(taiUtcSeconds)); }, [settings, taiUtcSeconds]);
 useEffect(() => { if (running && age > 1600) setLive(null); }, [running, age]);
 const runClockCheck = async () => { setClockBusy(true); setClockError(null); try { setClockCheck(await checkClock()); } catch (error) { setClockCheck(null); setClockError(`Clock check unavailable: ${error.message}`); } finally { setClockBusy(false); } };
 const os = native?.deviceVersion() || String(Platform.Version);
 const calibration = validCalibration(session.calibration, os, Date.now());
 useEffect(() => { native?.saveState(JSON.stringify(session)); }, [session]);
 const ask = async () => { await requestCamera(); await requestMic(); };
 const cancelCalibration = () => { setFlow(null); camera.current?.resumeAnalysis(); };
 const report = event => {
  const value = event.nativeEvent;
  lastReportAt.current = performance.now();
  setStatus(value);
  if (value.kind === 'calibration') { setTake(value); setFlow('review'); }
  else if (value.kind === 'error') { setNotice(value.message); if (flow === 'listening') { setFlow('aim'); camera.current?.resumeAnalysis(); } }
  else if (value.kind === 'status' && !flow) setLive(null);
  else if (value.kind === 'reading' && !flow) { setLive(prepareReading(value, calibration, Date.now())); setNotice(null); }
 };
 const save = () => { if (live) { setSession(s => ({ ...s, results: [{ ...live, id: String(Date.now()), at: Date.now() }, ...s.results].slice(0, 50) })); setNotice('Measurement saved'); } };
 const exportResult = async item => { try { await Share.share({ message: JSON.stringify({ ...item, mode: 'LIVE', reference: { type: 'none' }, accuracy: 'unvalidated' }, null, 2) }); } catch (e) { setNotice(e.message); } };
 const startGenerator = async () => {
  setGenerating(true); setNotice(null);
  try {
   const result = await native.generate(session.generatorRate === 1 ? 1 : 2, taiUtcSeconds);
   setNotice(`Signal stopped · ${result.skippedDisplayFrames} skipped display frames · output timing estimated`);
  } catch (error) { setNotice(error.message); }
  finally { setGenerating(false); }
 };
 const startCalibration = () => { setSettings(false); setRunning(true); setFlow('aim'); setNotice(null); };
 return <SafeAreaView style={{ flex: 1, backgroundColor: bg }}>
  <StatusBar barStyle="light-content" />
  <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingHorizontal: 16, paddingTop: 8 }}>
   <View style={{ flexDirection: 'row', alignItems: 'center' }}><Image source={require('./assets/icon.png')} style={{ width: 28, height: 28, borderRadius: 6, marginRight: 10 }} /><Text style={{ color: muted, letterSpacing: 1 }}>{flow ? 'CALIBRATION' : tab.toUpperCase()}</Text></View>
   <Pressable accessibilityLabel="Settings" onPress={() => setSettings(true)} style={round}><Ionicons name="settings-outline" size={24} color={ink} /></Pressable>
  </View>
  {flow === 'review' ? <CalibrationReview take={take} onCancel={() => { setFlow(null); setTake(null); }} onRepeat={() => { setTake(null); setFlow('aim'); }} onSave={index => { const value = makeCalibration(take, index, os, Date.now()); setSession(s => ({ ...s, calibration: value })); setLive(null); setTake(null); setFlow(null); }} />
   : tab === 'measure' || flow ? <View style={{ flex: 1 }}>
    <View style={{ flex: 1, backgroundColor: '#000', margin: 12, borderRadius: 12, overflow: 'hidden', justifyContent: 'center' }}>
     {!NativeCamera ? <Text style={{ ...text, padding: 20 }}>Install the new native build to enable high-frame-rate live capture.</Text>
      : cameraPermission?.granted && micPermission?.granted ? <NativeCamera ref={camera} active={running || Boolean(flow)} taiUtcOffset={taiUtcSeconds} onReport={report} style={{ flex: 1 }} />
      : <View style={{ padding: 20 }}><Text style={text}>Allow camera and microphone for live AV sync measurement.</Text><Button title="Allow access" onPress={ask} /></View>}
     {!flow && <View pointerEvents="none" style={{ position: 'absolute', left: 12, right: 12, top: 12, flexDirection: 'row', flexWrap: 'wrap', gap: 7 }}><SignalPill value={feedback.audio} icon="pulse-outline" /><SignalPill value={feedback.picture} icon="scan-outline" /></View>}
     {status.configuredFps > 0 && <Text style={{ position: 'absolute', left: 14, bottom: 12, color: ink, backgroundColor: '#0009', padding: 5, borderRadius: 5 }}>{status.configuredFps.toFixed(0)} fps camera · {(status.fps || 0).toFixed(0)} fps processed</Text>}
     {flow && <View style={{ position: 'absolute', top: 12, left: 12, right: 12, padding: 12, borderRadius: 8, backgroundColor: '#121417db' }}><Text style={text}>{flow === 'listening' ? 'Listening… Capture continues for 700 ms AFTER the sound.' : 'Aim at one visible clap or tap. Start, wait a second, then make the sound.'}</Text></View>}
    </View>
    {flow ? <View style={{ paddingHorizontal: 20, paddingBottom: 12 }}>{flow === 'aim' && <Button title="Start calibration" disabled={!NativeCamera || !cameraPermission?.granted || !micPermission?.granted} onPress={() => { setFlow('listening'); camera.current?.beginCalibration(); }} />}<Button title="Cancel" onPress={cancelCalibration} />{notice && <Text style={{ color: orange }}>{notice}</Text>}</View>
     : <View style={{ paddingHorizontal: 20, paddingBottom: 12 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' }}><Text style={{ color: live && !calibration ? orange : ink, fontSize: 56 }}>{offsetText(live?.offset_ms)}</Text><Pressable accessibilityLabel="Clear reading" onPress={() => { setLive(null); camera.current?.resumeAnalysis(); }} style={round}><Ionicons name="close-outline" size={28} color={ink} /></Pressable></View>
      <Text style={{ color: ink, fontSize: 20, marginTop: 4 }}>{measureSentence(live?.offset_ms)}</Text>
      <Text style={{ color: muted, marginTop: 12 }}>Quality: {live?.quality || '—'} · Calibration: {calibration ? 'set' : 'missing (0 ms)'} · Uncertainty: unknown</Text>
      <Text style={{ color: notice ? orange : muted, marginTop: 8, fontSize: 12 }}>{notice || (status.kind === 'reading' ? `${status.eventCount} matched events · live updates` : status.message || 'Looking for test signal…')}</Text>
      <LatencyPanel values={latency} hint={!running ? 'Analysis paused' : age > 1600 ? 'Waiting for capture updates' : latencyHint(status.latency?.state)} clock={clockSummary(clockCheck)} onCheck={runClockCheck} checking={clockBusy} error={clockError} />
      <View style={{ flexDirection: 'row', gap: 10 }}><View style={{ flex: 1 }}><Button title={running ? 'Pause' : 'Live analysis'} onPress={() => setRunning(v => !v)} /></View><View style={{ flex: 1 }}><Button title="Save result" disabled={!live} onPress={save} /></View></View>
     </View>}
   </View> : tab === 'generator' ? <GeneratorPanel rate={session.generatorRate === 1 ? 1 : 2} onRate={value => setSession(s => ({ ...s, generatorRate: value }))} onStart={startGenerator} busy={generating} clock={clockSummary(clockCheck)} checking={clockBusy} clockError={clockError} onCheck={runClockCheck} notice={notice} /> : <ScrollView contentContainerStyle={{ padding: 20 }}>{session.results.length ? session.results.map(item => <Pressable key={item.id} onPress={() => exportResult(item)} style={{ paddingVertical: 16, borderBottomWidth: 1, borderBottomColor: '#2c3540' }}><Text style={{ color: item.calibrated ? ink : orange, fontSize: 22 }}>{offsetText(item.offset_ms)}</Text><Text style={{ color: muted }}>{formatResultWhen(item.at)} · {item.calibrated ? 'Calibrated' : '0 ms correction'} · tap to export</Text></Pressable>) : <Text style={{ color: muted }}>No saved results</Text>}</ScrollView>}
  {!flow && <View style={{ flexDirection: 'row', borderTopWidth: 1, borderTopColor: '#2c3540' }}>{['measure', 'generator', 'results'].map(value => <Pressable key={value} onPress={() => setTab(value)} style={{ flex: 1, paddingVertical: 14, alignItems: 'center' }}><Text style={{ color: tab === value ? ink : muted, fontWeight: tab === value ? '700' : '400' }}>{value === 'measure' ? 'Measure' : value === 'generator' ? 'Generate' : 'Results'}</Text></Pressable>)}</View>}
  {settings && <View style={{ position: 'absolute', top: 0, right: 0, bottom: 0, left: 0, backgroundColor: '#0007', justifyContent: 'flex-end' }}><View style={{ backgroundColor: '#1c2128', padding: 20, borderTopLeftRadius: 16, borderTopRightRadius: 16 }}><Text style={{ ...text, fontSize: 18 }}>Settings</Text><Text style={{ color: muted, fontSize: 12, marginTop: 12 }}>Latency uses the live QR's TAI timestamp and phone UTC. Capture and source clock accuracy are unknown. The HTTPS check detects gross clock errors and does not calibrate latency.</Text><View style={{ flexDirection: 'row', alignItems: 'center', marginVertical: 10 }}><Text style={{ color: ink, flex: 1 }}>Source TAI−UTC (seconds)</Text><TextInput accessibilityLabel="Source TAI minus UTC seconds" keyboardType="number-pad" value={taiDraft} onChangeText={value => { if (/^\d{0,3}$/.test(value)) setTaiDraft(value); }} style={{ color: ink, backgroundColor: '#121417', borderRadius: 6, padding: 8, minWidth: 48, textAlign: 'center' }} /><Pressable accessibilityRole="button" disabled={!taiDraft || Number(taiDraft) > 100} onPress={() => { setSession(previous => ({ ...previous, taiUtcSeconds: Number(taiDraft) })); setStatus(previous => ({ ...previous, latency: { state: 'waiting_for_live_qr' } })); }} style={{ padding: 8 }}><Text style={{ color: !taiDraft || Number(taiDraft) > 100 ? muted : orange }}>Apply</Text></Pressable></View><Text style={{ color: muted, fontSize: 11 }}>Time check: worldtime.timezone.io · only when requested</Text>{[['Repeat calibration', startCalibration], ['Clear saved calibration (use 0 ms)', () => { setSession(s => ({ ...s, calibration: null })); setLive(null); setSettings(false); }], ['Clear results', () => setSession(s => ({ ...s, results: [] }))], ['Close', () => setSettings(false)]].map(([label, onPress]) => <Pressable key={label} onPress={onPress} style={{ paddingVertical: 14, borderTopWidth: 1, borderTopColor: '#2c3540' }}><Text style={text}>{label}</Text></Pressable>)}</View></View>}
 </SafeAreaView>;
}
