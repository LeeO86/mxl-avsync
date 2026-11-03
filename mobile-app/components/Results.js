import { Pressable, ScrollView, Text, View } from 'react-native';
import { colors, small, text } from './ui';
import { formatWhen, latencyText, offsetText } from '../src/live.mjs';

export default function Results({ results, onShare }) {
 return <ScrollView contentContainerStyle={{ padding: 16 }}>
  {results.length ? results.map(item => <Pressable key={item.id} accessibilityRole="button" accessibilityHint="Share this result" onPress={() => onShare(item)}
   style={{ paddingVertical: 14, borderBottomWidth: 1, borderBottomColor: colors.line }}>
   <View style={{ flexDirection: 'row', alignItems: 'baseline', justifyContent: 'space-between' }}>
    <Text style={{ ...text, fontSize: 22 }}>{offsetText(item.offsetMs)}</Text>
    <Text style={small}>{formatWhen(item.at)}</Text>
   </View>
   <Text style={small}>Video {latencyText(item.videoLatencyMs)} · Audio {latencyText(item.audioLatencyMs)} · ±{Number.isFinite(item.spreadMs) ? (item.spreadMs / 2).toFixed(1) : '—'} ms{item.calibrationMs === null ? ' · not calibrated' : ''}</Text>
  </Pressable>) : <Text style={{ ...small, marginTop: 20, textAlign: 'center' }}>No saved results yet. Tap Save while measuring.</Text>}
 </ScrollView>;
}
