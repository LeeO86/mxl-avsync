import test from 'node:test';
import assert from 'node:assert/strict';
import { generatorOptions } from './generator.mjs';
test('appearance preserves Unicode and stable image identity without altering timing settings', () => {
 const id='66EDD7F5-7F49-4703-ADDD-D780D9558EE6';
 const session={generatorName:'  Studio Zürich\nCamera A ',generatorSubtitle:' Path\tSRT ',generatorImageId:id,generatorRate:1,calibration:{offset_ms:35}};
 assert.deepEqual(generatorOptions(session),{name:'Studio Zürich Camera A',subtitle:'Path SRT',imageId:id});
 assert.equal(session.generatorName,'  Studio Zürich\nCamera A ');
});
test('missing settings have safe defaults; remote/arbitrary image paths are not source IDs', () => {
 assert.deepEqual(generatorOptions({}),{name:'',subtitle:'',imageId:''});
 for(const value of ['https://example.com/photo.png','file:///private/photo.png','../photo.png',null]) assert.equal(generatorOptions({generatorImageId:value}).imageId,'');
 assert.equal(Array.from(generatorOptions({generatorName:'ä'.repeat(100),generatorSubtitle:'ö'.repeat(200)}).name).length,64);
 assert.equal(generatorOptions({generatorSubtitle:'ö'.repeat(200)}).subtitle.length,128);
});
