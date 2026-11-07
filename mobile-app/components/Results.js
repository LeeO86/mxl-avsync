import { useState } from 'react';
import { Pressable, ScrollView, Text, TextInput, View } from 'react-native';
import Ionicons from '@expo/vector-icons/Ionicons';
import { colors, small, text } from './ui';
import { formatWhen, latencyText, offsetText } from '../src/live.mjs';
import { deltaText, groupResults } from '../src/results.mjs';

// Saved results grouped by sender. ★ marks a sender's reference; the other results show Δ to it.
export default function Results({ results, senders, onShare, onToggleReference, onRename }) {
 const groups = groupResults(results, senders);
 const [open, setOpen] = useState({}), [editing, setEditing] = useState(null), [draft, setDraft] = useState('');
 const isOpen = (group, i) => open[group.key] ?? i === 0;
 const finishEdit = () => { onRename(editing, draft); setEditing(null); };
 return <ScrollView contentContainerStyle={{ padding: 16 }} keyboardShouldPersistTaps="handled">
  {groups.length ? groups.map((group, i) => <View key={String(group.key)} style={{ marginBottom: 8 }}>
   <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 10, borderBottomWidth: 1, borderBottomColor: colors.line }}>
    <Pressable accessibilityRole="button" accessibilityState={{ expanded: isOpen(group, i) }} onPress={() => setOpen({ ...open, [group.key]: !isOpen(group, i) })}
     style={{ flex: 1, flexDirection: 'row', alignItems: 'center' }}>
     <Ionicons name={isOpen(group, i) ? 'chevron-down' : 'chevron-forward'} size={18} color={colors.muted} style={{ marginRight: 6 }} />
     {editing === group.key
      ? <TextInput autoFocus accessibilityLabel="Sender nickname" value={draft} onChangeText={setDraft} onSubmitEditing={finishEdit} onBlur={finishEdit}
       maxLength={40} placeholder={group.id || ''} placeholderTextColor={colors.muted} returnKeyType="done"
       style={{ ...text, flex: 1, paddingVertical: 4, borderBottomWidth: 1, borderBottomColor: colors.accent }} />
      : <Text style={{ ...text, fontWeight: '600', flexShrink: 1 }}>{group.name}
       <Text style={small}>{group.key === null ? `  (${group.items.length} results)` : group.id && group.id !== group.name ? `  (${group.id})` : ''}</Text></Text>}
    </Pressable>
    {group.key !== null && editing !== group.key ? <Pressable accessibilityRole="button" accessibilityLabel="Edit nickname" style={{ padding: 6 }}
     onPress={() => { setEditing(group.key); setDraft(senders?.[group.key]?.nickname || ''); }}>
     <Ionicons name="pencil-outline" size={18} color={colors.muted} />
    </Pressable> : null}
   </View>
   {isOpen(group, i) && group.items.map(({ result: item, isReference, delta, check }) => <View key={item.id}
    style={{ flexDirection: 'row', alignItems: 'center', borderBottomWidth: 1, borderBottomColor: colors.line }}>
    {group.key !== null ? <Pressable accessibilityRole="button" accessibilityLabel={isReference ? 'Clear reference' : 'Use as reference'} onPress={() => onToggleReference(item)} style={{ padding: 8 }}>
     <Ionicons name={isReference ? 'star' : 'star-outline'} size={20} color={isReference ? colors.accent : colors.muted} />
    </Pressable> : null}
    <Pressable accessibilityRole="button" accessibilityHint="Share this result" onPress={() => onShare(item)} style={{ flex: 1, paddingVertical: 12 }}>
     <View style={{ flexDirection: 'row', alignItems: 'baseline', justifyContent: 'space-between' }}>
      <Text style={{ ...text, fontSize: 22 }}>{offsetText(item.offsetMs)}
       {delta ? <Text style={{ ...text, color: colors.accent }}>{`  ${deltaText(delta.offsetMs)}`}</Text> : null}</Text>
      <Text style={small}>{isReference ? 'Reference · ' : ''}{formatWhen(item.at)}</Text>
     </View>
     <Text style={small}>Video {latencyText(item.videoLatencyMs)} · Audio {latencyText(item.audioLatencyMs)} · ±{Number.isFinite(item.spreadMs) ? (item.spreadMs / 2).toFixed(1) : '—'} ms{item.calibrationMs === null ? ' · not calibrated' : ''}</Text>
     {delta && (Number.isFinite(delta.videoLatencyMs) || Number.isFinite(delta.audioLatencyMs) || check) ? <Text style={{ ...small, color: colors.accent }}>
      {[Number.isFinite(delta.videoLatencyMs) && `Video ${deltaText(delta.videoLatencyMs)}`, Number.isFinite(delta.audioLatencyMs) && `Audio ${deltaText(delta.audioLatencyMs)}`,
       check && 'check: camera mode or calibration differs from the reference'].filter(Boolean).join(' · ')}
     </Text> : null}
    </Pressable>
   </View>)}
  </View>) : <Text style={{ ...small, marginTop: 20, textAlign: 'center' }}>No saved results yet. Tap Save while measuring.</Text>}
 </ScrollView>;
}
