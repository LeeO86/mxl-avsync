import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { Resvg } from '@resvg/resvg-js';
import { PNG } from 'pngjs';

const here=path.dirname(fileURLToPath(import.meta.url)),root=path.dirname(here);
const source=fs.readFileSync(path.join(here,'app-icon.svg'),'utf8');
const defs=source.match(/<defs>[\s\S]*?<\/defs>/)?.[0];
const background=source.match(/data-background="(#[0-9a-fA-F]{6})"/)?.[1];
assert(defs&&background,'Master SVG needs defs and data-background');
const svg=body=>`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">${defs}${body}</svg>\n`;
const art='<use href="#artwork"/>';
const branded=source;
const ios=svg(`<rect width="256" height="256" fill="${background}"/>${art}`);
const artwork=PNG.sync.read(new Resvg(svg(art),{fitTo:{mode:'width',value:1024}}).render().asPng());
let radius=0;
for(let y=0;y<1024;++y)for(let x=0;x<1024;++x)if(artwork.data[(y*1024+x)*4+3])radius=Math.max(radius,Math.hypot(x-511.5,y-511.5));
const safeRadius=1024*66/108/2;
const adaptiveScale=(safeRadius-8)/radius;
const adaptiveBody=`<g transform="translate(128 128) scale(${adaptiveScale.toFixed(9)}) translate(-128 -128)">${art}</g>`;
const adaptive=svg(adaptiveBody);
const monochrome=svg(`<style>#artwork * {stroke:#fff;fill:none;opacity:1} #artwork circle {fill:#fff}</style>${adaptiveBody}`);
fs.mkdirSync(path.join(here,'generated'),{recursive:true});
for(const [name,text] of Object.entries({ios,adaptive,monochrome}))fs.writeFileSync(path.join(here,'generated',`app-icon-${name}.svg`),text);
const outputs=[
 ['mobile-app/assets/icon.png',ios,1024,true],
 ['mobile-app/assets/android-icon.png',branded,1024,false],
 ['mobile-app/assets/adaptive-icon.png',adaptive,1024,false],
 ['mobile-app/assets/monochrome-icon.png',monochrome,1024,false],
 ['mobile-app/assets/header-icon.png',branded,256,false],
 ['mobile-app/assets/favicon.png',branded,48,false],
 ['web/icon.png',branded,256,false],
 ['web/apple-touch-icon.png',ios,180,true],
 ['web/favicon-32.png',branded,32,false]
];
for(const [name,text,size,opaque] of outputs) {
 const rendered=new Resvg(text,{fitTo:{mode:'width',value:size}}).render().asPng();
 const image=PNG.sync.read(rendered);
 if(opaque)for(let i=3;i<image.data.length;i+=4)assert.equal(image.data[i],255,`${name}: transparent launcher pixel`);
 if(opaque) {
  const rgb=[1,3,5].map(at=>Number.parseInt(background.slice(at,at+2),16));
  for(let t=0;t<size;++t)for(const index of [t,(size-1)*size+t,t*size,t*size+size-1]) {
   for(let channel=0;channel<3;++channel)assert.equal(image.data[index*4+channel],rgb[channel],`${name}: unexpected baked outer border`);
  }
 }
 const result=opaque?PNG.sync.write(image,{colorType:2,inputColorType:6,inputHasAlpha:true}):rendered;
 fs.writeFileSync(path.join(root,name),result);
 console.log(`${name}: ${size}x${size}${opaque?' opaque RGB':''}`);
}
for(const name of ['adaptive-icon.png','monochrome-icon.png']) {
 const image=PNG.sync.read(fs.readFileSync(path.join(root,'mobile-app/assets',name)));
 for(let y=0;y<1024;++y)for(let x=0;x<1024;++x)if(image.data[(y*1024+x)*4+3])assert(Math.hypot(x-511.5,y-511.5)<=safeRadius,`${name}: artwork outside Android safe circle`);
}
console.log(`Android artwork fits the centered 66/108 safe circle (scale ${adaptiveScale.toFixed(4)}).`);
