"""Native audio-only modem experiment; FFmpeg supplies independent real codecs."""
import argparse,array,hashlib,json,math,os,platform,random,shutil,statistics,struct,subprocess,sys,time,wave,zlib

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)

def load_wav(path):
    with wave.open(str(path),'rb') as f:
        assert f.getframerate()==8000 and f.getnchannels()==1 and f.getsampwidth()==2
        data=array.array('h',f.readframes(f.getnframes()))
    if sys.byteorder!='little':data.byteswap()
    return [v/32768 for v in data]

def save_wav(path,data):
    out=array.array('h',(round(max(-0.999,min(0.999,v))*32767) for v in data))
    if sys.byteorder!='little':out.byteswap()
    with wave.open(str(path),'wb') as f:
        f.setnchannels(1);f.setsampwidth(2);f.setframerate(8000);f.writeframes(out.tobytes())

def percentile(values,p):
    if not values:return None
    values=sorted(values);at=(len(values)-1)*p;lo=int(at);hi=min(lo+1,len(values)-1)
    return values[lo]+(at-lo)*(values[hi]-values[lo])

def experiment(binary,out,count,seed,anchor):
    out.mkdir(parents=True,exist_ok=True)
    ffmpeg=['ffmpeg','-hide_banner','-loglevel','error','-y']
    rows=[];violations=[]
    scenarios=[
        ('pcm_8k',None,''),('g711_mulaw','mulaw',''),('g711_alaw','alaw',''),
        ('phone_mulaw','mulaw','highpass=f=300,lowpass=f=3400'),('phone_alaw','alaw','highpass=f=300,lowpass=f=3400'),
        ('quiet_minus20','mulaw','highpass=f=300,lowpass=f=3400,volume=0.1'),
        ('agc','mulaw','highpass=f=300,lowpass=f=3400,acompressor=threshold=0.03:ratio=8:attack=5:release=80:makeup=3'),
        ('clip','mulaw',r'aeval=clip(val(0)*8\,-0.9\,0.9)*0.25'),
        ('resample_44100','mulaw','aresample=44100,aresample=48000'),
        ('drift_plus500ppm','mulaw','asetrate=48024'),('drift_minus500ppm','mulaw','asetrate=47976'),
        ('opus_32k','opus32',''),('opus_16k','opus16',''),
    ]
    mutations=['noise20','noise10','noise6','noise0','noise_minus6','echo_8ms_02','echo_12ms_05','drop10ms','drop20ms','drop40ms','plc20ms','delay3217ms','random_join3141ms','combined']
    for profile,baud,period in [('1200_1hz',1200,1000),('600_1hz',600,1000),('1200_2hz',1200,500)]:
        folder=out/profile;folder.mkdir(exist_ok=True)
        source=folder/'source.wav';manifest=folder/'manifest.json'
        run([binary,'generate','--baud',baud,'--packets',count,'--event-rate',1000//period,'--anchor',anchor,'--seed',seed,'--output',source,'--manifest',manifest])
        truth=json.loads(manifest.read_text());source_events=truth['events']
        def codec(name,codec,filters,source_path=source):
            path=folder/(name+'.wav');filters_args=['-af',filters] if filters else []
            if codec in ['mulaw','alaw']:
                encoded=folder/(name+'.'+codec)
                run([*ffmpeg,'-i',source_path,*filters_args,'-ar',8000,'-ac',1,'-c:a','pcm_'+codec,'-f',codec,encoded])
                run([*ffmpeg,'-f',codec,'-ar',8000,'-ac',1,'-i',encoded,'-c:a','pcm_s16le',path])
            elif codec and codec.startswith('opus'):
                encoded=folder/(name+'.ogg')
                run([*ffmpeg,'-i',source_path,'-c:a','libopus','-b:a',codec[4:]+'k','-application','audio','-frame_duration',20,encoded])
                run([*ffmpeg,'-i',encoded,'-ar',8000,'-c:a','pcm_s16le',path])
            else:run([*ffmpeg,'-i',source_path,*filters_args,'-ar',8000,'-c:a','pcm_s16le',path])
            return path
        cases=[]
        for name,encoding,filters in scenarios:
            path=codec(name,encoding,filters);scale=48000/(48024 if name=='drift_plus500ppm' else 47976) if name.startswith('drift_') else 1
            cases.append((name,path,scale,0,0))
        phone=load_wav(folder/'phone_mulaw.wav')
        for index,name in enumerate(mutations):
            data=phone.copy();scale=1;delay=0;cut=0;rng=random.Random(seed+index)
            if name.startswith('noise') or name=='combined':
                snr={'noise20':20,'noise10':10,'noise6':6,'noise0':0,'noise_minus6':-6,'combined':6}[name]
                sigma=(10**(-12/20)/math.sqrt(2))*10**(-snr/20)
                data=[v+rng.gauss(0,sigma) for v in data]
            if name.startswith('echo_') or name=='combined':
                samples=64 if name=='echo_8ms_02' else 96
                gain=0.2 if name in ['echo_8ms_02','combined'] else 0.5
                direct=data.copy();data.extend([0.]*samples)
                for i,v in enumerate(direct):data[i+samples]+=gain*v
            if name.startswith('drop') or name.startswith('plc'):
                ms=int(name.removeprefix('drop').removeprefix('plc').removesuffix('ms'))
                for event in source_events:
                    payload_start=event['chirp_start_ms']+40+64000/baud
                    payload_duration=(25*8+6)*2000/baud
                    begin=round((payload_start+rng.uniform(20,payload_duration-ms-20))*8);length=ms*8
                    data[begin:begin+length]=data[begin-length:begin] if name.startswith('plc') else [0.]*length
            if name=='delay3217ms':delay=3217;data=[0.]*(delay*8)+data
            if name=='random_join3141ms':cut=3141;data=data[cut*8:]
            path=folder/(name+'.wav');save_wav(path,data);cases.append((name,path,scale,delay,cut))
        for name,path,scale,delay,cut in cases:
            decoded=folder/(name+'.decoded.json');started=time.monotonic()
            run([binary,'analyze','--input',path,'--output',decoded]);elapsed=time.monotonic()-started
            observed=json.loads(decoded.read_text());events={}
            with wave.open(str(path),'rb') as received: duration_ms=received.getnframes()/8
            for event in source_events:
                begin=event['chirp_start_ms']*scale+delay-cut
                end=begin+truth['packet_duration_ms']*scale
                if begin>=0 and end<=duration_ms:events[(event['source'],event['seq'],event['send_tai_ns'])]=event
            found=set();errors=[];incorrect=[];corrections=[]
            for got in observed['packets']:
                key=(got['source'],got['seq'],got['send_tai_ns']);expected=events.get(key)
                if not expected or got['video_offset_ns']!=expected['video_offset_ns'] or got['baud']!=baud or got['anchor_reference']!=anchor:
                    incorrect.append(got);continue
                found.add(key);arrival=expected['anchor_ms']*scale+delay-cut
                errors.append(got['anchor_ms']-arrival);corrections.append(got['hard_changes'])
            row={'profile':profile,'baud':baud,'events_per_second':1000/period,'condition':name,'expected':len(events),'decoded':len(found),'wrong':len(incorrect),'decode_percent':100*len(found)/len(events) if events else None,'anchor_shift_median_ms':statistics.median(errors) if errors else None,'anchor_shift_abs_p95_ms':percentile([abs(v) for v in errors],.95),'anchor_shift_abs_max_ms':max([abs(v) for v in errors],default=None),'hard_changes_max':max(corrections,default=0),'crc_rejects':observed['crc_rejects'],'prefix_candidates':observed['prefix_candidates'],'decode_wall_seconds':elapsed,'wav_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
            rows.append(row);print(profile,name,f"{len(found)}/{len(events)}",'wrong',len(incorrect),'p95-ms',row['anchor_shift_abs_p95_ms'],flush=True)
            if incorrect:violations.append(f'{profile}/{name}: incorrect accepted payload')
            if name in ['pcm_8k','g711_mulaw','g711_alaw','phone_mulaw','phone_alaw'] and len(found)!=len(events):violations.append(f'{profile}/{name}: incomplete baseline decode')
        for control in ['bad_crc','legacy','noise_only','tone_only']:
            path=folder/(control+'.wav')
            if control in ['bad_crc','legacy']:
                tx=folder/(control+'-48k.wav');run([binary,'generate','--baud',baud,'--packets',count,'--event-rate',1000//period,'--anchor',anchor,'--output',tx,'--manifest',folder/(control+'.manifest.json'),'--'+control.replace('_','-'),1]);path=codec(control,'mulaw','',tx)
            else:
                rng=random.Random(seed+991);size=round(min(20,count*period/1000)*8000)
                data=[rng.gauss(0,.08) for _ in range(size)] if control=='noise_only' else [0.2*math.sin(2*math.pi*1300*i/8000)+0.1*math.sin(2*math.pi*2100*i/8000) for i in range(size)]
                save_wav(path,data)
            decoded=folder/(control+'.decoded.json');run([binary,'analyze','--input',path,'--output',decoded]);observed=json.loads(decoded.read_text())
            row={'profile':profile,'baud':baud,'condition':control,'expected':0,'decoded':len(observed['packets']),'wrong':len(observed['packets']),'decode_percent':None,'prefix_candidates':observed['prefix_candidates'],'crc_rejects':observed['crc_rejects']};rows.append(row)
            print(profile,control,'false packets',row['wrong'],flush=True)
            if row['wrong']:violations.append(f'{profile}/{control}: false accepted packet')
    return {'schema':'mxl-avsync-audio-timecode-benchmark/1','seed':seed,'packets_per_stream':count,'platform':platform.platform(),'ffmpeg':subprocess.check_output(['ffmpeg','-version'],text=True).splitlines()[0],'protocol':{'payload_bytes':25,'fec':'K7 rate 1/2, terminated, soft Viterbi','interleaver':'permutation 73 mod 412','preamble_bits':32,'sync_bits':32,'fsk_bits_per_packet':476,'frequencies_hz':[1300,2100],'tx_sample_rate':48000,'rx_sample_rate':8000,'chirp_ms':30,'guard_ms':10,'reference':'chirp '+anchor,'receiver':'blind audio-only, auto 600/1200; no manifest or image supplied'},'rows':rows,'hard_gate_violations':violations,'limits':['Synthetic/offline channels only; no real phone/call/speaker/microphone validation.','Anchor shift includes channel filter/echo effects; decoding success alone is not timing accuracy.','Stress losses are reported, not hidden by changing thresholds.','Not integrated into the production generator or iPhone app.']}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--anchor',choices=['start','centre'],default='start',help='Start is the new default; centre reproduces historical packets.')
    parser.add_argument('--binary',type=Path,help='Use a prebuilt experiment binary instead of compiling.')
    parser.add_argument('--out',type=Path,default=Path('/tmp/avsync-timecode-results'))
    parser.add_argument('--packets',type=int,default=128)
    parser.add_argument('--seed',type=int,default=20261026)
    parser.add_argument('--cxx',default=os.environ.get('CXX','clang++'))
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    compiled_here=args.binary is None
    if args.binary is None:
        args.binary=args.out/'audio-timecode-probe'
        run([args.cxx,'-std=c++20','-O3','-Wall','-Wextra','-I'+str(ROOT/'core/include'),'-I'+str(ROOT/'third_party/pocketfft'),Path(__file__).with_name('probe.cpp'),ROOT/'core/src/chirp.cpp',ROOT/'core/src/detect.cpp','-o',args.binary])
    body=struct.pack('>BIIQi',0x23 if args.anchor=='start' else 0x21,0x01020304,0x10203040,1792963237123456789,35000000)
    expected=(body+struct.pack('>I',zlib.crc32(body))).hex()
    actual=subprocess.check_output([str(args.binary),'dump-vector','--anchor',args.anchor],text=True).strip()
    assert actual==expected, 'Independent struct/zlib wire-vector mismatch'
    run([args.binary,'self-test'])
    started=time.monotonic()
    report=experiment(args.binary,args.out,args.packets,args.seed,args.anchor)
    report['elapsed_seconds']=time.monotonic()-started
    report['compiler']=subprocess.check_output([args.cxx,'--version'],text=True).splitlines()[0] if compiled_here else 'prebuilt binary (see binary hash)'
    files=[Path(__file__).with_name('probe.cpp'),Path(__file__),ROOT/'core/src/chirp.cpp',ROOT/'core/src/detect.cpp',ROOT/'core/include/avsync/avsync.hpp']
    report['source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    report['binary_sha256']=hashlib.sha256(args.binary.read_bytes()).hexdigest()
    report['independent_wire_vector']=expected
    (args.out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print('REPORT',args.out/'results.json','hard violations',report['hard_gate_violations'],flush=True)
    return bool(report['hard_gate_violations'])

if __name__=='__main__':sys.exit(main())
