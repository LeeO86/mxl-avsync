import {useEffect,useRef,useState} from 'react';
import {AppState,Image,Pressable,ScrollView,Text,View} from 'react-native';
import {Audio} from 'expo-av';
import Ionicons from '@expo/vector-icons/Ionicons';
import {CalibrationPlayback,clamp,repeatInterval,waveformPosition,waveformBins} from '../src/calibration-playback.mjs';
import {offsetText} from '../src/measure.mjs';
const ink='#e7ecf1',muted='#9aa7b5',blue='#60a5fa',orange='#ffc26b';
const text={color:ink,fontSize:16,lineHeight:23};
function Action({title,onPress,disabled}){return <Pressable accessibilityRole="button" disabled={disabled} onPress={onPress} style={{padding:12,borderRadius:8,backgroundColor:'#2a3644',marginTop:10,alignItems:'center',opacity:disabled ? .4 : 1}}><Text style={text}>{title}</Text></Pressable>;}
function Waveform({take,driver,state}){
 const [width,setWidth]=useState(1),t=driver.timeline;
 const values=waveformBins(take,t),max=Math.max(.001,...values.map(bin=>bin.value)),percent=time=>`${clamp((time-t.start)/Math.max(1,t.end-t.start)*100,0,100)}%`;
 return <View style={{flexDirection:'row',alignItems:'center',gap:12,marginTop:12}}>
  <Pressable accessibilityRole="button" accessibilityLabel={state.playing?'Pause audio':'Play audio'} onPress={()=>driver.toggle()} style={{width:48,height:56,alignItems:'center',justifyContent:'center',borderRadius:10,backgroundColor:'#2a3644'}}><Ionicons name={state.playing?'pause':'play'} color={ink} size={25}/></Pressable>
  <Pressable accessibilityRole="adjustable" accessibilityLabel="Calibration audio waveform" accessibilityHint="Tap to seek audio and video" accessibilityValue={{min:0,max:Math.round(t.end-t.start),now:Math.round(state.position-t.start)}} accessibilityActions={[{name:'increment'},{name:'decrement'}]} onAccessibilityAction={event=>driver.step(event.nativeEvent.actionName==='increment'?1:-1)} onLayout={event=>setWidth(event.nativeEvent.layout.width)} onPress={event=>driver.seek(waveformPosition(event.nativeEvent.locationX,width,t),state.playing)} style={{flex:1,height:70,justifyContent:'center'}}>
   <View pointerEvents="none" style={{position:'absolute',left:0,right:0,top:0,bottom:0}}>{values.map((bin,i)=>{const height=Math.max(2,bin.value/max*52);return <View key={i} style={{position:'absolute',left:`${bin.left}%`,width:`${bin.width}%`,top:(70-height)/2,height,backgroundColor:'#8e9aae',borderRightWidth:1,borderRightColor:'#1c2128'}}/>;})}</View>
   <View pointerEvents="none" style={{position:'absolute',left:percent(take.spikeTimeMs),width:2,top:0,bottom:0,backgroundColor:orange}}/>
   <View pointerEvents="none" style={{position:'absolute',left:percent(state.position),width:3,top:0,bottom:0,backgroundColor:blue}}/>
  </Pressable>
 </View>;
}
export default function CalibrationReview({take,onSave,onRepeat,onCancel}){
 const initial=useRef(null),controller=useRef(null),hold=useRef(null);
 if(!initial.current){const preview=new CalibrationPlayback(take,()=>{},()=>{});initial.current=preview.snapshot();}
 const [state,setState]=useState(initial.current),[leaving,setLeaving]=useState(false);
 const alive=useRef(true),closing=useRef(false);
 useEffect(()=>{
  alive.current=true;
  const driver=new CalibrationPlayback(take,async callback=>{
   await Audio.setAudioModeAsync({playsInSilentModeIOS:true,allowsRecordingIOS:false,staysActiveInBackground:false});
   const result=await Audio.Sound.createAsync({uri:take.uri},{shouldPlay:false,progressUpdateIntervalMillis:33},callback);return result.sound;
  },setState);controller.current=driver;setState(driver.snapshot());
  const timer=setInterval(()=>driver.tick(),16);
  const subscription=AppState.addEventListener('change',value=>{if(value!=='active'){stopHold();if(driver.playing)driver.seek(driver.position,false);}});
  return()=>{alive.current=false;stopHold();clearInterval(timer);subscription.remove();void driver.dispose();controller.current=null;};
 },[take]);
 const exit=async action=>{if(closing.current)return;closing.current=true;setLeaving(true);stopHold();const selected=controller.current?.snapshot().selected ?? state.selected;await controller.current?.dispose();if(alive.current)action(selected);};
 const stopHold=()=>{if(hold.current)clearTimeout(hold.current);hold.current=null;};
 const startHold=direction=>{stopHold();const started=performance.now();controller.current?.step(direction);const repeat=()=>{controller.current?.step(direction);hold.current=setTimeout(repeat,repeatInterval(performance.now()-started));};hold.current=setTimeout(repeat,350);};
 const prefetched=useRef(new Set());
 useEffect(()=>{for(const frame of take.frames.slice(Math.max(0,state.selected-2),state.selected+7)){if(!prefetched.current.has(frame.uri)){prefetched.current.add(frame.uri);Image.prefetch(frame.uri).catch(()=>{});}}},[take,state.selected]);
 const frame=take.frames[state.selected],delta=frame.timeMs-take.spikeTimeMs,driver=controller.current;
 return <ScrollView contentContainerStyle={{padding:20}}>
  <Text style={{...text,fontSize:22}}>Match the sound</Text>
  <Text style={{color:muted,lineHeight:21,marginVertical:10}}>Select the first frame where the hands or object make contact. Tap the waveform to seek; hold an arrow to move through frames faster.</Text>
  <View><Image source={{uri:frame.uri}} resizeMode="contain" style={{width:'100%',height:280,backgroundColor:'#000',borderRadius:12}}/>
   <Pressable accessibilityRole="button" accessibilityLabel={state.playing?'Pause video':'Play video'} onPress={()=>driver?.toggle()} style={{position:'absolute',right:12,bottom:12,padding:12,borderRadius:24,backgroundColor:'#1c2128e8'}}><Ionicons name={state.playing?'pause':'play'} size={25} color={ink}/></Pressable>
  </View>
  <View style={{flexDirection:'row',gap:12,marginTop:10}}>{[-1,1].map(direction=><Pressable key={direction} accessibilityRole="button" accessibilityLabel={direction<0?'Previous frame':'Next frame'} accessibilityActions={[{name:'activate'}]} onPressIn={()=>startHold(direction)} onPressOut={stopHold} onAccessibilityAction={()=>controller.current?.step(direction)} style={{flex:1,alignItems:'center',padding:14,borderRadius:8,backgroundColor:'#2a3644',opacity:(direction<0&&state.selected===0||direction>0&&state.selected===take.frames.length-1) ? .45 : 1}}><Ionicons name={direction<0?'chevron-back':'chevron-forward'} size={25} color={ink}/></Pressable>)}</View>
  <Text style={{...text,marginTop:10}}>Frame {state.selected+1}/{take.frames.length} · {delta>=0?'+':''}{delta.toFixed(1)} ms from sound</Text>
  <Text style={{color:muted,fontSize:12}}>{take.fps.toFixed(0)} fps · audio and video play together</Text>
  {driver && <Waveform take={take} driver={driver} state={state}/>}
  <Text style={{color:blue,fontSize:12,marginTop:5}}>Playback {(state.position-take.spikeTimeMs)>=0?'+':''}{(state.position-take.spikeTimeMs).toFixed(0)} ms · <Text style={{color:orange}}>orange: detected sound</Text></Text>
  {state.error && <Text style={{color:orange,marginTop:8}}>{state.error}</Text>}
  <Text style={{...text,marginTop:12}}>Correction {offsetText(take.spikeTimeMs-frame.timeMs)}</Text>
  <Action title="Save calibration" disabled={state.playing||leaving} onPress={()=>exit(onSave)}/><Action title="Repeat" disabled={leaving} onPress={()=>exit(onRepeat)}/><Action title="Cancel" disabled={leaving} onPress={()=>exit(onCancel)}/>
 </ScrollView>;
}
