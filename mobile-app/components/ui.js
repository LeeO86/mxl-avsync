import { Pressable, Text, View } from 'react-native';

export const colors = { bg: '#121417', card: '#1c2128', line: '#2c3540', ink: '#e7ecf1', muted: '#9aa7b5', accent: '#ffc26b', good: '#76e2a3', button: '#2a3644' };
export const text = { color: colors.ink, fontSize: 16, lineHeight: 22 };
export const small = { color: colors.muted, fontSize: 13, lineHeight: 18 };

export function Button({ title, onPress, disabled, primary, style }) {
 return <Pressable accessibilityRole="button" disabled={disabled} onPress={onPress}
  style={[{ padding: 14, borderRadius: 10, backgroundColor: primary && !disabled ? colors.accent : colors.button, alignItems: 'center', marginTop: 10 }, disabled && { opacity: 0.35 }, style]}>
  <Text style={{ ...text, color: primary && !disabled ? '#1a1206' : colors.ink, fontWeight: primary ? '700' : '500' }}>{title}</Text>
 </Pressable>;
}

export function Card({ children, style }) {
 return <View style={[{ backgroundColor: colors.card, borderRadius: 14, padding: 16, marginTop: 12 }, style]}>{children}</View>;
}

export function Row({ label, value, onPress, valueColor }) {
 const body = <View style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 14, borderTopWidth: 1, borderTopColor: colors.line }}>
  <Text style={{ ...text, flex: 1 }}>{label}</Text>
  {value !== undefined && <Text style={{ ...small, color: valueColor || colors.muted, maxWidth: '60%', textAlign: 'right' }}>{value}</Text>}
 </View>;
 return onPress ? <Pressable accessibilityRole="button" onPress={onPress}>{body}</Pressable> : body;
}

export function Choice({ options, value, onChange, disabled }) {
 return <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8, marginTop: 8 }}>
  {options.map(([key, label]) => <Pressable key={String(key)} accessibilityRole="radio" accessibilityState={{ checked: value === key, disabled }} disabled={disabled} onPress={() => onChange(key)}
   style={{ paddingVertical: 9, paddingHorizontal: 14, borderRadius: 9, borderWidth: 1, borderColor: value === key ? colors.accent : colors.line, opacity: disabled ? 0.4 : 1 }}>
   <Text style={{ color: value === key ? colors.accent : colors.ink }}>{label}</Text>
  </Pressable>)}
 </View>;
}
