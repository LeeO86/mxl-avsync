"""Actual AVS2 generator/file-reader round trips, with crop/rotation/codec controls.

Uses the portable testfile tool or the Linux CLI. No manifest is given to the
analyzer: video identity must be decoded from pixels and audio independently.
"""
import argparse
import json
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();binary=args.binary.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)

def run(command,log):
    with log.open('w') as file:subprocess.run([str(x) for x in command],stdout=file,stderr=subprocess.STDOUT,check=True)

def analyze(path,name,expected,minimum=3):
    result=out/(name+'.json')
    run([binary,'analyze','--input',path,'--json',result],out/(name+'-analyze.log'))
    data=json.loads(result.read_text());events=data.get('events',[])
    good=[e for e in events if e.get('av_offset_ms') is not None and e.get('grade')!='invalid']
    if expected is None:assert not good,(name,good)
    else:
        assert len(good)>=minimum,(name,data)
        for e in good:
            assert e.get('audio_timecode',{}).get('valid'),(name,e)
            assert abs(e['av_offset_ms']-expected)<1.1,(name,e)
    return {'case':name,'valid_pairs':len(good),'offsets_ms':[e['av_offset_ms'] for e in good]}

results=[]
for rate in (1,2):
    source=out/f'{rate}hz.mov'
    run([binary,'generate','--format','720p60','--duration','6','--offset','35','--event-rate',rate,
         '--audio-timecode','1200','--container','mov','--video-codec','prores','--audio-codec','pcm24','--out',source],out/f'{rate}hz-generate.log')
    results.append(analyze(source,f'{rate}hz',35))
    for name,filters in [('crop','crop=606:290:337:164'),('rotate','crop=606:290:337:164,transpose=1'),
                         ('camera-tile','scale=960:540,pad=1280:720:160:90:color=gray')]:
        target=out/f'{rate}hz-{name}.mov'
        run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',source,'-vf',filters,
             '-c:v','libx264','-b:v','1M','-pix_fmt','yuv420p','-c:a','pcm_s16le',target],out/f'{rate}hz-{name}-encode.log')
        results.append(analyze(target,f'{rate}hz-{name}',35))
    target=out/f'{rate}hz-no-picture.mov'
    run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',source,'-vf','drawbox=x=0:y=0:w=iw:h=ih:color=gray:t=fill',
         '-c:v','libx264','-pix_fmt','yuv420p','-c:a','copy',target],out/f'{rate}hz-no-picture-encode.log')
    results.append(analyze(target,f'{rate}hz-no-picture',None))
(out/'summary.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
