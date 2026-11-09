import { useEffect, useMemo, useRef, useState } from 'react';
import { AppState, Image, Linking, Modal, Platform, Pressable, SafeAreaView, Share, StatusBar, Text, View } from 'react-native';
import { useCameraPermissions, useMicrophonePermissions } from 'expo-camera';
import { requireNativeViewManager } from 'expo-modules-core';
import Ionicons from '@expo/vector-icons/Ionicons';
import { useFonts } from 'expo-font';
import native from './modules/avsync';
import CalibrationReview from './components/CalibrationReview';
import CalibratorWizard from './components/CalibratorWizard';
import Measure from './components/Measure';
import Generate from './components/Generate';
import Results from './components/Results';
import { Debug, Settings, Sheet } from './components/Settings';
import { Button, colors, small, text } from './components/ui';
import { liveView, offsetText, savedResult, formatWhen, clockText } from './src/live.mjs';
import { clockCheckDetails, clockSettings, generatorClockOptions, referenceAt, synchronizeClock, CLOCK_MAX_AGE_MS } from './src/time-reference.mjs';
import { calibrationSummary, makeCalibration, validCalibration } from './src/workflow.mjs';
import { generatorOptions } from './src/generator.mjs';
import { makePulseCalibration, makeSenderCalibration, senderCalibrationLabel, senderCalibrationOptions } from './src/calibrator.mjs';
import { screenAwakeEnabled } from './src/screen-awake.mjs';
import { addResult, liveDelta, loadSessionState, setNickname, toggleReference } from './src/results.mjs';

const NativeCamera = native?.supportsLiveCapture?.() ? requireNativeViewManager('Avsync') : null;
const RESYNC_MS = CLOCK_MAX_AGE_MS - 60000; // renew the clock reference a minute before it expires
const UNCERTAIN_RESYNC_MS = 30000; // or sooner, at most every 30 s, once the holdover passes the limit
const TABS = [['measure', 'Measure', 'speedometer-outline'], ['generate', 'Signal', 'tv-outline'], ['results', 'Results', 'list-outline']];

function loadSession() {
 try { return loadSessionState(native?.loadState?.()); } catch { return loadSessionState(null); }
}

export default function App() {
 useFonts(Ionicons.font); // the Android release showed no icons until the font was loaded here
 const [session, setSession] = useState(loadSession);
 const update = patch => setSession(previous => ({ ...previous, ...(typeof patch === 'function' ? patch(previous) : patch) }));
 useEffect(() => { native?.saveState?.(JSON.stringify(session)); }, [session]);

 const [tab, setTab] = useState('measure');
 const [sheet, setSheet] = useState(null); // null, 'settings' or 'debug'
 const [notice, setNotice] = useState(null);
 const [foreground, setForeground] = useState(AppState.currentState === 'active');
 useEffect(() => { const s = AppState.addEventListener('change', value => setForeground(value === 'active')); return () => s.remove(); }, []);
 const [cameraPermission, requestCamera] = useCameraPermissions(), [micPermission, requestMic] = useMicrophonePermissions();
 const granted = Boolean(cameraPermission?.granted && micPermission?.granted);

 // Live reports.
 const camera = useRef(null);
 const [report, setReport] = useState(null), [reportAt, setReportAt] = useState(0);
 const [held, setHeld] = useState(null);
 const [tick, setTick] = useState(0);
 useEffect(() => { const t = setInterval(() => setTick(value => value + 1), 500); return () => clearInterval(t); }, []);

 // Clock: synchronize automatically while the app is in front, and renew before expiry.
 const [reference, setReference] = useState(null), [syncing, setSyncing] = useState(false), [clockError, setClockError] = useState(null);
 const syncBusy = useRef(false);
 const syncClock = async () => {
  if (syncBusy.current || AppState.currentState !== 'active') return;
  syncBusy.current = true; setSyncing(true);
  try { setReference(await synchronizeClock(native, session)); setClockError(null); }
  catch (error) { setClockError(error.message); }
  finally { syncBusy.current = false; setSyncing(false); }
 };
 useEffect(() => { if (!foreground) setReference(null); else syncClock(); }, [foreground, session.ntpServer]);
 useEffect(() => {
  if (!foreground) return;
  const age = reference ? performance.now() - reference.receivedJsMonoMs + reference.ageAtReceiptMs : Infinity;
  if ((age > RESYNC_MS || (clockNow.state === 'uncertain' && age > UNCERTAIN_RESYNC_MS)) && !syncBusy.current) syncClock();
 }, [tick]);
 const limits = useMemo(() => { try { return clockSettings(session); } catch { return clockSettings({}); } }, [session.ntpServer, session.clockMaxUncertaintyMs]);
 const clockNow = referenceAt(reference, native?.clockNow?.(), limits.maxUncertaintyMs);
 const clockLabel = syncing && !reference ? 'Synchronizing…' : clockNow.state === 'estimated' ? clockText(clockNow)
  : clockError ? 'Not synchronized · no latency' : clockNow.state === 'uncertain' ? 'Too uncertain · no latency' : 'Not synchronized · no latency';

 // Calibration.
 const os = native?.deviceVersion?.() || String(Platform.Version);
 const calibration = validCalibration(session.calibration, os, Date.now(), report?.captureContext);
 const calibrationState = calibrationSummary(session.calibration, os, Date.now(), report?.captureContext);
 const calibrationLabel = calibrationState.state === 'active' ? `${offsetText(calibrationState.appliedOffsetMs)} · ${formatWhen(calibrationState.savedAt)}`
  : calibrationState.state === 'inactive' ? 'Not valid for this camera mode' : 'Not calibrated';
 const senderLabel = senderCalibrationLabel(session.senderCalibration, os, Date.now(), formatWhen);
 const [flow, setFlow] = useState(null), [take, setTake] = useState(null);
 // The calibrator wizard: null or {step, progress, result, error} (components/CalibratorWizard.js).
 const [cal, setCal] = useState(null);

 const onReport = event => {
  const value = event.nativeEvent;
  if (value.kind === 'calibrator-progress') { setCal(c => c && { ...c, progress: value }); return; }
  if (value.kind === 'calibrator') { setCal(c => c && { step: c.step.replace(/-run$/, '-done'), result: value }); return; }
  if (value.kind === 'error' && cal?.step.endsWith('-run')) { setCal(c => c && { step: c.step.replace(/-run$/, ''), error: value.message }); return; }
  if (value.kind === 'calibration') { setTake(value); setFlow('review'); return; }
  if (value.kind === 'error') { setNotice(value.message); if (flow === 'listening') setFlow('aim'); return; }
  setReport(value); setReportAt(performance.now());
 };
 const view = liveView(report, { reference, maxUncertaintyMs: limits.maxUncertaintyMs, calibrationMs: calibration?.offset_ms ?? 0, ageMs: reportAt ? performance.now() - reportAt : 0, held });
 useEffect(() => { if (view.reading?.fresh) setHeld(view.reading); }, [report]);
 const delta = liveDelta(view.reading, { senders: session.senders, results: session.results, captureContext: report?.captureContext ?? null, calibrationMs: calibration?.offset_ms ?? null });

 const restart = () => { setReport(null); setHeld(null); setNotice(null); camera.current?.resumeAnalysis?.(); };
 // Tap to focus: like restart, but the last reading stays greyed (held) while the camera refocuses.
 const refocus = (x, y) => { setReport(null); setNotice(null); camera.current?.focusAt?.(x, y); };
 const save = () => {
  const item = savedResult(view.reading, { calibration, captureContext: report?.captureContext ?? null });
  if (!item) return;
  update(previous => ({ results: addResult(previous.results, item, previous.senders) }));
  setNotice(item.held ? 'Last reading saved' : 'Saved');
 };
 const share = async value => { try { await Share.share({ message: JSON.stringify(value, null, 2) }); } catch (error) { setNotice(error.message); } };

 const startCalibration = () => { setSheet(null); setTab('measure'); setNotice(null); setFlow('aim'); };
 const finishCalibration = (index, keep) => {
  try { if (keep) update({ calibration: makeCalibration(take, index, os, Date.now()) }); setNotice(keep ? 'Calibration saved' : null); }
  catch (error) { setNotice(error.message); }
  setTake(null); setFlow(null); restart();
 };

 // With calibrator: step 1 is a camera take (this phone's camera vs microphone), step 2 the
 // generator's calibration pattern (this phone's screen vs speaker, sent in the AVS4 QR).
 const supportsCalibrator = Platform.OS === 'ios' && Boolean(NativeCamera) && Boolean(native?.supportsGenerator?.());
 const startCalibrator = () => { setSheet(null); setTab('measure'); setNotice(null); setFlow(null); setCal({ step: 'plug' }); };
 const closeCalibrator = message => { setCal(null); restart(); setNotice(message ?? null); };
 const calibratorStart = async base => {
  if (base !== 'display') { setCal({ step: `${base}-run`, progress: null }); camera.current?.beginCalibrator?.(base === 'tv' ? 'display' : 'camera'); return; }
  setCal({ step: 'display-run' });
  try {
   const result = await native.generateStyled(2, session.taiUtcSeconds ?? 37, { mode: 'calibration', keepAwake: 'true' });
   setCal(result?.calibration ? { step: 'display-done', result: result.calibration } : { step: 'display', error: result?.calibrationError || null });
  } catch (error) { setCal({ step: 'display', error: error.message }); }
 };
 const calibratorSave = base => {
  try {
   if (base === 'camera') { update({ calibration: makePulseCalibration(cal.result, os, Date.now()) }); setCal({ step: 'display' }); }
   else { update({ senderCalibration: makeSenderCalibration({ calibration: cal.result }, os, Date.now()) }); closeCalibrator('Sender calibration saved'); }
  } catch (error) { setCal(c => ({ ...c, error: error.message })); }
 };

 // Test signal.
 const [generating, setGenerating] = useState(false), [picking, setPicking] = useState(false);
 const image = useMemo(() => session.generatorImageId && native?.generatorImageInfo ? native.generatorImageInfo(session.generatorImageId) : null, [session.generatorImageId]);
 const startSignal = async () => {
  setGenerating(true); setNotice(null);
  try {
   const rate = session.generatorRate === 1 ? 1 : 2;
   const options = { ...generatorOptions(session), keepAwake: screenAwakeEnabled(session) ? 'true' : 'false', ...generatorClockOptions(reference, native.clockNow?.(), limits.maxUncertaintyMs),
    ...senderCalibrationOptions(session.senderCalibration, os, Date.now()) };
   await (native.generateStyled || native.generate)(rate, session.taiUtcSeconds ?? 37, options);
  } catch (error) { setNotice(error.message); }
  finally { setGenerating(false); }
 };
 const pickImage = async () => {
  setPicking(true);
  try { const picked = await native.pickGeneratorImage(); if (picked) update({ generatorImageId: picked.id }); }
  catch (error) { setNotice(error.message); }
  finally { setPicking(false); }
 };

 const measuring = tab === 'measure' && foreground;
 const awake = Boolean(screenAwakeEnabled(session) && measuring && granted);
 useEffect(() => { native?.setScreenAwake?.(awake); return () => native?.setScreenAwake?.(false); }, [awake]);
 const applyDebug = patch => {
  try { clockSettings({ ...session, ...patch }); update(patch); if ('captureFps' in patch) restart(); }
  catch (error) { setNotice(error.message); }
 };

 const cameraView = !NativeCamera
  ? <Text style={{ ...text, padding: 20 }}>This build has no live camera measurement.</Text>
  : granted
   ? <NativeCamera ref={camera} active={measuring && flow !== 'review' && cal?.step !== 'display-run'} captureFps={Number(session.captureFps) || 0} taiUtcOffset={session.taiUtcSeconds ?? 37} onReport={onReport} style={{ flex: 1 }} />
   : <View style={{ padding: 20 }}><Text style={text}>AVsync needs the camera and the microphone to measure.</Text><Button primary title="Allow access" onPress={async () => { await requestCamera(); await requestMic(); }} /></View>;

 return <SafeAreaView style={{ flex: 1, backgroundColor: colors.bg }}>
  <StatusBar barStyle="light-content" backgroundColor={colors.bg} />
  <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingHorizontal: 16, paddingTop: 6 }}>
   <View style={{ flexDirection: 'row', alignItems: 'center' }}>
    <Image source={require('./assets/header-icon.png')} resizeMode="contain" style={{ width: 36, height: 36, marginRight: 10 }} />
    <Text style={{ ...text, fontSize: 19, fontWeight: '600' }}>AVsync</Text>
   </View>
   <Pressable accessibilityRole="button" accessibilityLabel="Settings" onPress={() => setSheet('settings')} style={{ padding: 8 }}>
    <Ionicons name="settings-outline" size={24} color={colors.ink} />
   </Pressable>
  </View>
  <View style={{ flex: 1, display: tab === 'measure' ? 'flex' : 'none' }}>
   <Measure camera={cameraView} view={view} flow={flow} calibrated={Boolean(calibration)} delta={delta} notice={notice}
    onSave={save} onRestart={restart} onFocus={NativeCamera && granted ? refocus : null}
    onStartCalibration={() => { setFlow('listening'); setNotice(null); camera.current?.beginCalibration?.(); }}
    onCancelCalibration={() => { setFlow(null); restart(); }} />
   {cal && <View style={{ position: 'absolute', top: 0, right: 0, bottom: 0, left: 0, backgroundColor: colors.bg }}>
    <CalibratorWizard step={cal.step} progress={cal.progress} result={cal.result} error={cal.error} onNext={step => setCal({ step })}
     onStart={calibratorStart} onSave={calibratorSave} onRepeat={base => setCal({ step: base })} onClose={() => closeCalibrator()} />
   </View>}
  </View>
  {tab === 'generate' && <Generate session={session} onChange={(key, value) => update({ [key]: value })} image={image} busy={generating} picking={picking}
   onPickImage={native?.pickGeneratorImage ? pickImage : null} onStart={startSignal} supported={Boolean(native?.supportsGenerator?.())} notice={notice} />}
  {tab === 'results' && <Results results={session.results || []} senders={session.senders} onShare={share}
   onToggleReference={item => update(previous => ({ senders: toggleReference(previous.senders, item) }))}
   onRename={(key, name) => update(previous => ({ senders: setNickname(previous.senders, key, name) }))} />}
  {!flow && !cal && <View style={{ flexDirection: 'row', borderTopWidth: 1, borderTopColor: colors.line }}>
   {TABS.map(([key, label, icon]) => <Pressable key={key} accessibilityRole="tab" accessibilityState={{ selected: tab === key }} onPress={() => { setTab(key); setNotice(null); }} style={{ flex: 1, paddingVertical: 10, alignItems: 'center' }}>
    <Ionicons name={icon} size={22} color={tab === key ? colors.accent : colors.muted} />
    <Text style={{ ...small, color: tab === key ? colors.ink : colors.muted }}>{label}</Text>
   </Pressable>)}
  </View>}
  <Modal visible={flow === 'review' && Boolean(take)} animationType="slide" onRequestClose={() => finishCalibration(0, false)}>
   <SafeAreaView style={{ flex: 1, backgroundColor: colors.bg }}>
    {take && <CalibrationReview take={take} onSave={index => finishCalibration(index, true)} onRepeat={() => { setTake(null); setFlow('aim'); }} onCancel={() => finishCalibration(0, false)} />}
   </SafeAreaView>
  </Modal>
  <Sheet visible={sheet !== null} onClose={() => setSheet(null)}>
   {sheet === 'debug'
    ? <Debug onBack={() => setSheet('settings')} session={session} onChange={applyDebug} report={report}
     clockDetails={reference ? clockCheckDetails(reference) : clockError}
     onShare={() => share({ report, reference, calibration: session.calibration, results: (session.results || []).slice(0, 5) })}
     onClearResults={() => update({ results: [] })} onClearCalibration={() => { update({ calibration: null, senderCalibration: null }); setNotice('Analyser and sender calibration cleared'); }} />
    : <Settings onClose={() => setSheet(null)} calibration={calibrationLabel} sender={senderLabel} clock={clockLabel} onCalibrate={startCalibration}
     onCalibrator={supportsCalibrator ? startCalibrator : null}
     onSyncClock={syncClock} syncing={syncing} keepAwake={screenAwakeEnabled(session)} onKeepAwake={value => update({ keepScreenAwake: value })}
     onPrivacy={() => Linking.openURL('https://leeo86.github.io/mxl-avsync/privacy-en.html').catch(() => {})}
     onDebug={() => setSheet('debug')} />}
  </Sheet>
 </SafeAreaView>;
}
