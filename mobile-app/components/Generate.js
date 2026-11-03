import { useState } from 'react';
import { Image, Pressable, ScrollView, Text, TextInput } from 'react-native';
import { Button, Card, Choice, colors, small, text } from './ui';

const input = { ...text, backgroundColor: colors.bg, padding: 12, borderRadius: 8, marginTop: 6 };

// Fullscreen test signal from this phone. Labels and image are optional extras.
export default function Generate({ session, onChange, image, busy, picking, onPickImage, onStart, supported, notice }) {
 const [more, setMore] = useState(false);
 return <ScrollView keyboardShouldPersistTaps="handled" contentContainerStyle={{ padding: 16 }}>
  <Card>
   <Text style={{ ...text, fontSize: 22 }}>Test signal</Text>
   <Text style={{ ...small, marginTop: 8 }}>Show this phone to the camera you want to test and keep its speaker audible. Measure the result with a second phone or the analyzer.</Text>
   <Button primary title={busy ? 'Signal is running…' : 'Start test signal'} disabled={busy || picking || !supported} onPress={onStart} />
   {!supported && <Text style={{ ...small, color: colors.accent, marginTop: 8 }}>This build cannot generate a signal.</Text>}
   {notice ? <Text style={{ ...small, color: colors.accent, marginTop: 8 }}>{notice}</Text> : null}
  </Card>
  <Pressable accessibilityRole="button" onPress={() => setMore(value => !value)} style={{ paddingVertical: 14 }}>
   <Text style={{ ...text, color: colors.muted }}>{more ? 'Hide options' : 'Options'}</Text>
  </Pressable>
  {more && <Card style={{ marginTop: 0 }}>
   <Text style={small}>Events per second</Text>
   <Choice options={[[2, '2 / s'], [1, '1 / s']]} value={session.generatorRate === 1 ? 1 : 2} onChange={value => onChange('generatorRate', value)} disabled={busy} />
   <Text style={{ ...small, marginTop: 6 }}>Use 1 / s when the measuring camera runs at 30 fps.</Text>
   <Text style={{ ...small, marginTop: 14 }}>Title</Text>
   <TextInput accessibilityLabel="Title" editable={!busy} maxLength={64} placeholder="iPhone" placeholderTextColor={colors.muted} value={session.generatorName || ''} onChangeText={value => onChange('generatorName', value)} style={input} />
   <Text style={{ ...small, marginTop: 12 }}>Subtitle</Text>
   <TextInput accessibilityLabel="Subtitle" editable={!busy} maxLength={128} placeholder="Camera or signal path" placeholderTextColor={colors.muted} value={session.generatorSubtitle || ''} onChangeText={value => onChange('generatorSubtitle', value)} style={input} />
   {image && <Image accessibilityLabel="Image preview" source={{ uri: image.uri }} resizeMode="contain" style={{ width: 72, height: 72, marginTop: 12, borderRadius: 8, backgroundColor: colors.bg }} />}
   {onPickImage && <Button title={picking ? 'Importing image…' : image ? 'Change image' : 'Add image'} disabled={busy || picking} onPress={onPickImage} />}
   {session.generatorImageId ? <Button title="Remove image" disabled={busy || picking} onPress={() => onChange('generatorImageId', '')} /> : null}
  </Card>}
 </ScrollView>;
}
