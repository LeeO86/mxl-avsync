const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');

// Exercise the actual shipped Vue methods, without a separate imitation of the form logic.
function app(fetch) {
  let options;
  const source = fs.readFileSync(path.join(__dirname, '../../web/index.html'), 'utf8').split('<script>')[1].split('</script>')[0];
  vm.runInNewContext(source, {Vue:{createApp(o) { options=o; return {mount() {}}; }}, fetch, Date, location:{}});
  const instance = options.data();
  for (const [key, method] of Object.entries(options.methods)) instance[key]=method.bind(instance);
  for (const [key, getter] of Object.entries(options.computed)) Object.defineProperty(instance, key, {get:getter.bind(instance)});
  return instance;
}
function server() {
  const state = {generator:{offset_ms:0, level_dbfs:-12, title:'Saved', source:'', subtitle:'', event_rate:2, audio_timecode:'1200', muted:false}, config:{AVSYNC_TITLE:'Saved', LOG_LEVEL:'info'}};
  const calls=[];
  const fetch = async (url, init={}) => {
    calls.push({url, ...init});
    if (url==='/api/v1/generator' && init.method==='PATCH') Object.assign(state.generator, JSON.parse(init.body));
    if (url==='/api/v1/config' && init.method==='PUT') Object.assign(state.config, JSON.parse(init.body));
    const data = url==='/api/v1/config' ? state.config : url==='/api/v1/generator' ? state.generator : {};
    return {ok:true, headers:{get:()=>''}, json:async()=>({...data}), text:async()=>''};
  };
  return {state,calls,fetch};
}

test('polling does not replace generator or config drafts, including a temporarily blank numeric input', async () => {
  const backend=server(), a=app(backend.fetch); a.info={role:'generator'};
  await a.loadConfig(); await a.refresh();
  a.title='Draft being typed'; a.offset=''; a.draft.AVSYNC_TITLE='Settings draft';
  backend.state.generator.title='External update'; backend.state.config.AVSYNC_TITLE='External settings';
  for (let i=0;i<4;i++) await a.refresh();
  assert.equal(a.title,'Draft being typed'); assert.equal(a.offset,''); assert.equal(a.draft.AVSYNC_TITLE,'Settings draft');
  assert.equal(a.signalDirty,true); assert.equal(a.settingsDirty,true);
  assert.equal(backend.calls.filter(c=>c.url==='/api/v1/config').length,1);
  await a.discardGenerator(); await a.discardSettings();
  assert.equal(a.title,'External update'); assert.equal(a.draft.AVSYNC_TITLE,'External settings');
  assert.equal(a.signalDirty,false); assert.equal(a.settingsDirty,false);
});

test('Apply persists a draft and adopts its own successful server response', async () => {
  const backend=server(), a=app(backend.fetch); a.info={role:'generator'};
  await a.loadConfig(); await a.refresh();
  a.title='Applied signal'; a.offset=35; await a.saveGenerator();
  a.draft.AVSYNC_TITLE='Applied settings'; await a.applySettings();
  assert.equal(backend.state.generator.title,'Applied signal'); assert.equal(backend.state.generator.offset_ms,35);
  assert.equal(backend.state.config.AVSYNC_TITLE,'Applied settings');
  assert.equal(a.signalDirty,false); assert.equal(a.settingsDirty,false);
});

test('failed writes preserve drafts and report errors', async () => {
  const backend=server();
  const a=app((url, init={})=>init.method ? Promise.resolve({ok:false,json:async()=>({error:'rejected'})}) : backend.fetch(url, init));
  a.info={role:'generator'}; await a.loadConfig(); await a.refresh();
  a.title='Keep signal'; a.draft.AVSYNC_TITLE='Keep settings'; await a.saveGenerator(); await a.applySettings();
  assert.equal(a.title,'Keep signal'); assert.equal(a.draft.AVSYNC_TITLE,'Keep settings');
  assert.equal(a.signalError,'rejected'); assert.equal(a.settingsError,'rejected');
  assert.equal(a.signalDirty,true); assert.equal(a.settingsDirty,true);
});

test('edits typed during Apply survive its response and remain unsaved', async () => {
  const backend=server(); let release;
  const gate=new Promise(resolve=>release=resolve);
  const a=app(async (url,init={})=>{ if(init.method) await gate; return backend.fetch(url,init); });
  a.info={role:'generator'}; await a.loadConfig(); await a.refresh();
  a.title='Submitted'; a.draft.AVSYNC_TITLE='Submitted';
  const signal=a.saveGenerator(), settings=a.applySettings();
  a.title='Typed later'; a.draft.AVSYNC_TITLE='Typed later'; release(); await Promise.all([signal,settings]);
  assert.equal(backend.state.generator.title,'Submitted'); assert.equal(backend.state.config.AVSYNC_TITLE,'Submitted');
  assert.equal(a.title,'Typed later'); assert.equal(a.draft.AVSYNC_TITLE,'Typed later');
  assert.equal(a.signalDirty,true); assert.equal(a.settingsDirty,true);
});

test('event-rate and modem selections survive polling and persist on Apply', async () => {
  const backend=server(), a=app(backend.fetch); a.info={role:'generator'};
  await a.refresh(); a.eventRate=1; a.audioTimecode='600';
  for(let i=0;i<4;i++)await a.refresh();
  assert.equal(a.eventRate,1); assert.equal(a.audioTimecode,'600'); assert.equal(a.signalDirty,true);
  await a.saveGenerator();
  assert.equal(backend.state.generator.event_rate,1); assert.equal(backend.state.generator.audio_timecode,'600');
  assert.equal(a.signalDirty,false);
});

test('image-only selection makes the form dirty and uploads bytes on Apply', async () => {
  const backend=server(), a=app(backend.fetch); a.info={role:'generator'};
  await a.refresh();
  const file={name:'picture.PNG',type:'image/png',arrayBuffer:async()=>new Uint8Array([137,80,78,71]).buffer};
  a.$refs={sourceImageInput:{value:'picture.PNG'}};
  a.onImage({target:{files:[file]}});
  assert.equal(a.imageFile,file); assert.equal(a.signalDirty,true);
  for(let i=0;i<4;i++) await a.refresh();
  assert.equal(a.imageFile,file);
  await a.saveGenerator();
  const upload=backend.calls.find(c=>c.url==='/api/v1/generator/source-image' && c.method==='POST');
  assert.ok(upload); assert.equal(upload.headers['Content-Type'],'image/png');
  assert.deepEqual([...new Uint8Array(upload.body)],[137,80,78,71]);
  assert.equal(a.imageFile,null); assert.equal(a.$refs.sourceImageInput.value,'');
  assert.equal(a.signalDirty,false);
  a.onImage({target:{files:[file]}}); await a.discardGenerator();
  assert.equal(a.imageFile,null); assert.equal(a.$refs.sourceImageInput.value,'');
  a.onImage({target:{files:[]}}); assert.equal(a.imageFile,null);
});

test('failed image uploads retain the selection and a later image survives an in-flight upload', async () => {
  const backend=server(); let rejectUpload=true,release;
  const gate=new Promise(resolve=>release=resolve);
  const first={name:'first.gif',type:'',arrayBuffer:async()=>new ArrayBuffer(1)};
  const second={name:'next.png',type:'image/png',arrayBuffer:async()=>new ArrayBuffer(1)};
  const a=app(async(url,init={})=>{
    if(url==='/api/v1/generator/source-image' && init.method==='POST') {
      if(rejectUpload) return {ok:false,json:async()=>({error:'Invalid image'})};
      await gate;
    }
    return backend.fetch(url,init);
  });
  a.info={role:'generator'}; await a.refresh();
  a.onImage({target:{files:[first]}}); await a.saveGenerator();
  assert.equal(a.signalError,'Invalid image'); assert.equal(a.imageFile,first); assert.equal(a.signalDirty,true);
  rejectUpload=false;
  const saving=a.saveGenerator(); a.onImage({target:{files:[second]}}); release(); await saving;
  assert.equal(a.signalError,''); assert.equal(a.imageFile,second); assert.equal(a.signalDirty,true);
  const uploads=backend.calls.filter(c=>c.url==='/api/v1/generator/source-image' && c.method==='POST');
  assert.equal(uploads[0].headers['Content-Type'],'image/gif');
});

test('preview slots match the role and normal generator activity is green', () => {
  const a=app(server().fetch); a.info={role:'generator'};
  assert.deepEqual(JSON.parse(JSON.stringify(a.previewSlots)),[{slot:'generated',label:'Generated signal'}]);
  assert.equal(a.generatorSignal.kind,'ok'); assert.equal(a.generatorSignal.label,'Test signal active');
  a.audioOn=false; assert.equal(a.generatorSignal.kind,''); assert.equal(a.generatorSignal.label,'Test signal muted');
  a.stopped=true; assert.equal(a.generatorSignal.kind,''); assert.equal(a.generatorSignal.label,'Test signal stopped');
  a.info={role:'analyzer'}; assert.deepEqual(Array.from(a.previewSlots,p=>p.slot),['input','generated']);
});

test('one Title control edits the persisted source caption through refresh and Apply', async () => {
  const html=fs.readFileSync(path.join(__dirname,'../../web/index.html'),'utf8');
  assert.match(html,/<label>Title <input v-model="source"/);
  assert.doesNotMatch(html,/<label>Source name|<label>Title <input v-model="title"/);
  const backend=server(),a=app(backend.fetch);a.info={role:'generator'};
  backend.state.generator.source='Saved caption';await a.refresh();
  assert.equal(a.source,'Saved caption');a.source='New title';
  for(let i=0;i<4;i++)await a.refresh();assert.equal(a.source,'New title');
  await a.saveGenerator();assert.equal(backend.state.generator.source,'New title');
  assert.equal(a.signalDirty,false);
});
