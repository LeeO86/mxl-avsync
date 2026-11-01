import {Pressable,Text,View} from 'react-native';
import {captureDiagnostics} from '../src/capture.mjs';

export default function CaptureDiagnostics({report,ageMs,active,onShare}) {
 const {lines,warning}=captureDiagnostics(report,ageMs,active);
 return <View style={{marginTop:10}}>
  <Text style={{color:'#9aa7b5',fontSize:11,fontWeight:'600'}}>CAPTURE DIAGNOSTICS · current report</Text>
  {lines.map(line=><Text selectable key={line} style={{color:'#9aa7b5',fontSize:11,marginTop:4}}>{line}</Text>)}
  {warning && <Text style={{color:'#ffc26b',fontSize:11,marginTop:6}}>{warning}</Text>}
  <Pressable accessibilityRole="button" accessibilityLabel="Share capture diagnostics" onPress={onShare} style={{paddingVertical:12}}><Text style={{color:'#60a5fa',fontSize:13}}>Share diagnostic snapshot</Text></Pressable>
 </View>;
}
