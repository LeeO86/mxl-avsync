import { Text, View } from 'react-native';
import { Button, colors, small, text } from './ui';
import { latencyText, offsetSentence, offsetText } from '../src/live.mjs';

// The camera fills the screen; below it one big number and the two latencies.
export default function Measure({ camera, view, flow, calibrated, onSave, onRestart, onStartCalibration, onCancelCalibration, notice }) {
 const reading = view.reading;
 const overlay = flow === 'aim' ? 'Point the camera at your hands, tap Start, then clap once.'
  : flow === 'listening' ? 'Clap once now. Keep the hands in view.'
  : reading?.fresh ? null : view.message;
 return <View style={{ flex: 1 }}>
  <View style={{ flex: 1, backgroundColor: '#000', marginHorizontal: 12, marginTop: 8, borderRadius: 14, overflow: 'hidden', justifyContent: 'center' }}>
   {camera}
   {overlay ? <View pointerEvents="none" style={{ position: 'absolute', left: 12, right: 12, top: 12, padding: 12, borderRadius: 10, backgroundColor: '#121417d9' }}>
    <Text style={text}>{overlay}</Text>
   </View> : null}
  </View>
  {flow ? <View style={{ padding: 16 }}>
   {flow === 'aim' && <Button primary title="Start" onPress={onStartCalibration} />}
   <Button title="Cancel" onPress={onCancelCalibration} />
   {notice ? <Text style={{ ...small, color: colors.accent, marginTop: 8 }}>{notice}</Text> : null}
  </View> : <View style={{ paddingHorizontal: 20, paddingTop: 12, paddingBottom: 8 }}>
   <Text accessibilityRole="header" style={{ color: reading?.fresh ? colors.ink : colors.muted, fontSize: 52, fontWeight: '300' }}>{offsetText(reading?.offsetMs)}</Text>
   <Text style={{ ...text, color: reading?.fresh ? colors.ink : colors.muted }}>
    {reading ? offsetSentence(reading.offsetMs) : ' '}
    {reading && !reading.fresh ? ` · ${Math.round(reading.heldForMs / 1000)} s ago` : ''}
   </Text>
   <Text style={{ ...small, marginTop: 4 }}>
    {reading ? `±${(reading.spreadMs / 2).toFixed(1)} ms · ${reading.events} events${calibrated ? '' : ' · not calibrated'}` : ' '}
   </Text>
   <View style={{ flexDirection: 'row', marginTop: 12 }}>
    <View style={{ flex: 1 }}><Text style={small}>VIDEO LATENCY</Text><Text style={{ ...text, fontSize: 20 }}>{latencyText(reading?.videoLatencyMs)}</Text></View>
    <View style={{ flex: 1 }}><Text style={small}>AUDIO LATENCY</Text><Text style={{ ...text, fontSize: 20 }}>{latencyText(reading?.audioLatencyMs)}</Text></View>
   </View>
   {notice ? <Text style={{ ...small, color: colors.accent, marginTop: 6 }}>{notice}</Text> : null}
   <View style={{ flexDirection: 'row', gap: 10 }}>
    <View style={{ flex: 1 }}><Button title="Restart" onPress={onRestart} /></View>
    <View style={{ flex: 1 }}><Button primary title="Save" disabled={!reading} onPress={onSave} /></View>
   </View>
  </View>}
 </View>;
}
