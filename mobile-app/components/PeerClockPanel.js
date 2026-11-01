import {useEffect,useRef,useState} from 'react';
import {Pressable,Text,View} from 'react-native';
export default function PeerClockPanel({native,busy,disabled,onSync}) {
 const [peers,setPeers]=useState([]),[searching,setSearching]=useState(false),[error,setError]=useState(null),[searched,setSearched]=useState(false);
 const alive=useRef(true);useEffect(()=>{alive.current=true;return()=>{alive.current=false;};},[]);
 const search=async()=>{setSearching(true);setError(null);try{const result=await native.discoverPeerClocks();if(alive.current){setPeers(result);setSearched(true);}}catch(e){if(alive.current)setError(e.message);}finally{if(alive.current)setSearching(false);}};
 if(!native?.synchronizePeerClock)return null;
 return <View style={{marginTop:16,paddingTop:12,borderTopWidth:1,borderTopColor:'#2c3540'}}>
  <Text style={{color:'#e7ecf1',fontSize:16}}>Direct device test</Text>
  <Text style={{color:'#9aa7b5',fontSize:12,marginTop:6}}>Start the native Mac sender, or enable Direct clock in Generate on another iPhone/iPad. Match its six-character code, then film its picture and sound directly.</Text>
  <Pressable accessibilityRole="button" disabled={searching||busy||disabled} onPress={search} style={{paddingVertical:12,opacity:(searching||busy||disabled)?0.4:1}}><Text style={{color:'#60a5fa'}}>{searching?'Finding senders…':'Find local senders'}</Text></Pressable>
  {peers.map(peer=><Pressable key={peer.name} accessibilityRole="button" disabled={busy||disabled} onPress={()=>onSync(peer)} style={{paddingVertical:12,opacity:(busy||disabled)?0.4:1}}><Text style={{color:'#60a5fa'}}>Use sender clock · {peer.code}</Text></Pressable>)}
  {searched&&!peers.length&&<Text style={{color:'#ffc26b',fontSize:12}}>No sender found. Keep its signal running on the same network and allow Local Network access.</Text>}
  {error&&<Text style={{color:'#ffc26b',fontSize:12}}>{error}</Text>}
  <Text style={{color:'#9aa7b5',fontSize:11}}>Clock-only exchange · sender-relative estimate · output/capture delay remains part of the measurement. Pair again after restarting or swapping senders.</Text>
 </View>;
}
