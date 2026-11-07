import { useRef, useState } from 'react';
import { Animated, Pressable, Text, View } from 'react-native';
import { Button, colors, small, text } from './ui';
import { latencyText, offsetSentence, offsetText } from '../src/live.mjs';
import { FADE_MS, REFOCUS_MS, tapPoint } from '../src/focus.mjs';

// The camera fills the screen; below it one big number and the two latencies.
// A quick tap on the camera refocuses there (not during calibration, so the take stays whole).
export default function Measure({ camera, view, flow, calibrated, onSave, onRestart, onFocus, onStartCalibration, onCancelCalibration, notice }) {
 const reading = view.reading;
 const size = useRef({ width: 0, height: 0 });
 const [focus, setFocus] = useState(null); // the tap point in the camera box, while the square shows
 const fade = useRef(new Animated.Value(0)).current;
 const fadeOut = useRef(null);
 const tap = event => {
  const { locationX, locationY } = event.nativeEvent;
  const point = tapPoint(locationX, locationY, size.current.width, size.current.height);
  if (!point) return;
  onFocus(point.x, point.y);
  setFocus({ x: locationX, y: locationY });
  fadeOut.current?.stop(); fade.setValue(1);
  fadeOut.current = Animated.sequence([Animated.delay(REFOCUS_MS), Animated.timing(fade, { toValue: 0, duration: FADE_MS, useNativeDriver: true })]);
  fadeOut.current.start(({ finished }) => { if (finished) setFocus(null); });
 };
 const overlay = flow === 'aim' ? 'Point the camera at your hands, tap Start, then clap once.'
  : flow === 'listening' ? 'Clap once now. Keep the hands in view.'
  : focus ? 'Focusing…'
  : reading?.fresh ? null : view.stage === 'starting' && notice ? notice : view.message;
 const noticeBelow = notice && overlay !== notice;
 return <View style={{ flex: 1 }}>
  <Pressable disabled={Boolean(flow) || !onFocus} onPress={tap} onLongPress={() => {}} accessibilityHint="Tap to focus here"
   onLayout={event => { size.current = event.nativeEvent.layout; }}
   style={{ flex: 1, backgroundColor: '#000', marginHorizontal: 12, marginTop: 8, borderRadius: 14, overflow: 'hidden', justifyContent: 'center' }}>
   {camera}
   {focus ? <Animated.View pointerEvents="none" style={{ position: 'absolute', left: focus.x - 36, top: focus.y - 36, width: 72, height: 72, borderWidth: 2, borderColor: '#ffd60a', opacity: fade }} /> : null}
   {overlay ? <View pointerEvents="none" style={{ position: 'absolute', left: 12, right: 12, top: 12, padding: 12, borderRadius: 10, backgroundColor: '#121417d9' }}>
    <Text style={text}>{overlay}</Text>
   </View> : null}
  </Pressable>
  {flow ? <View style={{ padding: 16 }}>
   {flow === 'aim' && <Button primary title="Start" onPress={onStartCalibration} />}
   <Button title="Cancel" onPress={onCancelCalibration} />
   {notice ? <Text style={{ ...small, color: colors.accent, marginTop: 8 }}>{notice}</Text> : null}
  </View> : <View style={{ paddingHorizontal: 20, paddingTop: 4, paddingBottom: 8 }}>
   <Text accessibilityRole="header" style={{ color: reading?.fresh ? colors.ink : colors.muted, fontSize: 48, lineHeight: 54, fontWeight: '300' }}>{reading ? offsetText(reading.offsetMs) : '—'}</Text>
   <Text style={{ ...text, color: reading?.fresh ? colors.ink : colors.muted }}>
    {reading ? offsetSentence(reading.offsetMs) : 'No reading yet'}
    {reading && !reading.fresh ? ` · ${Math.round(reading.heldForMs / 1000)} s ago` : ''}
   </Text>
   {reading ? <Text style={{ ...small, marginTop: 4 }}>{`±${(reading.spreadMs / 2).toFixed(1)} ms · ${reading.events} events${calibrated ? '' : ' · not calibrated'}`}</Text> : null}
   <View style={{ flexDirection: 'row', marginTop: 12 }}>
    <View style={{ flex: 1 }}><Text style={small}>VIDEO LATENCY</Text><Text style={{ ...text, fontSize: 20 }}>{latencyText(reading?.videoLatencyMs)}</Text></View>
    <View style={{ flex: 1 }}><Text style={small}>AUDIO LATENCY</Text><Text style={{ ...text, fontSize: 20 }}>{latencyText(reading?.audioLatencyMs)}</Text></View>
   </View>
   {noticeBelow ? <Text style={{ ...small, color: colors.accent, marginTop: 6 }}>{notice}</Text> : null}
   <View style={{ flexDirection: 'row', gap: 10 }}>
    <View style={{ flex: 1 }}><Button title="Restart" onPress={onRestart} /></View>
    <View style={{ flex: 1 }}><Button primary title="Save" disabled={!reading} onPress={onSave} /></View>
   </View>
  </View>}
 </View>;
}
