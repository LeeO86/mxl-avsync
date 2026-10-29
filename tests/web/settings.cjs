// Real browser + real service regression. No production/lab state is modified.
// BIN=build/mxl-avsync PLAYWRIGHT_MODULE=/path/to/playwright-core node tests/web/settings.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { spawn } = require('node:child_process');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

(async () => {
  const root = path.resolve(__dirname, '../..');
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'avsync-web-'));
  const socket = net.createServer();
  await new Promise(resolve => socket.listen(0, '127.0.0.1', resolve));
  const port = socket.address().port;
  await new Promise(resolve => socket.close(resolve));
  const base = `http://127.0.0.1:${port}`;
  const env = {...process.env};
  for (const key of Object.keys(env)) if (/^(AVSYNC_|NMOS_|MXL_|WEB_)/.test(key)) delete env[key];
  Object.assign(env, {AVSYNC_ROLE:'generator', NMOS_ENABLE:'false', WEB_PORT:String(port),
    AVSYNC_WEB_ROOT:path.join(root,'web'), AVSYNC_STATE_DIR:path.join(work,'state'),
    MXL_DOMAIN_SCAN_PATH:path.join(work,'mxl'), MXL_OUTPUT_DOMAIN_DIR:path.join(work,'mxl','gen'), MXL_CLEANUP_ON_EXIT:'true'});
  const log = fs.openSync(path.join(work,'service.log'),'w');
  const child = spawn(process.env.BIN || path.join(root,'build/mxl-avsync'), ['serve'], {env, stdio:['ignore',log,log]});
  const exited = new Promise(resolve => child.once('exit',resolve));
  let browser;
  try {
    for (let attempt=0; attempt<100; attempt++) {
      try { if ((await fetch(base+'/livez')).ok) break; } catch {}
      if (child.exitCode !== null) throw new Error('Service exited before HTTP startup');
      await delay(100);
    }
    browser = await chromium.launch({headless:true, ...(process.env.CHROMIUM_PATH ? {executablePath:process.env.CHROMIUM_PATH} : {})});
    const page = await browser.newPage();
    const errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    let inputPreviewRequests=0;
    page.on('request',request=>{ if(request.url().includes('/api/v1/preview/input.jpg')) inputPreviewRequests++; });
    let polls=0;
    page.on('response',response=>{ if(response.url()===base+'/api/v1/status') polls++; });
    await page.goto(base+'/#Signal');
    const signal = page.getByTestId('signal-form');
    const title = signal.getByLabel('Title',{exact:true});
    await title.waitFor();
    await title.fill('Typing survives live refresh');
    const before=polls;
    await delay(3300);
    assert.ok(polls-before>=3, 'At least three real status polls must have run');
    assert.equal(await title.inputValue(),'Typing survives live refresh');
    await signal.getByText('Unsaved changes',{exact:true}).waitFor();
    await signal.getByRole('button',{name:'Apply',exact:true}).click();
    await signal.getByText('Unsaved changes',{exact:true}).waitFor({state:'hidden'});
    assert.equal((await (await fetch(base+'/api/v1/generator')).json()).title,'Typing survives live refresh');
    await page.reload();
    assert.equal(await title.inputValue(),'Typing survives live refresh');

    // Generator has no input preview; activity belongs with normal header status.
    assert.equal(inputPreviewRequests,0);
    assert.equal(await page.getByTestId('preview-input').count(),0);
    const activity=page.getByTestId('generator-status');
    assert.equal(await activity.textContent(),'Test signal active');
    assert.match(await activity.getAttribute('class'), /\bok\b/);
    await signal.getByRole('button',{name:'Mute',exact:true}).click();
    await activity.filter({hasText:'Test signal muted'}).waitFor();
    assert.doesNotMatch(await activity.getAttribute('class'), /\b(ok|bad)\b/);
    await signal.getByRole('button',{name:'Unmute',exact:true}).click();
    await activity.filter({hasText:'Test signal active'}).waitFor();
    await signal.getByRole('button',{name:'Stop',exact:true}).click();
    await activity.filter({hasText:'Test signal stopped'}).waitFor();
    await signal.getByRole('button',{name:'Start',exact:true}).click();
    await activity.filter({hasText:'Test signal active'}).waitFor();

    // Selecting only a picture enables Apply and survives several refreshes.
    const picker=signal.locator('input[type=file]');
    await picker.setInputFiles(path.join(root,'tests/fixtures/transparent.png'));
    assert.equal(await signal.getByRole('button',{name:'Apply',exact:true}).isEnabled(),true);
    const imageBefore=polls; await delay(3300); assert.ok(polls-imageBefore>=3);
    await signal.getByRole('button',{name:'Apply',exact:true}).click();
    await signal.getByText('Unsaved changes',{exact:true}).waitFor({state:'hidden'});
    assert.equal((await (await fetch(base+'/api/v1/generator')).json()).source_image,true);
    const stored=Buffer.from(await (await fetch(base+'/api/v1/generator/source-image')).arrayBuffer());
    assert.deepEqual(stored,fs.readFileSync(path.join(root,'tests/fixtures/transparent.png')));
    assert.equal(await picker.inputValue(),'');
    await picker.setInputFiles(path.join(root,'tests/fixtures/blink.gif'));
    await signal.getByRole('button',{name:'Discard',exact:true}).click();
    await signal.getByText('Unsaved changes',{exact:true}).waitFor({state:'hidden'});
    assert.equal(await picker.inputValue(),'');

    // Missing previews display text, and a later good frame recovers without reload.
    let previewAvailable=false;
    await page.route('**/api/v1/preview/generated.jpg?*',route=>route.fulfill(previewAvailable
      ? {status:200,contentType:'image/png',body:fs.readFileSync(path.join(root,'tests/fixtures/transparent.png'))}
      : {status:503,contentType:'application/json',body:'{"error":"no preview yet"}'}));
    const preview=page.getByTestId('preview-generated');
    await preview.getByRole('status').waitFor();
    assert.equal(await preview.locator('img').isVisible(),false);
    previewAvailable=true;
    await preview.locator('img').waitFor({state:'visible'});
    await preview.getByRole('status').waitFor({state:'hidden'});
    await page.unroute('**/api/v1/preview/generated.jpg?*');

    // A successful own write must not erase a newer edit made while it was in flight.
    await page.route('**/api/v1/generator',async route=>{
      if(route.request().method()==='PATCH') await delay(700);
      await route.continue();
    });
    await title.fill('Submitted draft');
    await signal.getByRole('button',{name:'Apply',exact:true}).click();
    await title.fill('Typed during save');
    await signal.getByRole('button',{name:'Apply',exact:true}).waitFor();
    assert.equal(await title.inputValue(),'Typed during save');
    assert.equal((await (await fetch(base+'/api/v1/generator')).json()).title,'Submitted draft');
    await signal.getByRole('button',{name:'Discard',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('[data-testid="signal-form"] input[maxlength="80"]').value==='Submitted draft');

    await page.getByRole('button',{name:'Settings',exact:true}).click();
    const settings=page.getByTestId('settings-form');
    const notes=settings.getByLabel('AVSYNC_NOTES',{exact:true});
    await notes.fill('Settings draft survives refresh');
    const settingsBefore=polls;
    await delay(3300);
    assert.ok(polls-settingsBefore>=3);
    assert.equal(await notes.inputValue(),'Settings draft survives refresh');
    await settings.getByRole('button',{name:'Apply',exact:true}).click();
    await settings.getByText('Unsaved changes',{exact:true}).waitFor({state:'hidden'});
    assert.equal((await (await fetch(base+'/api/v1/config')).json()).AVSYNC_NOTES,'Settings draft survives refresh');
    await notes.fill('Discard this edit');
    await settings.getByRole('button',{name:'Discard',exact:true}).click();
    await page.waitForFunction(()=>Array.from(document.querySelectorAll('label')).find(el=>el.textContent.trim()==='AVSYNC_NOTES').querySelector('input').value==='Settings draft survives refresh');
    assert.deepEqual(errors,[]);
    console.log('web UI: drafts, image upload/Discard, role previews/recovery and generator status passed');
  } catch(error) {
    console.error(fs.readFileSync(path.join(work,'service.log'),'utf8').slice(-3000));
    throw error;
  } finally {
    if(browser) await browser.close();
    if(child.exitCode===null) { child.kill('SIGTERM'); await exited; }
    fs.closeSync(log);
    fs.rmSync(work,{recursive:true,force:true});
  }
})().catch(error=>{ console.error(error);process.exitCode=1; });
