import { useEffect, useMemo, useState } from 'react';
import { Pressable, SafeAreaView, ScrollView, Text, View } from 'react-native';
import { displayMeasurement, gradeColour } from './src/display.mjs';

// The native module is present on an Expo dev build. In this tree the JS
// display rules are what a device renders; capture itself is the native module.
function loadNative() {
  try {
    return require('./modules/avsync').default;
  } catch (err) {
    return null;
  }
}

export default function App() {
  const native = useMemo(() => loadNative(), []);
  const [report, setReport] = useState({
    calibration: 'missing',
    grade: 'invalid',
    sentence: 'no measurement',
    offset_ms: null,
    quality: 'invalid',
  });
  const view = displayMeasurement(report);
  useEffect(() => {
    if (native && native.subscribe) {
      return native.subscribe(setReport);
    }
    return undefined;
  }, [native]);
  return (
    <SafeAreaView style={{ flex: 1, background: '#121417', backgroundColor: '#121417' }}>
      <ScrollView contentContainerStyle={{ padding: 20 }}>
        <Text style={{ color: '#9aa7b5', letterSpacing: 1 }}>MXL AV SYNC</Text>
        <Text style={{ color: view.colour, fontSize: 56, marginTop: 12 }}>{view.offsetText}</Text>
        <Text style={{ color: '#e7ecf1', fontSize: 18 }}>{view.sentence}</Text>
        <View style={{ marginTop: 16, padding: 12, backgroundColor: view.showGrade ? gradeColour(report.grade) : '#d9822b', borderRadius: 8 }}>
          <Text style={{ color: '#111', fontWeight: '700' }}>{view.banner}</Text>
        </View>
        <Text style={{ color: '#9aa7b5', marginTop: 16 }}>Quality: {report.quality || 'invalid'}</Text>
        <Text style={{ color: '#9aa7b5' }}>Calibration: {view.calibrationLabel}</Text>
        <Text style={{ color: '#9aa7b5' }}>Uncertainty: {view.uncertainty}</Text>
        <Pressable
          onPress={() => native && native.exportResult && native.exportResult()}
          style={{ marginTop: 24, backgroundColor: '#2a3644', padding: 12, borderRadius: 8 }}
        >
          <Text style={{ color: '#e7ecf1' }}>Export result JSON</Text>
        </Pressable>
      </ScrollView>
    </SafeAreaView>
  );
}
