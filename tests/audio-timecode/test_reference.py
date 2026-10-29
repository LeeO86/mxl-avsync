"""Regression checks for rate selection and self-describing chirp timestamps."""
import json
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


def run(*args):
    return subprocess.run([str(v) for v in args], check=True, capture_output=True)


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='avsync-reference-') as directory:
        folder = Path(directory)
        # Invalid combinations must fail before writing/truncating an output file.
        for flags in [('--event-rate','0'), ('--event-rate','4'), ('--event-rate','1.5'),
                      ('--event-rate','2','--baud','600'),
                      ('--event-rate','1','--period-ms','500'),
                      ('--period-ms','nan'), ('--period-ms','500oops'), ('--anchor','unknown')]:
            dest=folder/'invalid.wav'
            result=subprocess.run([str(binary),'generate',*flags,'--output',str(dest)],capture_output=True)
            assert result.returncode != 0 and not dest.exists(), flags

        manifests={}
        mixed=[]
        for rate in (1,2):
            for reference in ('start','centre'):
                stem=folder/f'{rate}-{reference}'
                tx=stem.with_suffix('.wav'); manifest=stem.with_suffix('.json')
                run(binary,'generate','--packets',8,'--event-rate',rate,'--anchor',reference,
                    '--output',tx,'--manifest',manifest)
                truth=json.loads(manifest.read_text()); manifests[rate,reference]=truth
                assert truth['events_per_second']==rate and truth['anchor_reference']==reference
                assert truth['events'][0]['anchor_ms']==(100 if reference=='start' else 115)
                assert all(b['anchor_ms']-a['anchor_ms']==1000/rate
                           for a,b in zip(truth['events'],truth['events'][1:]))
                # G.711 A and mu law, phone band, and a known +35-ms arrival delay.
                for codec in ('alaw','mulaw'):
                    encoded=folder/f'{rate}-{reference}.{codec}'
                    rx=folder/f'{rate}-{reference}-{codec}.wav'
                    decoded=folder/f'{rate}-{reference}-{codec}.json'
                    run('ffmpeg','-v','error','-y','-i',tx,'-af','highpass=f=300,lowpass=f=3400',
                        '-ar',8000,'-ac',1,'-c:a','pcm_'+codec,'-f',codec,encoded)
                    run('ffmpeg','-v','error','-y','-f',codec,'-ar',8000,'-ac',1,'-i',encoded,
                        '-af','adelay=35','-c:a','pcm_s16le',rx)
                    run(binary,'analyze','--input',rx,'--output',decoded)
                    packets=json.loads(decoded.read_text())['packets']
                    assert len(packets)==8, (rate,reference,codec,len(packets))
                    for expected,got in zip(truth['events'],packets):
                        for key in ('source','seq','send_tai_ns','video_offset_ns'):
                            assert got[key]==expected[key], (key,expected,got)
                        assert got['anchor_reference']==reference
                        assert abs(got['anchor_ms']-expected['anchor_ms']-35)<0.15, got
                    if rate==2 and codec=='mulaw':
                        with wave.open(str(rx),'rb') as wav:
                            mixed.append(wav.readframes(wav.getnframes()))

            onset=manifests[rate,'start']['events']; centre=manifests[rate,'centre']['events']
            assert all(c['send_tai_ns']-s['send_tai_ns']==15000000 for s,c in zip(onset,centre))
            with wave.open(str(folder/f'{rate}-start.wav'),'rb') as a, \
                 wave.open(str(folder/f'{rate}-centre.wav'),'rb') as b:
                # Same actual chirp; only the protected reference and its time differ.
                assert a.readframes(6240)==b.readframes(6240)

        # One blind decoder handles both references in one recording.
        rx=folder/'mixed.wav'; decoded=folder/'mixed.json'
        # Use a distinct source so both reference styles survive identity deduplication.
        run(binary,'generate','--packets',8,'--event-rate',2,'--anchor','centre','--seed',987,
            '--output',folder/'distinct.wav','--manifest',folder/'distinct.json')
        run('ffmpeg','-v','error','-y','-i',folder/'distinct.wav','-ar',8000,'-c:a','pcm_s16le',folder/'distinct-8k.wav')
        with wave.open(str(folder/'distinct-8k.wav'),'rb') as wav:
            distinct=wav.readframes(wav.getnframes())
        with wave.open(str(rx),'wb') as wav:
            wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(8000)
            wav.writeframes(mixed[0]+distinct)
        run(binary,'analyze','--input',rx,'--output',decoded)
        packets=json.loads(decoded.read_text())['packets']
        assert len(packets)==16
        assert [p['anchor_reference'] for p in packets]==['start']*8+['centre']*8
    print('PASS: 1/2 Hz, rejected overlaps/options, both references, G.711 A/mu-law +35 ms, mixed recording')


if __name__=='__main__':
    main()
