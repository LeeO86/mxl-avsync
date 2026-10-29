// Render the shipped App and exercise its real onReport/Save callbacks. UI/native
// surfaces are host-component mocks; no browser, simulator camera or network.
// AVSYNC_UI_DEPS=/tmp/avsync-ui-deps AVSYNC_BABEL_MODULES=/path/to/node_modules node tests/reading-ui.cjs
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const {createRequire}=require('node:module');
const root=path.resolve(__dirname,'..');
const deps=createRequire(path.join(process.env.AVSYNC_UI_DEPS || root,'package.json'));
const BabelRequire=createRequire(path.join(process.env.AVSYNC_BABEL_MODULES || path.join(root,'node_modules'),'../package.json'));
const babel=BabelRequire('@babel/core');
const React=deps('react'), renderer=deps('react-test-renderer');
let mono=5000,wall=100000,state=null,resumes=0;
const timers=new Map();let timerID=0;
const native={supportsLiveCapture:true,deviceVersion:()=> '26.6.1',
 loadState:()=>JSON.stringify({results:[],calibration:{offset_ms:-1,savedAt:1000,os:'26.6.1',capturePath:'native-raw-v1'}}),
 saveState:value=>{state=JSON.parse(value)}};
const rn={Platform:{Version:'26.6.1'},Share:{share:async()=>{}},Linking:{openURL:async()=>{}}};
for(const name of ['Image','Pressable','SafeAreaView','ScrollView','StatusBar','Text','TextInput','View'])rn[name]=name;
rn.Modal=props=>props.visible ? React.createElement('Modal',props,props.children) : null;
const cache=new Map();
class TestDate extends Date {static now(){return wall}}
function load(file){
 file=path.resolve(file);if(cache.has(file))return cache.get(file).exports;
 const module={exports:{}};cache.set(file,module);
 const code=babel.transformSync(fs.readFileSync(file,'utf8'),{filename:file,babelrc:false,configFile:false,
  plugins:[[BabelRequire.resolve('@babel/plugin-transform-react-jsx'),{runtime:'automatic'}],BabelRequire.resolve('@babel/plugin-transform-modules-commonjs')]}).code;
 const requireHere=spec=>{
  if(spec==='react'||spec==='react/jsx-runtime')return deps(spec);
  if(spec==='react-native')return rn;
  if(spec==='expo-camera')return {useCameraPermissions:()=>[{granted:true},async()=>{}],useMicrophonePermissions:()=>[{granted:true},async()=>{}]};
  if(spec==='expo-av')return {Audio:{}};
  if(spec==='expo-modules-core')return {requireNativeViewManager:()=> 'NativeCamera'};
  if(spec==='@expo/vector-icons/Ionicons')return 'Ionicons';
  if(spec==='./modules/avsync')return native;
  if(spec.endsWith('.png'))return 1;
  if(spec.startsWith('.'))return load(path.resolve(path.dirname(file),spec));
  throw new Error('Unexpected dependency: '+spec);
 };
 const sandbox={require:requireHere,module,exports:module.exports,console,Date:TestDate,performance:{now:()=>mono},
  setInterval:callback=>{const id=++timerID;timers.set(id,callback);return id},clearInterval:id=>timers.delete(id)};
 vm.runInNewContext(code,sandbox,{filename:file});return module.exports;
}
const App=load(path.join(root,'App.js')).default;
let tree;renderer.act(()=>{tree=renderer.create(React.createElement(App),{createNodeMock:element=>element.type==='NativeCamera'?{resumeAnalysis:()=>resumes++}:null})});
const button=title=>tree.root.findAllByType('Pressable').find(node=>node.findAllByType('Text').some(t=>t.children.join('')===title));
const text=()=>tree.root.findAllByType('Text').map(node=>node.children.filter(x=>typeof x==='string').join('')).join('\n');
function report(value){renderer.act(()=>tree.root.findByType('NativeCamera').props.onReport({nativeEvent:value}))}
function advance(ms){renderer.act(()=>{mono+=ms;wall+=ms;for(const timer of timers.values())timer()})}
const sample={kind:'reading',timeMs:2400,offset_ms:-26,eventCount:3,spread_ms:6.2,quality:'degraded',signalIdentity:'source-a',
 audioTimecode:{valid:true,crcOk:true,sourceId:123},events:[1000,1500,2000].map((t,i)=>({seq:i,valid:true,offset_ms:-26,videoTimeMs:t,audioTimeMs:t-26}))};
assert.equal(button('Save result').props.disabled,true);
report(sample);assert.equal(button('Save result').props.disabled,false);assert.match(text(),/-25\.0 ms/);
report({kind:'status',stage:'inconsistent_pairs',message:'Waiting for consistent pairs'});
advance(5000);assert.equal(button('Save result').props.disabled,false);assert.match(text(),/Last valid reading/);
renderer.act(()=>button('Save result').props.onPress());
assert.equal(state.results.length,1);assert.equal(state.results[0].raw_offset_ms,-26);assert.equal(state.results[0].offset_ms,-25);
assert.equal(state.results[0].at,100000);assert.equal(state.results[0].savedAt,105000);assert.equal(state.results[0].savedDisplayState,'held');
assert.equal(state.results[0].measurementAgeMsAtSave,5400);
report({...sample,timeMs:7400});assert.match(text(),/Last valid reading/);
renderer.act(()=>button('Pause').props.onPress());assert.equal(button('Save result').props.disabled,false);assert.match(text(),/Paused · last valid/);
report({kind:'status',signalIdentity:'source-b'});assert.equal(button('Save result').props.disabled,true);
report({...sample,signalIdentity:'source-b',events:[{seq:9,valid:true,offset_ms:-26,videoTimeMs:8000,audioTimeMs:7974}],timeMs:8400});
renderer.act(()=>tree.root.findAllByType('Pressable').find(node=>node.props.accessibilityLabel==='Clear reading').props.onPress());
assert.equal(button('Save result').props.disabled,true);assert.equal(resumes,1);
renderer.act(()=>tree.unmount());assert.equal(timers.size,0);
console.log('PASS: real App keeps Save enabled after status/timeout/pause, preserves original evidence and age, clears on source change/explicit reset.');
