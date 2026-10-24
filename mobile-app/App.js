import { useEffect, useMemo, useState } from 'react';
import { Pressable, SafeAreaView, Text, View } from 'react-native';
import { AIM_TEXT, encodeWav, formatResultWhen, measureSentence, offsetText, selectHighestFrameRate, spikeBucket, statusLine, waveformPeaks } from './src/measure.mjs';
import {
  clearCalibration,
  clearLive,
  clearResults,
  completeMeasurement,
  createSession,
  saveCalibration,
  setLive,
} from './src/session.mjs';
import { appendAudio, appendFrame, createRecording, selectedOffsetMs, spikeAbsoluteMs, stepSelection } from './src/wizard.mjs';
function loadNative() {
  try {
    return require('./modules/avsync').default;
  } catch (err) {
    return null;
  }
}

function loadCamera() {
  try {
    return require('expo-camera');
  } catch (err) {
    return null;
  }
}

const METER_HZ = 25;

function bytesToBase64(bytes) {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  let out = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const a = bytes[i];
    const b = i + 1 < bytes.length ? bytes[i + 1] : 0;
    const c = i + 2 < bytes.length ? bytes[i + 2] : 0;
    const triple = (a << 16) | (b << 8) | c;
    out += chars[(triple >> 18) & 63];
    out += chars[(triple >> 12) & 63];
    out += i + 1 < bytes.length ? chars[(triple >> 6) & 63] : '=';
    out += i + 2 < bytes.length ? chars[triple & 63] : '=';
  }
  return out;
}

async function startMicrophone(setRecording) {
  try {
    const { Audio } = require('expo-av');
    const permission = await Audio.requestPermissionsAsync();
    if (!permission.granted) return null;
    await Audio.setAudioModeAsync({ allowsRecordingIOS: true, playsInSilentModeIOS: true });
    const mic = new Audio.Recording();
    await mic.prepareToRecordAsync({
      isMeteringEnabled: true,
      android: { extension: '.m4a', outputFormat: 2, audioEncoder: 3, sampleRate: 44100, numberOfChannels: 1, bitRate: 128000 },
      ios: { extension: '.m4a', outputFormat: 'aac ', audioQuality: 96, sampleRate: 44100, numberOfChannels: 1, bitRate: 128000 },
      web: {},
    });
    mic.setProgressUpdateInterval(1000 / METER_HZ);
    mic.setOnRecordingStatusUpdate((status) => {
      if (!status.isRecording || status.metering == null) return;
      const linear = Math.max(0, Math.min(1, 10 ** (status.metering / 20)));
      setRecording((current) => (current && current.step === 'recording' ? appendAudio(current, [linear]) : current));
    });
    await mic.startAsync();
    return mic;
  } catch (err) {
    return null;
  }
}

const ink = '#e7ecf1';
const muted = '#9aa7b5';
const panel = '#1c2128';
const bg = '#121417';

function GearButton({ onPress }) {
  return (
    <Pressable accessibilityLabel="Settings" onPress={onPress} style={iconButton}>
      <Text style={{ color: ink, fontSize: 20 }}>⚙</Text>
    </Pressable>
  );
}

function ClearButton({ onPress }) {
  return (
    <Pressable accessibilityLabel="Clear reading" onPress={onPress} style={iconButton}>
      <Text style={{ color: ink, fontSize: 22 }}>✕</Text>
    </Pressable>
  );
}

function FrameGuide() {
  return (
    <View pointerEvents="none" style={{ position: 'absolute', top: 0, right: 0, bottom: 0, left: 0, alignItems: 'center', justifyContent: 'center' }}>
      <View style={{ width: '88%', aspectRatio: 16 / 9, borderWidth: 2, borderColor: '#ffffff', borderRadius: 4 }} />
    </View>
  );
}

function CameraPane() {
  const camera = useMemo(() => loadCamera(), []);
  if (camera && camera.CameraView && camera.useCameraPermissions) {
    return <DeviceCamera camera={camera} />;
  }
  return (
    <View style={{ flex: 1, backgroundColor: '#000', margin: 12, borderRadius: 12, overflow: 'hidden' }}>
      <FrameGuide />
    </View>
  );
}

function DeviceCamera({ camera }) {
  const CameraView = camera.CameraView;
  const [permission, requestPermission] = camera.useCameraPermissions();
  const chosen = selectHighestFrameRate(camera.availableFormats || []);
  const rateLabel = chosen ? `${chosen.maxFps ?? chosen.fps ?? chosen.max} fps` : '';
  useEffect(() => {
    if (permission && !permission.granted) requestPermission();
  }, [permission, requestPermission]);
  return (
    <View style={{ flex: 1, backgroundColor: '#000', margin: 12, borderRadius: 12, overflow: 'hidden' }}>
      <CameraView style={{ flex: 1 }} facing="back" active mode="video" videoQuality="2160p" />
      <FrameGuide />
      {rateLabel ? (
        <Text style={{ position: 'absolute', left: 16, bottom: 12, color: '#fff', fontSize: 12 }}>{rateLabel}</Text>
      ) : null}
    </View>
  );
}

function MeasureScreen({ session, onClear }) {
  const live = session.live;
  const ms = live ? live.offset_ms : null;
  return (
    <View style={{ flex: 1 }}>
      <CameraPane />
      <View style={{ paddingHorizontal: 20, paddingBottom: 12 }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' }}>
          <Text style={{ color: ink, fontSize: 56 }}>{offsetText(ms)}</Text>
          <ClearButton onPress={onClear} />
        </View>
        <Text style={{ color: ink, fontSize: 20, marginTop: 4 }}>{measureSentence(ms)}</Text>
        <Text style={{ color: muted, marginTop: 16 }}>
          {statusLine({
            quality: live ? live.quality : null,
            calibrated: Boolean(session.calibration),
            uncertaintyMs: live ? live.uncertaintyMs : null,
          })}
        </Text>
      </View>
    </View>
  );
}

function ResultsScreen({ session }) {
  return (
    <View style={{ flex: 1, padding: 20 }}>
      {session.results.length === 0 ? (
        <Text style={{ color: muted }}>No saved results</Text>
      ) : (
        session.results.map((item) => (
          <View key={item.id} style={{ paddingVertical: 12, borderBottomWidth: 1, borderBottomColor: '#2c3540' }}>
            <Text style={{ color: ink, fontSize: 18 }}>{offsetText(item.offset_ms)}</Text>
            <Text style={{ color: muted }}>{formatWhen(item.at)}</Text>
          </View>
        ))
      )}
    </View>
  );
}

function formatWhen(unixMs) {
  return formatResultWhen(unixMs);
}

function SettingsSheet({ onClose, onClearCalibration, onRepeat, onClearResults }) {
  return (
    <View style={{ position: 'absolute', top: 0, right: 0, bottom: 0, left: 0, backgroundColor: 'rgba(0,0,0,0.45)', justifyContent: 'flex-end' }}>
      <View style={{ backgroundColor: panel, padding: 20, borderTopLeftRadius: 16, borderTopRightRadius: 16 }}>
        <Text style={{ color: ink, fontSize: 18, marginBottom: 12 }}>Settings</Text>
        <SettingsRow label="Clear saved calibration" onPress={onClearCalibration} />
        <SettingsRow label="Repeat calibration" onPress={onRepeat} />
        <SettingsRow label="Clear results" onPress={onClearResults} />
        <SettingsRow label="Close" onPress={onClose} />
      </View>
    </View>
  );
}

function SettingsRow({ label, onPress }) {
  return (
    <Pressable onPress={onPress} style={{ paddingVertical: 14, borderTopWidth: 1, borderTopColor: '#2c3540' }}>
      <Text style={{ color: ink, fontSize: 16 }}>{label}</Text>
    </Pressable>
  );
}

function Waveform({ samples, spikeIndex }) {
  const peaks = waveformPeaks(samples, 48);
  const bucket = spikeBucket(spikeIndex, samples.length, peaks.length || 1);
  const max = Math.max(0.001, ...peaks);
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', height: 64, marginTop: 12 }}>
      {peaks.map((peak, index) => (
        <View key={index} style={{ flex: 1, alignItems: 'center', height: 64, justifyContent: 'center' }}>
          {index === bucket ? (
            <View style={{ position: 'absolute', top: 0, bottom: 0, width: 2, backgroundColor: '#e7ecf1' }} />
          ) : null}
          <View style={{ width: 3, height: Math.max(2, (peak / max) * 56), backgroundColor: '#7aa2ff', borderRadius: 1 }} />
        </View>
      ))}
    </View>
  );
}

async function replayTake(samples, sampleRate) {
  try {
    const { Audio } = require('expo-av');
    const wav = encodeWav(samples, sampleRate);
    const { sound } = await Audio.Sound.createAsync({ uri: `data:audio/wav;base64,${bytesToBase64(wav)}` });
    await sound.playAsync();
  } catch (err) {
    return false;
  }
  return true;
}

function CalibrationWizard({ onClose, onSave }) {
  const [phase, setPhase] = useState('aim');
  const [recording, setRecording] = useState(null);
  const startedAt = useMemo(() => ({ current: 0 }), []);
  const micRef = useMemo(() => ({ current: null }), []);

  const begin = () => {
    startedAt.current = Date.now();
    setRecording(createRecording(METER_HZ, 0));
    setPhase('recording');
    startMicrophone(setRecording).then((mic) => {
      micRef.current = mic;
    });
  };

  useEffect(() => {
    if (recording && recording.step === 'pick' && micRef.current) {
      const mic = micRef.current;
      micRef.current = null;
      mic.stopAndUnloadAsync?.();
    }
  }, [recording, micRef]);

  useEffect(() => {
    if (phase !== 'recording') return undefined;
    const timer = setInterval(() => {
      setRecording((current) => {
        if (!current || current.step !== 'recording') return current;
        return appendFrame(current, { timeMs: Date.now() - startedAt.current });
      });
    }, 50);
    return () => clearInterval(timer);
  }, [phase, startedAt]);

  const finish = () => {
    if (!recording) return;
    const offset = selectedOffsetMs(recording);
    onSave({
      offset_ms: offset,
      savedAt: Date.now(),
      spikeTimeMs: spikeAbsoluteMs(recording),
      frameTimeMs: recording.frames[recording.selected] ? recording.frames[recording.selected].timeMs : null,
      samples: recording.samples,
      sampleRate: recording.sampleRate,
    });
  };

  if (phase === 'aim' || !recording) {
    return (
      <View style={sheet}>
        <Text style={{ color: ink, fontSize: 22, marginBottom: 12 }}>Calibration</Text>
        <Text style={{ color: ink, fontSize: 16, lineHeight: 22 }}>{AIM_TEXT}</Text>
        <Pressable onPress={begin} style={primaryButton}><Text style={primaryText}>Start</Text></Pressable>
        <Pressable onPress={onClose} style={{ marginTop: 12 }}><Text style={{ color: muted }}>Cancel</Text></Pressable>
      </View>
    );
  }

  if (recording.step === 'recording') {
    return (
      <View style={sheet}>
        <Text style={{ color: ink, fontSize: 22 }}>Listening</Text>
        <Text style={{ color: muted, marginTop: 8 }}>Recording stops when a sharp sound is heard.</Text>
        <CameraPane />
      </View>
    );
  }

  const frame = recording.frames[recording.selected];
  const offset = selectedOffsetMs(recording);
  return (
    <View style={sheet}>
      <Text style={{ color: ink, fontSize: 22 }}>Match the sound</Text>
      <View style={{ marginTop: 16, height: 160, borderRadius: 8, backgroundColor: '#0e1114', alignItems: 'center', justifyContent: 'center' }}>
        <Text style={{ color: ink, fontSize: 18 }}>{frame ? frame.label || `${Math.round(frame.timeMs)} ms` : 'No frame'}</Text>
      </View>
      <View style={{ flexDirection: 'row', justifyContent: 'space-between', marginTop: 12 }}>
        <Pressable onPress={() => setRecording(stepSelection(recording, -1))} style={chip}><Text style={{ color: ink }}>Left</Text></Pressable>
        <Pressable onPress={() => setRecording(stepSelection(recording, 1))} style={chip}><Text style={{ color: ink }}>Right</Text></Pressable>
      </View>
      <Text style={{ color: ink, marginTop: 12 }}>{offsetText(offset)}</Text>
      <Text style={{ color: muted }}>{measureSentence(offset)}</Text>
      <Waveform samples={recording.samples} spikeIndex={recording.spike ? recording.spike.index : 0} />
      <Pressable onPress={() => replayTake(recording.samples, recording.sampleRate)} style={chip}>
        <Text style={{ color: ink }}>Replay</Text>
      </Pressable>
      <Pressable onPress={finish} style={primaryButton}><Text style={primaryText}>Save calibration</Text></Pressable>
    </View>
  );
}

const sheet = { flex: 1, backgroundColor: bg, padding: 20 };
const primaryButton = { marginTop: 20, backgroundColor: '#2a3644', padding: 14, borderRadius: 8, alignItems: 'center' };
const primaryText = { color: ink, fontSize: 16 };
const chip = { marginTop: 12, backgroundColor: '#2a3644', paddingVertical: 10, paddingHorizontal: 16, borderRadius: 8 };
const iconButton = { width: 44, height: 44, borderRadius: 22, backgroundColor: '#2a3644', alignItems: 'center', justifyContent: 'center' };

export default function App() {
  const native = useMemo(() => loadNative(), []);
  const [tab, setTab] = useState('measure');
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [wizardOpen, setWizardOpen] = useState(false);
  const [session, setSession] = useState(() => createSession());

  useEffect(() => {
    if (!native || !native.subscribe) return undefined;
    return native.subscribe((report) => {
      if (typeof report.offset_ms !== 'number') return undefined;
      setSession((current) => (
        report.complete
          ? completeMeasurement(current, report, Date.now())
          : setLive(current, report)
      ));
      return undefined;
    });
  }, [native]);

  return (
    <SafeAreaView style={{ flex: 1, backgroundColor: bg }}>
      <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingHorizontal: 16, paddingTop: 8 }}>
        <Text style={{ color: muted, letterSpacing: 1 }}>{tab === 'measure' ? 'MEASURE' : 'RESULTS'}</Text>
        <GearButton onPress={() => setSettingsOpen(true)} />
      </View>
      {tab === 'measure' ? (
        <MeasureScreen session={session} onClear={() => setSession(clearLive)} />
      ) : (
        <ResultsScreen session={session} />
      )}
      <View style={{ flexDirection: 'row', borderTopWidth: 1, borderTopColor: '#2c3540' }}>
        <TabButton label="Measure" active={tab === 'measure'} onPress={() => setTab('measure')} />
        <TabButton label="Results" active={tab === 'results'} onPress={() => setTab('results')} />
      </View>
      {settingsOpen ? (
        <SettingsSheet
          onClose={() => setSettingsOpen(false)}
          onClearCalibration={() => setSession(clearCalibration)}
          onClearResults={() => setSession(clearResults)}
          onRepeat={() => {
            setSettingsOpen(false);
            setWizardOpen(true);
          }}
        />
      ) : null}
      {wizardOpen ? (
        <View style={{ position: 'absolute', top: 0, right: 0, bottom: 0, left: 0 }}>
          <CalibrationWizard
            onClose={() => setWizardOpen(false)}
            onSave={(calibration) => {
              setSession((current) => saveCalibration(current, calibration));
              setWizardOpen(false);
            }}
          />
        </View>
      ) : null}
    </SafeAreaView>
  );
}

function TabButton({ label, active, onPress }) {
  return (
    <Pressable onPress={onPress} style={{ flex: 1, paddingVertical: 14, alignItems: 'center' }}>
      <Text style={{ color: active ? ink : muted, fontWeight: active ? '700' : '400' }}>{label}</Text>
    </Pressable>
  );
}
