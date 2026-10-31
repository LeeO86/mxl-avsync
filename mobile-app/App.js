import { useEffect, useMemo, useRef, useState } from 'react';
import { AppState, Image, Linking, Modal, PermissionsAndroid, Platform, Pressable, SafeAreaView, ScrollView, Share, StatusBar, Text, TextInput, View } from 'react-native';
import { useCameraPermissions, useMicrophonePermissions } from 'expo-camera';
import { Audio } from 'expo-av';
import { requireNativeViewManager } from 'expo-modules-core';
import Ionicons from '@expo/vector-icons/Ionicons';
import native from './modules/avsync';
import { receiveReading, readingPresentation, savedReading } from './src/reading.mjs';
import { generatorOptions } from './src/generator.mjs';
import { signalFeedback, latencyValues, latencyHint } from './src/feedback.mjs';
import { applyTimeReference, clockRadiusText, clockSettings, generatorClockOptions, measurementSourceKey, referenceSummary, synchronizeClock } from './src/time-reference.mjs';
import { offsetText, measureSentence, formatResultWhen } from './src/measure.mjs';
import { validCalibration, calibrationSummary, makeCalibration, nearestFrame } from './src/workflow.mjs';

const NativeCamera = native?.supportsLiveCapture?.() ? requireNativeViewManager('Avsync') : null;
const bg = '#121417', ink = '#e7ecf1', muted = '#9aa7b5', orange = '#ffc26b';
const text = { color: ink, fontSize: 16, lineHeight: 23 };
const button = { padding: 12, borderRadius: 8, backgroundColor: '#2a3644', marginTop: 10, alignItems: 'center' };
const round = { width: 44, height: 44, borderRadius: 22, backgroundColor: '#2a3644', alignItems: 'center', justifyContent: 'center' };
function Button({ title, onPress, disabled }) { return <Pressable accessibilityRole="button" disabled={disabled} onPress={onPress} style={[button, disabled && { opacity: 0.35 }]}><Text style={text}>{title}</Text></Pressable>; }
function SignalPill({ value, icon }) {
 const color = value.tone === 'green' ? '#76e2a3' : value.tone === 'orange' ? orange : muted;
 return <View accessible accessibilityLabel={value.label} style={{ flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: 10, paddingVertical: 7, borderRadius: 18, backgroundColor: '#121417e8', borderWidth: 1, borderColor: `${color}66` }}><Ionicons name={icon} size={15} color={color} /><Text style={{ color, fontSize: 12, fontWeight: '600' }}>{value.label}</Text></View>;
}
function LatencyPanel({ values, hint, clock, onCheck, checking, error, sampleNote, details }) {
 const [diagnostics, setDiagnostics] = useState(false);
 const format = value => value === null ? '—' : `${value < 0 ? '−' : ''}${Math.abs(value).toFixed(0)} ms`;
 return <View style={{ marginTop: 10, backgroundColor: '#1c2128', borderRadius: 10, padding: 10 }}>
  <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}><Text style={{ color: orange, fontSize: 12, fontWeight: '600' }}>ESTIMATED LATENCY</Text><Pressable accessibilityRole="button" accessibilityLabel="Check phone clock" disabled={checking} onPress={onCheck}><Text style={{ color: muted, fontSize: 12 }}>{checking ? 'Checking…' : 'Check clock'}</Text></Pressable></View>
  <View style={{ flexDirection: 'row', gap: 24, marginTop: 5 }}><Text style={{ color: ink, fontSize: 17 }}>Video {format(values.video)}</Text><Text style={{ color: ink, fontSize: 17 }}>Audio {format(values.audio)}</Text></View>
  <Text style={{ color: muted, fontSize: 10, marginTop: 4 }}>{values.video === null ? hint : 'Source time → phone capture · includes phone capture delay'}</Text>
  {sampleNote && <Text style={{ color: orange, fontSize: 10, marginTop: 4 }}>{sampleNote}</Text>}
  <Text style={{ color: error ? orange : muted, fontSize: 10, marginTop: 4 }}>{error || clock}</Text>
  <Text style={{ color: muted, fontSize: 10, marginTop: 4 }}>{details?.clock?.uncertaintyMs != null ? `Clock contribution ${clockRadiusText(details.clock.uncertaintyMs)} · capture/source uncertainty unknown` : 'Capture, source and clock accuracy unknown'}</Text>
  <Text style={{ color: muted, fontSize: 10 }}>{details?.basis ? [...new Set(Object.values(details.basis))].join(' · ') : 'Relative to source timestamp'}</Text>
  {details?.flags?.some(flag => flag.endsWith('mismatch')) && <Text style={{color: orange, fontSize: 11}}>Clock or source emit offset mismatch · see diagnostics</Text>}
  {details?.flags?.includes('source_emit_offsets_belong_to_another_source') && <Text style={{color:orange,fontSize:11}}>Source changed · enter emit offsets for this source</Text>}
  {details && <Pressable accessibilityRole="button" onPress={() => setDiagnostics(value => !value)}><Text style={{color: muted,fontSize: 11,marginTop: 5}}>{diagnostics ? 'Hide diagnostics' : 'Timing diagnostics'}</Text></Pressable>}
  {diagnostics && <View style={{marginTop:5}}>
   <Text selectable style={{color:muted,fontSize:10}}>Device UTC raw · V {format(details?.raw_video_ms ?? null)} / A {format(details?.raw_audio_ms ?? null)}</Text>
   <Text selectable style={{color:muted,fontSize:10}}>Arrival vs timestamp · V {format(details?.video_arrival_vs_timestamp_ms ?? null)} / A {format(details?.audio_arrival_vs_timestamp_ms ?? null)}</Text>
   <Text selectable style={{color:muted,fontSize:10}}>Source emit offsets · V {format(details?.video_source_emit_offset_ms ?? null)} / A {format(details?.audio_source_emit_offset_ms ?? null)}</Text>
   <Text selectable style={{color:muted,fontSize:10}}>Clock correction {format(details?.clock?.correctionMs ?? null)} · reference age {details?.clock?.ageMs != null ? `${(details.clock.ageMs/1000).toFixed(1)} s` : 'unknown'}</Text>
  </View>}
 </View>;
}
function GeneratorPanel({ rate, onRate, onStart, busy, clock, checking, clockError, onCheck, notice, session, onChange, image, picking, onPickImage }) {
 return <ScrollView keyboardShouldPersistTaps="handled" contentContainerStyle={{ padding: 20 }}>
  <View style={{ padding: 20, backgroundColor: '#1c2128', borderRadius: 16 }}>
   <Ionicons name="phone-landscape-outline" size={52} color={orange} />
   <Text style={{ ...text, fontSize: 24, marginTop: 14 }}>Generate a test signal</Text>
   <Text style={{ color: muted, lineHeight: 22, marginTop: 10 }}>Show the full picture to an external camera and keep this phone's speaker audible. Route that camera's picture and sound to the analyzer.</Text>
   <View style={{ flexDirection: 'row', gap: 10, marginTop: 16 }}>{[1, 2].map(value => <Pressable key={value} accessibilityRole="radio" accessibilityState={{ checked: rate === value }} onPress={() => onRate(value)} style={{ flex: 1, padding: 13, borderRadius: 10, borderWidth: 1, borderColor: rate === value ? orange : '#394454', alignItems: 'center' }}><Text style={{ color: rate === value ? orange : ink }}>{value} chirp{value === 1 ? '' : 's'} / second</Text></Pressable>)}</View>
   <Text style={{ color: muted, marginTop: 12, fontSize: 12 }}>1200 baud · full audio timecode · white flash at chirp onset</Text>
   <Text style={{ color: muted, marginTop: 18, marginBottom: 6 }}>Title</Text>
   <TextInput accessibilityLabel="Generator title" editable={!busy} maxLength={64} placeholder="iPhone" placeholderTextColor={muted} value={session.generatorName || ''} onChangeText={value => onChange('generatorName', value)} style={{ ...text, backgroundColor: '#121417', padding: 12, borderRadius: 8 }} />
   <Text style={{ color: muted, marginTop: 12, marginBottom: 6 }}>Subtitle</Text>
   <TextInput accessibilityLabel="Generator subtitle" editable={!busy} maxLength={128} placeholder="Camera, location or signal path" placeholderTextColor={muted} value={session.generatorSubtitle || ''} onChangeText={value => onChange('generatorSubtitle', value)} style={{ ...text, backgroundColor: '#121417', padding: 12, borderRadius: 8 }} />
   {image && <Image accessibilityLabel="Generator image preview" source={{ uri: image.uri }} resizeMode="contain" style={{ width: 72, height: 72, marginTop: 12, borderRadius: 8, backgroundColor: '#121417' }} />}
   <Button title={picking ? 'Importing image…' : image ? 'Change image' : 'Choose image'} disabled={busy || picking || !native?.pickGeneratorImage} onPress={onPickImage} />
   {session.generatorImageId && <Button title="Remove image" disabled={busy || picking} onPress={() => onChange('generatorImageId', '')} />}
   <Text style={{ color: muted, marginTop: 8, fontSize: 12 }}>Name, subtitle and image stay on this phone. Keep all four markers visible when filming.</Text>
   {!native?.generateStyled && <Text style={{ color: orange, marginTop: 8 }}>Install the updated native build for color bars, sync scale and custom labels.</Text>}
   <Button title={busy ? 'Signal is open…' : 'Start fullscreen signal'} disabled={busy || picking || !native?.supportsGenerator?.()} onPress={onStart} />
   {!native?.supportsGenerator?.() && <Text style={{ color: orange, marginTop: 10 }}>Install the updated native development build. Expo Go cannot generate this signal.</Text>}
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
 const [readingSnapshot, setReadingSnapshot] = useState(null), [tab, setTab] = useState('measure');
 const [generating, setGenerating] = useState(false), [pickingImage, setPickingImage] = useState(false);
 const generatorImage = useMemo(() => session.generatorImageId && native?.generatorImageInfo ? native.generatorImageInfo(session.generatorImageId) : null, [session.generatorImageId]);
 const [settings, setSettings] = useState(false), [flow, setFlow] = useState(null), [take, setTake] = useState(null);
 const [running, setRunning] = useState(true), [status, setStatus] = useState({ message: 'Looking for test signal…' }), [notice, setNotice] = useState(null);
 const [cameraPermission, requestCamera] = useCameraPermissions(), [micPermission, requestMic] = useMicrophonePermissions();
 const camera = useRef(null), lastReportAt = useRef(-Infinity);
 const [tick, setTick] = useState(() => performance.now());
 const clockGeneration = useRef(0), clockInFlight = useRef(false);
 const [clockCheck, setClockCheck] = useState(null), [clockBusy, setClockBusy] = useState(false), [clockError, setClockError] = useState(null);
 useEffect(() => { const timer = setInterval(() => setTick(performance.now()), 200); return () => clearInterval(timer); }, []);
 const age = Math.max(0, tick - lastReportAt.current);
 const live = readingSnapshot?.reading || null;
 const readingView = readingPresentation(readingSnapshot, status, age, tick, running && !flow);
 const feedback = signalFeedback(status, age, running && !flow);
 const currentLatency = latencyValues(status, age, running && !flow);
 const latencyHeld = Boolean(live) && currentLatency.video === null && currentLatency.audio === null;
 const latency = latencyHeld ? latencyValues(live, 0, true) : currentLatency;
 const taiUtcSeconds = Number.isFinite(session.taiUtcSeconds) ? session.taiUtcSeconds : 37;
 const [taiDraft, setTaiDraft] = useState(String(taiUtcSeconds));
 useEffect(() => { if (settings) setTaiDraft(String(taiUtcSeconds)); }, [settings, taiUtcSeconds]);
 useEffect(() => { const subscription = AppState.addEventListener('change', value => {
  // Core Media host time need not advance across device sleep. Never carry its UTC mapping through backgrounding.
  if(value !== 'active') { clockGeneration.current += 1;setClockCheck(null); }
 });return () => {clockGeneration.current += 1;subscription.remove();}; }, []);
 const timeSettings = useMemo(() => { try { return clockSettings(session); } catch { return clockSettings(); } }, [session.ntpServer, session.clockMaxUncertaintyMs, session.sourceEmitVideoMs, session.sourceEmitAudioMs, session.emitSourceId]);
 const [clockServerDraft,setClockServerDraft] = useState(timeSettings.server), [clockLimitDraft,setClockLimitDraft] = useState(String(timeSettings.maxUncertaintyMs));
 const [emitVideoDraft,setEmitVideoDraft] = useState(''), [emitAudioDraft,setEmitAudioDraft] = useState('');
 useEffect(() => { if(settings) { setClockServerDraft(timeSettings.server);setClockLimitDraft(String(timeSettings.maxUncertaintyMs));setEmitVideoDraft(String(session.sourceEmitVideoMs ?? ''));setEmitAudioDraft(String(session.sourceEmitAudioMs ?? '')); } }, [settings]);
 const clockText = referenceSummary(clockCheck, tick, timeSettings.maxUncertaintyMs);
 const timeDraftChanged = clockServerDraft !== timeSettings.server || clockLimitDraft !== String(timeSettings.maxUncertaintyMs) || emitVideoDraft !== String(session.sourceEmitVideoMs ?? '') || emitAudioDraft !== String(session.sourceEmitAudioMs ?? '');
 const applyClockSettings = () => {
  try { const draft = {...session,ntpServer:clockServerDraft,clockMaxUncertaintyMs:clockLimitDraft,sourceEmitVideoMs:emitVideoDraft,sourceEmitAudioMs:emitAudioDraft,emitSourceId:measurementSourceKey(status)};clockSettings(draft);setSession(draft);setClockError(null);setClockCheck(null);setReadingSnapshot(null);setNotice('Clock settings saved · synchronize the clock again'); }
  catch(error) { setClockError(error.message); }
 };
 const runClockCheck = async () => {
  if(clockInFlight.current) return;
  clockInFlight.current = true;
  setClockBusy(true);setClockError(null);
  try {
   if (Platform.OS === 'android') {
    const permission = await PermissionsAndroid.requestMultiple([PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,PermissionsAndroid.PERMISSIONS.ACCESS_COARSE_LOCATION]);
    if(permission[PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION] !== PermissionsAndroid.RESULTS.GRANTED) throw new Error('Precise location permission is required for GNSS time.');
   }
   if(AppState.currentState !== 'active') throw new Error('Return to the app before synchronizing.');
   const generation = clockGeneration.current;
   const value = await synchronizeClock(native,session);
   if(generation !== clockGeneration.current || AppState.currentState !== 'active') throw new Error('App left the foreground. Synchronize again.');
   setClockCheck(value);
  } catch(error) { setClockCheck(null);setClockError(`Clock unavailable: ${error.message}`); }
  finally { clockInFlight.current = false;setClockBusy(false); }
 };
 const os = native?.deviceVersion?.() || String(Platform.Version);
 const now = Date.now();
 const calibration = validCalibration(session.calibration, os, now, status.captureContext);
 const correction = calibrationSummary(session.calibration, os, now, status.captureContext);
 useEffect(() => { native?.saveState(JSON.stringify(session)); }, [session]);
 const ask = async () => { await requestCamera(); await requestMic(); };
 const cancelCalibration = () => { setFlow(null); camera.current?.resumeAnalysis(); };
 const report = event => {
  const sourceKey = measurementSourceKey(event.nativeEvent);
  if (sourceKey && !session.emitSourceId && (timeSettings.videoEmitMs !== null || timeSettings.audioEmitMs !== null)) setSession(previous => ({...previous,emitSourceId:sourceKey}));
  const value = applyTimeReference(event.nativeEvent, clockCheck, timeSettings);
  lastReportAt.current = performance.now();
  setStatus(value);
  if (value.kind === 'calibration') { setTake(value); setFlow('review'); }
  else if (value.kind === 'error') { setNotice(value.message); if (flow === 'listening') { setFlow('aim'); camera.current?.resumeAnalysis(); } }
  else if (!flow) {
   const mono = performance.now();
   setReadingSnapshot(previous => receiveReading(previous, value, validCalibration(session.calibration, os, Date.now(), value.captureContext), Date.now(), mono));
   if (value.kind === 'reading') setNotice(null);
  }
 };
 const save = () => { const atMono = performance.now(); const view = readingPresentation(readingSnapshot, status, Math.max(0, atMono - lastReportAt.current), atMono, running && !flow); const item = savedReading(readingSnapshot, view, Date.now()); if (item) { setSession(s => ({ ...s, results: [item, ...s.results].slice(0, 50) })); setNotice(readingView.state === 'live' ? 'Measurement saved' : 'Last valid measurement saved'); } };
 const exportResult = async item => { try { await Share.share({ message: JSON.stringify({ ...item, mode: 'LIVE', reference: item.reference || { type: 'none' }, accuracy: 'unvalidated' }, null, 2) }); } catch (e) { setNotice(e.message); } };
 const startGenerator = async () => {
  setGenerating(true); setNotice(null);
  try {
   const rate = session.generatorRate === 1 ? 1 : 2;
   const result = native.generateStyled ? await native.generateStyled(rate, taiUtcSeconds, {...generatorOptions(session), ...generatorClockOptions(clockCheck, native.clockNow?.(), timeSettings.maxUncertaintyMs)}) : await native.generate(rate, taiUtcSeconds);
   setNotice(`Signal stopped · ${result.skippedDisplayFrames} skipped display frames · output timing estimated`);
  } catch (error) { setNotice(error.message); }
  finally { setGenerating(false); }
 };
 const pickGeneratorImage = async () => {
  setPickingImage(true); setNotice(null);
  try { const image = await native.pickGeneratorImage(); if (image) setSession(s => ({ ...s, generatorImageId: image.id })); }
  catch (error) { setNotice(error.message); }
  finally { setPickingImage(false); }
 };
 const startCalibration = () => { setSettings(false); setRunning(true); setFlow('aim'); setNotice(null); };
 return <SafeAreaView style={{ flex: 1, backgroundColor: bg }}>
  <StatusBar barStyle="light-content" />
  <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingHorizontal: 16, paddingTop: 8 }}>
   <View style={{ flexDirection: 'row', alignItems: 'center' }}><Image source={require('./assets/header-icon.png')} resizeMode="contain" style={{ width: 44, height: 44, marginRight: 12 }} /><View><Text style={{ color: ink, fontSize: 19, fontWeight: '600' }}>AVsync</Text><Text style={{ color: muted, fontSize: 11, letterSpacing: 1, marginTop: 2 }}>{flow ? 'CALIBRATION' : tab.toUpperCase()}</Text></View></View>
   <Pressable accessibilityLabel="Settings" onPress={() => setSettings(true)} style={round}><Ionicons name="settings-outline" size={24} color={ink} /></Pressable>
  </View>
  {flow === 'review' ? <CalibrationReview take={take} onCancel={() => { setFlow(null); setTake(null); }} onRepeat={() => { setTake(null); setFlow('aim'); }} onSave={index => { const value = makeCalibration(take, index, os, Date.now()); setSession(s => ({ ...s, calibration: value })); setReadingSnapshot(null); setTake(null); setFlow(null); }} />
   : tab === 'measure' || flow ? <View style={{ flex: 1 }}>
    <View style={{ flex: 1, backgroundColor: '#000', margin: 12, borderRadius: 12, overflow: 'hidden', justifyContent: 'center' }}>
     {!NativeCamera ? <Text style={{ ...text, padding: 20 }}>{Platform.OS === 'android' ? 'Live AV measurement is not available on Android yet. GNSS time is available in Settings.' : 'Install the new native build to enable high-frame-rate live capture.'}</Text>
      : cameraPermission?.granted && micPermission?.granted ? <NativeCamera ref={camera} active={running || Boolean(flow)} taiUtcOffset={taiUtcSeconds} onReport={report} style={{ flex: 1 }} />
      : <View style={{ padding: 20 }}><Text style={text}>Allow camera and microphone for live AV sync measurement.</Text><Button title="Allow access" onPress={ask} /></View>}
     {!flow && <View pointerEvents="none" style={{ position: 'absolute', left: 12, right: 12, top: 12, flexDirection: 'row', flexWrap: 'wrap', gap: 7 }}><SignalPill value={feedback.audio} icon="pulse-outline" /><SignalPill value={feedback.picture} icon="scan-outline" /></View>}
     {status.configuredFps > 0 && <Text style={{ position: 'absolute', left: 14, bottom: 12, color: ink, backgroundColor: '#0009', padding: 5, borderRadius: 5 }}>{status.configuredFps.toFixed(0)} fps camera · {(status.fps || 0).toFixed(0)} fps processed</Text>}
     {flow && <View style={{ position: 'absolute', top: 12, left: 12, right: 12, padding: 12, borderRadius: 8, backgroundColor: '#121417db' }}><Text style={text}>{flow === 'listening' ? 'Listening… Capture continues for 700 ms AFTER the sound.' : 'Aim at one visible clap or tap. Start, wait a second, then make the sound.'}</Text></View>}
    </View>
    {flow ? <View style={{ paddingHorizontal: 20, paddingBottom: 12 }}>{flow === 'aim' && <Button title="Start calibration" disabled={!NativeCamera || !cameraPermission?.granted || !micPermission?.granted} onPress={() => { setFlow('listening'); camera.current?.beginCalibration(); }} />}<Button title="Cancel" onPress={cancelCalibration} />{notice && <Text style={{ color: orange }}>{notice}</Text>}</View>
     : <View style={{ paddingHorizontal: 20, paddingBottom: 12 }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' }}><Text style={{ color: live && !calibration ? orange : ink, fontSize: 56 }}>{offsetText(live?.offset_ms)}</Text><Pressable accessibilityLabel="Clear reading" onPress={() => { setReadingSnapshot(null); camera.current?.resumeAnalysis(); }} style={round}><Ionicons name="close-outline" size={28} color={ink} /></Pressable></View>
      <Text style={{ color: ink, fontSize: 20, marginTop: 4 }}>{measureSentence(live?.offset_ms)}</Text>
      {live && <Text accessibilityRole="text" style={{ color: readingView.state === 'live' ? ink : orange, marginTop: 6, fontSize: 12 }}>{readingView.label} · {live.eventCount} pairs · spread {Number.isFinite(live.spread_ms) ? live.spread_ms.toFixed(1) + ' ms' : 'unknown'}</Text>}
      <Text style={{ color: muted, marginTop: 12 }}>Quality: {live?.quality || '—'} · Calibration: {calibration ? offsetText(calibration.offset_ms) : 'missing (0 ms)'} · Uncertainty: unknown</Text>
      <Text style={{ color: notice ? orange : muted, marginTop: 8, fontSize: 12 }}>{notice || (status.kind === 'reading' ? `${status.eventCount} matched events · live updates` : status.message || 'Looking for test signal…')}</Text>
      <LatencyPanel details={latencyHeld ? live?.latency : status.latency} values={latency} sampleNote={latencyHeld ? `Last paired sample · ${readingView.label}` : null} hint={!running ? 'Analysis paused' : age > 1600 ? 'Waiting for capture updates' : latencyHint(status.latency)} clock={clockText} onCheck={runClockCheck} checking={clockBusy} error={clockError} />
      <View style={{ flexDirection: 'row', gap: 10 }}><View style={{ flex: 1 }}><Button title={running ? 'Pause' : 'Live analysis'} onPress={() => setRunning(v => !v)} /></View><View style={{ flex: 1 }}><Button title="Save result" disabled={!live} onPress={save} /></View></View>
     </View>}
   </View> : tab === 'generator' ? <GeneratorPanel session={session} onChange={(key, value) => setSession(s => ({ ...s, [key]: value }))} image={generatorImage} picking={pickingImage} onPickImage={pickGeneratorImage} rate={session.generatorRate === 1 ? 1 : 2} onRate={value => setSession(s => ({ ...s, generatorRate: value }))} onStart={startGenerator} busy={generating} clock={clockText} checking={clockBusy} clockError={clockError} onCheck={runClockCheck} notice={notice} /> : <ScrollView contentContainerStyle={{ padding: 20 }}>{session.results.length ? session.results.map(item => <Pressable key={item.id} onPress={() => exportResult(item)} style={{ paddingVertical: 16, borderBottomWidth: 1, borderBottomColor: '#2c3540' }}><Text style={{ color: item.calibrated ? ink : orange, fontSize: 22 }}>{offsetText(item.offset_ms)}</Text><Text style={{ color: muted }}>{formatResultWhen(item.at)} · {item.calibrated ? 'Calibrated' : '0 ms correction'} · tap to export</Text></Pressable>) : <Text style={{ color: muted }}>No saved results</Text>}</ScrollView>}
  {!flow && <View style={{ flexDirection: 'row', borderTopWidth: 1, borderTopColor: '#2c3540' }}>{['measure', 'generator', 'results'].map(value => <Pressable key={value} onPress={() => { setTab(value); setNotice(null); }} style={{ flex: 1, paddingVertical: 14, alignItems: 'center' }}><Text style={{ color: tab === value ? ink : muted, fontWeight: tab === value ? '700' : '400' }}>{value === 'measure' ? 'Measure' : value === 'generator' ? 'Generate' : 'Results'}</Text></Pressable>)}</View>}
  <Modal visible={settings} transparent animationType="fade" onRequestClose={() => setSettings(false)}>
   <View style={{ flex: 1, backgroundColor: '#0007', justifyContent: 'flex-end' }}>
    <Pressable accessibilityRole="button" accessibilityLabel="Close settings" onPress={() => setSettings(false)} style={{ position: 'absolute', top: 0, right: 0, bottom: 0, left: 0 }} />
    <View style={{ maxHeight: '90%', backgroundColor: '#1c2128', borderTopLeftRadius: 16, borderTopRightRadius: 16 }}>
     <ScrollView keyboardShouldPersistTaps="handled" contentContainerStyle={{ padding: 20, paddingBottom: 36 }}>
      <Text accessibilityRole="header" style={{ ...text, fontSize: 20 }}>Settings</Text>
      <View style={{ backgroundColor: bg, borderRadius: 10, padding: 14, marginTop: 16 }}>
       <Text style={{ color: muted, fontSize: 12 }}>CALIBRATION</Text>
       <Text style={{ color: correction.state === 'active' ? ink : orange, fontSize: 21, marginTop: 5 }}>Current correction {offsetText(correction.appliedOffsetMs)}</Text>
       <Text style={{ color: muted, marginTop: 6, lineHeight: 19 }}>{correction.state === 'active' ? 'Applied to live AV sync measurements.' : correction.state === 'inactive' ? `Saved correction ${offsetText(correction.storedOffsetMs)} is no longer valid. Repeat calibration to use it again.` : 'No calibration saved. Measurements use 0 ms correction.'}</Text>
       {correction.savedAt !== null && <Text style={{ color: muted, fontSize: 12, marginTop: 6 }}>Saved {formatResultWhen(correction.savedAt)}</Text>}
      </View>
      <Text style={{ color: muted, fontSize: 12, marginTop: 12 }}>Latency uses matching live source TAI timestamps and the selected phone clock. Capture and source clock accuracy are unknown. Clock synchronization corrects UTC mapping only; it does not calibrate capture or physical source output.</Text><View style={{ flexDirection: 'row', alignItems: 'center', marginVertical: 10 }}><Text style={{ color: ink, flex: 1 }}>Source TAI−UTC (seconds)</Text><TextInput accessibilityLabel="Source TAI minus UTC seconds" keyboardType="number-pad" value={taiDraft} onChangeText={value => { if (/^\d{0,3}$/.test(value)) setTaiDraft(value); }} style={{ color: ink, backgroundColor: '#121417', borderRadius: 6, padding: 8, minWidth: 48, textAlign: 'center' }} /><Pressable accessibilityRole="button" disabled={!taiDraft || Number(taiDraft) > 100} onPress={() => { setSession(previous => ({ ...previous, taiUtcSeconds: Number(taiDraft) })); setStatus(previous => ({ ...previous, latency: { state: 'waiting_for_live_qr' } })); }} style={{ padding: 8 }}><Text style={{ color: !taiDraft || Number(taiDraft) > 100 ? muted : orange }}>Apply</Text></Pressable></View><View style={{marginVertical: 12}}>
       <Text style={text}>Time reference · {Platform.OS === 'android' ? 'GNSS' : 'NTP'}</Text>
       <Text style={{color: muted,fontSize: 11,marginTop: 4}}>{clockText}</Text>
       {Platform.OS === 'ios' && <><Text style={{color:muted,marginTop:8}}>NTP server</Text><TextInput accessibilityLabel="NTP server" autoCapitalize="none" autoCorrect={false} value={clockServerDraft} onChangeText={setClockServerDraft} style={{...text,backgroundColor:'#121417',padding:8}} /></>}
       {Platform.OS === 'android' && <Text style={{color:muted,fontSize:11}}>Requires precise location permission, raw GNSS clock support and a clear sky view. Coordinates are not stored. Android live AV capture is not yet available.</Text>}
       <Text style={{color:muted,marginTop:8}}>Maximum clock uncertainty (ms)</Text><TextInput accessibilityLabel="Maximum clock uncertainty milliseconds" keyboardType="numeric" value={clockLimitDraft} onChangeText={setClockLimitDraft} style={{...text,backgroundColor:'#121417',padding:8}} />
       <Text style={{color:muted,marginTop:8}}>Source emit offsets · video / audio (ms)</Text>
       <Text style={{color:muted,fontSize:11}}>Optional: copy emit_offsets from the generator. Empty means relative to the source timestamp. Keep these values matched to the source.</Text>
       <View style={{flexDirection:'row',gap:8}}>{[[emitVideoDraft,setEmitVideoDraft,'Video source emit offset'],[emitAudioDraft,setEmitAudioDraft,'Audio source emit offset']].map(([value,setValue,label]) => <TextInput key={label} accessibilityLabel={label} keyboardType="numbers-and-punctuation" value={value} onChangeText={setValue} placeholder="unknown" placeholderTextColor={muted} style={{...text,flex:1,backgroundColor:'#121417',padding:8}} />)}</View>
       <Button title="Apply time settings" onPress={applyClockSettings} />
       <Button title={clockBusy ? 'Synchronizing…' : `Synchronize ${Platform.OS === 'android' ? 'GNSS' : 'NTP'} clock`} disabled={clockBusy || timeDraftChanged} onPress={runClockCheck} />
       {timeDraftChanged && <Text style={{color:orange,fontSize:11}}>Apply time settings before synchronizing.</Text>}
       {clockError && <Text style={{color:orange,fontSize:12}}>{clockError}</Text>}
       <Text style={{color:muted,fontSize:11,marginTop:8}}>Only when requested · reference expires after 5 min · no system clock changes. ± covers the estimated clock contribution; capture/source timing remains unvalidated.</Text>
      </View>{[['Repeat calibration', startCalibration], ['Clear saved calibration (use 0 ms)', () => { setSession(s => ({ ...s, calibration: null })); setReadingSnapshot(null); setSettings(false); }], ['Clear results', () => setSession(s => ({ ...s, results: [] }))], ['Privacy policy & support', () => { setSettings(false); Linking.openURL('https://leeo86.github.io/mxl-avsync/privacy-en.html').catch(() => setNotice('Open https://leeo86.github.io/mxl-avsync/privacy-en.html for privacy and support.')); }], ['Close', () => setSettings(false)]].map(([label, onPress]) => <Pressable key={label} onPress={onPress} style={{ paddingVertical: 14, borderTopWidth: 1, borderTopColor: '#2c3540' }}><Text style={text}>{label}</Text></Pressable>)}
     </ScrollView>
    </View>
   </View>
  </Modal>
 </SafeAreaView>;
}
