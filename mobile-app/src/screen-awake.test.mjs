import test from 'node:test';
import assert from 'node:assert/strict';
import {screenAwakeEnabled,measurementNeedsScreen} from './screen-awake.mjs';
const live={enabled:true,foreground:true,tab:'measure',running:true,captureAvailable:true,cameraGranted:true,micGranted:true};
test('screen awake follows active capture, calibration review, foreground and the persisted preference',()=>{
 assert.equal(screenAwakeEnabled(),true);assert.equal(screenAwakeEnabled({keepScreenAwake:false}),false);
 assert.equal(measurementNeedsScreen(live),true);
 for(const change of [{foreground:false},{enabled:false},{running:false},{tab:'results'},{tab:'generator'},{cameraGranted:false},{micGranted:false},{captureAvailable:false}])assert.equal(measurementNeedsScreen({...live,...change}),false);
 assert.equal(measurementNeedsScreen({...live,tab:'results',running:false,flow:'review'}),true);
 assert.equal(measurementNeedsScreen({...live,flow:'review',foreground:false}),false);
 assert.equal(measurementNeedsScreen({...live,flow:'review',enabled:false}),false);
});
