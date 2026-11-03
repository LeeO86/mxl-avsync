import { useEffect, useMemo, useRef, useState } from 'react';
import { AppState, Image, Linking, Modal, Platform, Pressable, SafeAreaView, Share, StatusBar, Text, View } from 'react-native';
import { useCameraPermissions, useMicrophonePermissions } from 'expo-camera';
import { requireNativeViewManager } from 'expo-modules-core';
import Ionicons from '@expo/vector-icons/Ionicons';
import native from './modules/avsync';
import CalibrationReview from './components/CalibrationReview';
import Measure from './components/Measure';
import Generate from './components/Generate';
import Results from './components/Results';
import { Debug, Settings } from './components/Settings';
import { Button, colors, small, text } from './components/ui';
import { liveView, offsetText, savedResult, formatWhen } from './src/live.mjs';
import { clockCheckDetails, clockSettings, generatorClockOptions, referenceAt, synchronizeClock, CLOCK_MAX_AGE_MS } from './src/time-reference.mjs';
import { calibrationSummary, makeCalibration, validCalibration } from './src/workflow.mjs';
import { generatorOptions } from './src/generator.mjs';
import { screenAwakeEnabled } from './src/screen-awake.mjs';

const NativeCamera = native?.supportsLiveCapture?.() ? requireNativeViewManager('Avsync') : null;
const RESYNC_MS = CLOCK_MAX_AGE_MS - 60000; // renew the clock reference a minute before it expires
const TABS = [['measure', 'Measure', 'speedometer-outline'], ['generate', 'Signal', 'tv-outline'], ['results', 'Results', 'list-outline']];

function loadSession() {
 try { return JSON.parse(native?.loadState?.() || 'null') || { calibration: null, results: [] }; } catch { return { calibration: null, results: [] }; }
}

export default function App() {
 const [session, setSession] = useState(loadSession);
 const update = patch => setSession(previous => ({ ...previous, ...(typeof patch === 'function' ? patch(previous) : patch) }));
 useEffect(() => { native?.saveState?.(JSON.stringify(session)); }, [session]);

 const [tab, setTab] = useState('measure');
 const [settings, setSettings] = useState(false), [debug, setDebug] = useState(false);
 const [notice, setNotice] = useState(null);
 const [foreground, setForeground] = useState(AppState.currentState === 'active');
 useEffect(() => { const s = AppState.addEventListener('change', value => setForeground(value === 'active')); return () => s.remove(); }, []);
 const [cameraPermission, requestCamera] = useCameraPermissions(), [micPermission, requestMic] = useMicrophonePermissions();
 const granted = cameraPermission?.granted && micPermission?.granted;

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
  if (age > RESYNC_MS && !syncBusy.current) syncClock();
 }, [tick]);
 const limits = useMemo(() => { try { return clockSettings(session); } catch { return clockSettings({}); } }, [session.ntpServer, session.clockMaxUncertaintyMs]);
 const clockNow = referenceAt(reference, native?.clockNow?.(), limits.maxUncertaintyMs);
 const clockLabel = syncing && !reference ? 'Synchronizing…' : clockNow.state === 'estimated' ? `NTP ±${clockNow.uncertaintyMs.toFixed(1)} ms`
  : clockError ? 'Not synchronized · no latency' : clockNow.state === 'uncertain' ? 'Too uncertain · no latency' : 'Not synchronized · no latency';

 // Calibration.
 const os = native?.deviceVersion?.() || String(Platform.Version);
 const calibration = validCalibration(session.calibration, os, Date.now(), report?.captureContext);
 const calibrationState = calibrationSummary(session.calibration, os, Date.now(), report?.captureContext);
 const calibrationLabel = calibrationState.state === 'active' ? `${offsetText(calibrationState.appliedOffsetMs)} · ${formatWhen(calibrationState.savedAt)}`
  : calibrationState.state === 'inactive' ? 'Not valid for this camera mode' : 'Not calibrated';
 const [flow, setFlow] = useState(null), [take, setTake] = useState(null);

 const onReport = event => {
  const value = event.nativeEvent;
  if (value.kind === 'calibration') { setTake(value); setFlow('review'); return; }
  if (value.kind === 'error') { setNotice(value.message); if (flow === 'listening') setFlow('aim'); return; }
  setReport(value); setReportAt(performance.now());
 };
 const view = liveView(report, { reference, maxUncertaintyMs: limits.maxUncertaintyMs, calibrationMs: calibration?.offset_ms ?? 0, ageMs: reportAt ? performance.now() - reportAt : 0, held });
 useEffect(() => { if (view.reading?.fresh) setHeld(view.reading); }, [report]);

 const restart = () => { setReport(null); setHeld(null); setNotice(null); camera.current?.resumeAnalysis?.(); };
 const save = () => {
  const item = savedResult(view.reading, { calibration });
  if (!item) return;
  update(previous => ({ results: [item, ...(previous.results || [])].slice(0, 100) }));
  setNotice(item.held ? 'Last reading saved' : 'Saved');
 };
 const share = async value => { try { await Share.share({ message: JSON.stringify(value, null, 2) }); } catch (error) { setNotice(error.message); } };

 const startCalibration = () => { setSettings(false); setTab('measure'); setNotice(null); setFlow('aim'); };
 const finishCalibration = (index, keep) => {
  try { if (keep) update({ calibration: makeCalibration(take, index, os, Date.now()) }); setNotice(keep ? 'Calibration saved' : null); }
  catch (error) { setNotice(error.message); }
  setTake(null); setFlow(null); restart();
 };

 // Test signal.
 const [generating, setGenerating] = useState(false), [picking, setPicking] = useState(false);
 const image = useMemo(() => session.generatorImageId && native?.generatorImageInfo ? native.generatorImageInfo(session.generatorImageId) : null, [session.generatorImageId]);
 const startSignal = async () => {
  setGenerating(true); setNotice(null);
  try {
   const rate = session.generatorRate === 1 ? 1 : 2;
   const options = { ...generatorOptions(session), keepAwake: screenAwakeEnabled(session) ? 'true' : 'false', ...generatorClockOptions(reference, native.clockNow?.(), limits.maxUncertaintyMs) };
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
 const awake = screenAwakeEnabled(session) && measuring && granted;
 useEffect(() => { native?.setScreenAwake?.(awake); return () => native?.setScreenAwake?.(false); }, [awake]);
 const applyDebug = patch => {
  try { clockSettings({ ...session, ...patch }); update(patch); if ('captureFps' in patch) restart(); }
  catch (error) { setNotice(error.message); }
 };

 const cameraView = !NativeCamera
  ? <Text style={{ ...text, padding: 20 }}>This build has no live camera measurement.</Text>
  : granted
   ? <NativeCamera ref={camera} active={measuring && flow !== 'review'} captureFps={Number(session.captureFps) || 0} taiUtcOffset={session.taiUtcSeconds ?? 37} onReport={onReport} style={{ flex: 1 }} />
   : <View style={{ padding: 20 }}><Text style={text}>AVsync needs the camera and the microphone to measure.</Text><Button primary title="Allow access" onPress={async () => { await requestCamera(); await requestMic(); }} /></View>;

 return <SafeAreaView style={{ flex: 1, backgroundColor: colors.bg }}>
  <StatusBar barStyle="light-content" />
  <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingHorizontal: 16, paddingTop: 6 }}>
   <View style={{ flexDirection: 'row', alignItems: 'center' }}>
    <Image source={require('./assets/header-icon.png')} resizeMode="contain" style={{ width: 36, height: 36, marginRight: 10 }} />
    <Text style={{ ...text, fontSize: 19, fontWeight: '600' }}>AVsync</Text>
   </View>
   <Pressable accessibilityRole="button" accessibilityLabel="Settings" onPress={() => setSettings(true)} style={{ padding: 8 }}>
    <Ionicons name="settings-outline" size={24} color={colors.ink} />
   </Pressable>
  </View>
  <View style={{ flex: 1, display: tab === 'measure' ? 'flex' : 'none' }}>
   <Measure camera={cameraView} view={view} flow={flow} calibrated={Boolean(calibration)} notice={notice}
    onSave={save} onRestart={restart}
    onStartCalibration={() => { setFlow('listening'); setNotice(null); camera.current?.beginCalibration?.(); }}
    onCancelCalibration={() => { setFlow(null); restart(); }} />
  </View>
  {tab === 'generate' && <Generate session={session} onChange={(key, value) => update({ [key]: value })} image={image} busy={generating} picking={picking}
   onPickImage={native?.pickGeneratorImage ? pickImage : null} onStart={startSignal} supported={Boolean(native?.supportsGenerator?.())} notice={notice} />}
  {tab === 'results' && <Results results={session.results || []} onShare={share} />}
  {!flow && <View style={{ flexDirection: 'row', borderTopWidth: 1, borderTopColor: colors.line }}>
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
  <Settings visible={settings} onClose={() => setSettings(false)} calibration={calibrationLabel} clock={clockLabel} onCalibrate={startCalibration}
   onSyncClock={syncClock} syncing={syncing} keepAwake={screenAwakeEnabled(session)} onKeepAwake={value => update({ keepScreenAwake: value })}
   onPrivacy={() => Linking.openURL('https://leeo86.github.io/mxl-avsync/privacy-en.html').catch(() => {})}
   onDebug={() => { setSettings(false); setTimeout(() => setDebug(true), 400); }} />
  <Debug visible={debug} onClose={() => setDebug(false)} session={session} onChange={applyDebug} report={report}
   clockDetails={reference ? clockCheckDetails(reference) : clockError}
   onShare={() => share({ report, reference, calibration: session.calibration, results: (session.results || []).slice(0, 5) })}
   onClearResults={() => update({ results: [] })} onClearCalibration={() => { update({ calibration: null }); setNotice('Calibration cleared'); }} />
 </SafeAreaView>;
}
