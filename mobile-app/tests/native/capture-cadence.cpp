#include "avsync/avsync.hpp"
#include "third_party/nayuki/qrcodegen.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace avsync;
int main() {
 constexpr int w=640,h=360;
 const auto g=qr_flash_geometry(w,h);const double unit=g.qr_size/41.0;
 for(int fps:{60,120,240})for(int rate:{1,2})for(bool adaptive:{false,true})for(bool jitter:{false,true}) {
  double lastScan=-1e30;int scans=0;
  const auto onset=[&](int k){return k*1000.0/rate+(jitter&&k ? (k%2 ? -35.0:35.0):0.0);};
  QrFlashTracker tracker;std::vector<VideoObservation> events;
  std::vector<uint16_t> pixels(w*h,512);
  for(int i=0;i<fps*4;++i) {
   const double time=3+i*1000.0/fps,period=1000.0/rate;
   QrPayload p;p.timecode=p.qr_flash=true;p.source_id=5;p.test_id="cadence";
   int event=0;while(event+1<4*rate && time>=onset(event+1))++event;
   p.rate=rate;p.k=1+event;p.tk=1793300000000000000LL+int64_t(p.k-1)*(1000000000/rate);p.format="1080p50";
   const auto symbol=qrcodegen::QrCode::encodeSegments(qrcodegen::QrSegment::makeSegments(qr_payload_text(p).c_str()),qrcodegen::QrCode::Ecc::QUARTILE,4,4,-1,false);
   for(int y=g.qr_y;y<g.qr_y+g.qr_size;++y)for(int x=g.qr_x;x<g.qr_x+g.qr_size;++x) {
    const int mx=int((x-g.qr_x)/unit)-4,my=int((y-g.qr_y)/unit)-4;
    pixels[y*w+x]=symbol.getModule(mx,my)?64:940;
   }
   for(int y=g.marker_y;y<g.marker_y+g.marker_side;++y)for(int x=g.marker_x;x<g.marker_x+g.marker_side;++x)pixels[y*w+x]=time-onset(event)<40?940:64;
   if(time-lastScan>=100-.001 && (!adaptive||tracker.qr_scan_suggested(time))) {
    lastScan=time;++scans;
    QrDetection q;q.payload=p;
    q.x={(g.qr_x+4*unit)/w,(g.qr_x+37*unit)/w,(g.qr_x+37*unit)/w,(g.qr_x+4*unit)/w};
    q.y={(g.qr_y+4*unit)/h,(g.qr_y+4*unit)/h,(g.qr_y+37*unit)/h,(g.qr_y+37*unit)/h};
    tracker.observe(q,time);
   }
   VideoObservation frame;frame.time_ms=time;tracker.push(pixels.data(),w,h,w,frame);
   auto found=tracker.take_events();events.insert(events.end(),found.begin(),found.end());
  }
  assert(events.size()==size_t(4*rate-1));
  for(size_t i=0;i<events.size();++i) {
   const double expected=onset(int(i)+1);
   assert(events[i].seq==i+2 && events[i].timecode);
   assert(events[i].time_ms>=expected && events[i].time_ms-expected<=1000.0/fps+0.01);
  }
  const auto& d=tracker.diagnostics();
  assert(d.frames==uint64_t(4*fps) && d.symbol_frames<d.frames*.15);
  assert(d.match_calls<=uint64_t(scans)+d.symbol_frames);
  assert(d.module_reads==d.symbol_frames*1089 && d.samples.size()<=64);
  assert(d.emitted==events.size());
  if(adaptive && rate==1 && fps==240)assert(scans<35);
  std::cout<<fps<<" fps "<<rate<<" Hz adaptive="<<adaptive<<" jitter="<<jitter<<" scans="<<scans<<" symbolFrames="<<d.symbol_frames<<" matchCalls="<<d.match_calls<<"\n";
 }
 std::cout<<"PASS: AVS2 pulse/identity at 60/120/240 fps, 1/2 Hz, within one capture interval (synthetic pixels, known QR geometry)\n";
}
