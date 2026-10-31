"""Test the actual Swift SNTP codec and Network.framework client against a local UDP server.
macOS/Xcode required. No public time service, phone, credentials or location are used.
"""
from pathlib import Path
import json, socket, struct, subprocess, tempfile, threading, time
ROOT=Path(__file__).resolve().parents[3]
IOS=ROOT/'mobile-app/modules/avsync/ios'
HERE=Path(__file__).resolve().parent

def timestamp(value):
    value+=2208988800
    sec=int(value)
    return struct.pack('!II',sec&0xffffffff,int((value-sec)*2**32))

with tempfile.TemporaryDirectory(prefix='avsync-clock-') as temp:
    out=Path(temp)
    common=['swiftc','-module-cache-path',str(out/'modules'),str(IOS/'SntpPacket.swift')]
    subprocess.run(common+[str(HERE/'clock-packet.swift'),'-o',str(out/'packet')],check=True)
    subprocess.run([out/'packet'],check=True)
    subprocess.run(common+[str(IOS/'PhoneClock.swift'),str(HERE/'clock-network.swift'),'-o',str(out/'network')],check=True)
    for mode in ['valid','unsynchronized','wrong_origin','kiss_of_death']:
        sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',0));sock.settimeout(.1)
        port=sock.getsockname()[1];stop=threading.Event();count=[0]
        def serve():
            while not stop.is_set():
                try:q,address=sock.recvfrom(1024)
                except socket.timeout:continue
                except OSError:return
                count[0]+=1
                received=time.time()+.125
                b=bytearray(48);b[0]=0x24;b[1]=2;b[3]=(-20)&255
                b[24:32]=q[40:48]
                b[32:40]=timestamp(received)
                b[40:48]=timestamp(time.time()+.125)
                if mode=='unsynchronized':b[0]=0xe4
                if mode=='wrong_origin':b[24]^=255
                if mode=='kiss_of_death':b[1]=0
                sock.sendto(b,address)
        worker=threading.Thread(target=serve);worker.start()
        try:
            result=subprocess.run([out/'network',str(port)],text=True,capture_output=True,timeout=15)
            if mode=='valid':
                assert result.returncode==0,result.stderr
                sample=json.loads(result.stdout)
                assert abs(sample['offsetMs']-125)<sample['uncertaintyMs']+2,sample
                assert sample['samples']==2 and sample['timebase']=='ios-host' and sample['authenticated'] is False,sample
                assert sample['referenceMonotonicMs']>0 and sample['clockId'] and sample['uncertaintyMs']<20,sample
                print('Real Swift UDP exchange passed:',json.dumps({k:sample[k] for k in ['source','samples','offsetMs','uncertaintyMs','roundTripMs']}))
            else:
                assert result.returncode!=0,(mode,result.stdout)
                if mode=='kiss_of_death':assert count[0]==1,count
                print('Rejected:',mode)
        finally:stop.set();worker.join();sock.close()
print('Native SNTP codec + network tests passed')
