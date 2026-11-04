import { useEffect, useState } from 'react';
import { Modal, Pressable, ScrollView, Switch, Text, TextInput, View } from 'react-native';
import { Button, Card, Choice, Row, colors, small, text } from './ui';
import { latencyText, offsetText } from '../src/live.mjs';

export function Sheet({ visible, onClose, children }) {
 return <Modal visible={visible} transparent animationType="fade" onRequestClose={onClose}>
  <View style={{ flex: 1, backgroundColor: '#0008', justifyContent: 'flex-end' }}>
   <Pressable accessibilityRole="button" accessibilityLabel="Close" onPress={onClose} style={{ position: 'absolute', top: 0, right: 0, bottom: 0, left: 0 }} />
   <View style={{ maxHeight: '92%', backgroundColor: colors.card, borderTopLeftRadius: 18, borderTopRightRadius: 18 }}>
    <ScrollView keyboardShouldPersistTaps="handled" contentContainerStyle={{ padding: 20, paddingBottom: 40 }}>{children}</ScrollView>
   </View>
  </View>
 </Modal>;
}

// Settings and Debug share one modal (UIKit cannot present a second one while the first closes).
export function Settings({ onClose, calibration, clock, onCalibrate, onSyncClock, syncing, keepAwake, onKeepAwake, onPrivacy, onDebug }) {
 return <>
  <Text accessibilityRole="header" style={{ ...text, fontSize: 22, marginBottom: 6 }}>Settings</Text>
  <Row label="Calibration" value={calibration} />
  <Button title="Calibrate this phone" onPress={onCalibrate} style={{ marginTop: 0, marginBottom: 10 }} />
  <Row label="Clock" value={clock} />
  <Button title={syncing ? 'Synchronizing…' : 'Sync clock now'} disabled={syncing} onPress={onSyncClock} style={{ marginTop: 0, marginBottom: 10 }} />
  <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 10, paddingRight: 8, borderTopWidth: 1, borderTopColor: colors.line }}>
   <Text style={{ ...text, flex: 1 }}>Keep screen awake</Text>
   <Switch accessibilityLabel="Keep screen awake" value={keepAwake} onValueChange={onKeepAwake} />
  </View>
  <Row label="Privacy and support" onPress={onPrivacy} />
  <Row label="Debug" value="›" onPress={onDebug} />
  <Button title="Close" onPress={onClose} />
 </>;
}

const RATES = [[0, 'Auto'], [240, '240'], [120, '120'], [60, '60'], [30, '30']];

// Everything an engineer needs and an operator does not.
export function Debug({ onBack, session, onChange, report, clockDetails, onShare, onClearResults, onClearCalibration }) {
 const [server, setServer] = useState(''), [limit, setLimit] = useState(''), [tai, setTai] = useState('');
 useEffect(() => { setServer(session.ntpServer || ''); setLimit(String(session.clockMaxUncertaintyMs ?? 20)); setTai(String(session.taiUtcSeconds ?? 37)); }, []);
 const d = report?.diagnostics || {};
 const stats = report?.captureStats || {};
 const input = { ...text, backgroundColor: colors.card, padding: 10, borderRadius: 8, marginTop: 4 };
 const card = { backgroundColor: colors.bg };
 return <>
  <Pressable accessibilityRole="button" onPress={onBack} style={{ paddingBottom: 8 }}><Text style={{ ...text, color: colors.muted }}>‹ Settings</Text></Pressable>
  <Text accessibilityRole="header" style={{ ...text, fontSize: 22 }}>Debug</Text>
  <Button title="Share diagnostics" onPress={onShare} />
  <Card style={card}>
   <Text style={small}>Camera rate (fps)</Text>
   <Choice options={RATES} value={Number(session.captureFps) || 0} onChange={value => onChange({ captureFps: value })} />
   <Text style={{ ...small, marginTop: 8 }}>
    {`Mode ${report?.captureContext?.cameraMode || '—'} · delivered ${Number.isFinite(d.fps) ? d.fps.toFixed(1) : '—'} fps · exposure ${Number.isFinite(report?.exposureMs) && report.exposureMs > 0 ? report.exposureMs.toFixed(2) + ' ms (locked)' : 'auto'}`}
   </Text>
   <Text style={small}>{`Max frame gap ${Number.isFinite(stats.maxFrameGapMs) ? stats.maxFrameGapMs.toFixed(1) : '—'} ms · drops ${stats.dropCallbacks ?? '—'}`}</Text>
   {report?.captureContext?.inputGain !== undefined ? <Text style={small}>{`Mic gain ${Number(report.captureContext.inputGain).toFixed(2)} · ${report.captureContext.inputGainSettable ? 'settable' : 'fixed'}`}</Text> : null}
  </Card>
  <Card style={card}>
   <Text style={small}>Signal</Text>
   <Text selectable style={{ ...small, color: colors.ink }}>
    {`Stage ${report?.stage || '—'} · QR age ${Number.isFinite(d.qrAgeMs) ? Math.round(d.qrAgeMs) + ' ms' : '—'}\n` +
     `Bursts ${d.bursts ?? '—'} · crossings ${d.crossings ?? '—'}\n` +
     `Rejected ${d.rejected ?? '—'} · no QR ${d.unbound ?? '—'} · by grid ${d.byGrid ?? '—'}\n` +
     `Video events ${d.videoEvents ?? '—'} · audio timecodes ${d.audioTimecodes ?? '—'} · audio peak ${Number.isFinite(d.audioPeakDbfs) ? d.audioPeakDbfs.toFixed(0) + ' dBFS' : '—'}`}
   </Text>
   {(d.audioBursts || []).slice().reverse().map(b => <Text key={`a${b.t}`} selectable style={small}>{`Audio ${b.ok ? (b.eq ? 'ok (echo filter)' : 'ok') : 'failed'} · match ${Number(b.match).toFixed(2)} · sync errors ${b.syncErrors}/32 · ${Number(b.dbfs).toFixed(0)} dBFS`}</Text>)}
   {(report?.events || []).slice(-8).reverse().map(e => <Text key={e.seq} selectable style={small}>{`#${e.seq}  AV ${offsetText(e.offsetMs)}  V ${latencyText(e.videoLatencyMs)}  A ${latencyText(e.audioLatencyMs)}  (device UTC)`}</Text>)}
  </Card>
  <Card style={card}>
   <Text style={small}>Clock</Text>
   {clockDetails ? <Text selectable style={{ ...small, marginTop: 4 }}>{clockDetails}</Text> : null}
   <Text style={{ ...small, marginTop: 8 }}>NTP server</Text>
   <TextInput accessibilityLabel="NTP server" autoCapitalize="none" autoCorrect={false} placeholder="time.cloudflare.com" placeholderTextColor={colors.muted} value={server} onChangeText={setServer} style={input} />
   <Text style={{ ...small, marginTop: 8 }}>Maximum clock uncertainty (ms)</Text>
   <TextInput accessibilityLabel="Maximum clock uncertainty" keyboardType="numeric" value={limit} onChangeText={setLimit} style={input} />
   <Text style={{ ...small, marginTop: 8 }}>Source TAI − UTC (s)</Text>
   <TextInput accessibilityLabel="TAI minus UTC" keyboardType="number-pad" value={tai} onChangeText={setTai} style={input} />
   <Button title="Apply" onPress={() => onChange({ ntpServer: server.trim(), clockMaxUncertaintyMs: Number(limit), taiUtcSeconds: Number(tai) })} />
  </Card>
  <Button title="Clear saved results" onPress={onClearResults} />
  <Button title="Clear calibration" onPress={onClearCalibration} />
  <Button title="Back" onPress={onBack} />
 </>;
}
