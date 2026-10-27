"""Exercise the shipped generator, flash, blind file analyzer and audio-only mode.
Usage: python3 tests/integration/timecode.py [build/mxl-avsync]
Also accepts the Mac portable build/full/avsync-testfile-tool.
"""
import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    binary = Path(sys.argv[1] if len(sys.argv)>1 else 'build/mxl-avsync').resolve()
    portable = binary.name == 'avsync-testfile-tool'
    def run(*args):
        return subprocess.run([str(v) for v in args],check=True,capture_output=True)
    with tempfile.TemporaryDirectory(prefix='avsync-v3-') as directory:
        folder=Path(directory)
        cases=[('1200',1,'1080p25',0,7),('1200',2,'1080p50',35,5),
               ('1200',2,'1080p59.94',-100,5),('600',1,'1080p50',100,7),
               ('legacy',2,'1080p50',35,5)]
        for modem,rate,format,offset,duration in cases:
            stem=f'{modem}-{rate}-{format}-{offset}';movie=folder/(stem+'.mkv')
            # Matroska intentionally uses a 1-ms container time base. Keep the
            # CLI's strict default; explicitly opt into this encoded variant.
            run(binary,'generate','--audio-timecode',modem,'--event-rate',rate,'--format',format,
                '--duration',duration,'--offset',offset,'--container','mkv','--allow-container-shift',
                '--video-codec','h264','--audio-codec','pcm16','--out',movie)
            def analyze(path,name):
                report=folder/(name+'.json')
                run(binary,'analyze',*(['--input',path] if portable else [path]),'--json',report)
                return json.loads(report.read_text())
            report=analyze(movie,stem)
            values=report['statistics']['window']['series']
            assert len(values)>=3 and abs(statistics.median(values)-offset)<1,(stem,report)
            if modem=='legacy':
                assert not report['audio_timecodes'];continue
            assert len(report['audio_timecodes'])>=3 and all(p['crc_ok'] for p in report['audio_timecodes']),report
            assert all(e.get('audio_timecode',{}).get('valid') for e in report['events'] if e['grade']!='invalid'),report
            # The first white marker frame is at the first chirp onset when offset=0.
            if offset==0:
                pixels=run('ffmpeg','-v','error','-i',movie,'-vf','crop=16:16:952:532,scale=1:1','-pix_fmt','gray','-f','rawvideo','-').stdout
                first=next(i for i,p in enumerate(pixels) if p>200)
                manifest=json.loads(Path(str(movie)+'.manifest.json').read_text())
                assert first==manifest['events'][0]['video_frame'],(first,manifest)
                assert abs(report['audio_timecodes'][0]['received_ms']-manifest['events'][0]['video_timeline_ms'])<0.15,report
            # A real G.711 roundtrip plus a known 3217-ms path delay. The receiver
            # gets no manifest; whole-second delay must remain unambiguous.
            encoded=folder/(stem+'.alaw');wave=folder/(stem+'.wav')
            run('ffmpeg','-v','error','-y','-i',movie,'-vn','-af','highpass=f=300,lowpass=f=3400',
                '-ar',8000,'-ac',1,'-c:a','pcm_alaw','-f','alaw',encoded)
            run('ffmpeg','-v','error','-y','-f','alaw','-ar',8000,'-ac',1,'-i',encoded,
                '-af','adelay=3217','-c:a','pcm_s16le',wave)
            only=analyze(wave,stem+'-audio-only')
            assert only['stream']['format']=='audio-only' and not only['events'],only
            assert len(only['audio_timecodes'])==len(report['audio_timecodes']),only
            for a,b in zip(report['audio_timecodes'],only['audio_timecodes']):
                assert (a['source_id'],a['audio_send_ns'],a['video_send_ns'])==(b['source_id'],b['audio_send_ns'],b['video_send_ns'])
                assert abs(b['received_ms']-a['received_ms']-3217)<0.2,(a,b)
            assert all('audio_latency_ms' not in e for e in only['events'])
            print('PASS',stem,'pairs',len(values),'median',statistics.median(values),'audio-only',len(only['audio_timecodes']))
    print('PASS: full render/encode/analyze, white-onset alignment, 1/2 Hz, 59.94 grid, 600 fallback, legacy, G.711 +3217 ms, audio-only')


if __name__=='__main__':main()
