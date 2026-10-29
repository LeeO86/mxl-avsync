// Offline replay of an explicitly requested iPhone raw diagnostic capture.
#include "avsync/avsync.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using nlohmann::json;
int main(int argc,char**argv){try{
 if(argc!=2&&argc!=3)throw std::runtime_error("usage: diagnostic-replay CAPTURE_DIRECTORY [REFERENCE_QR_JSON]");
 const std::string root=argv[1];
 std::ifstream index(root+"/index.jsonl"),video(root+"/video.gray",std::ios::binary),audio(root+"/audio.f32",std::ios::binary);
 if(!index||!video||!audio)throw std::runtime_error("missing capture files");
 std::ofstream trace(root+"/replay-frames.jsonl");
 avsync::PhoneSession session(2,{});
 if(argc==3){std::ifstream input(argv[2]);std::string payload((std::istreambuf_iterator<char>(input)),{});auto qr=avsync::parse_qr_payload(payload);if(!qr)throw std::runtime_error("invalid reference QR");session.observe_signal(*qr);}
 std::string line;int frames=0,mismatches=0;bool ended=false;
 for(;std::getline(index,line);){
  auto row=json::parse(line);const auto kind=row.at("kind").get<std::string>();
  if(kind=="qr"){auto qr=avsync::parse_qr_payload(row.at("payload").get<std::string>());if(qr)session.observe_signal(*qr);}
  else if(kind=="video"){
   int w=row.at("width"),h=row.at("height");if(w<=0||h<=0||int64_t(w)*h>16777216)throw std::runtime_error("invalid frame size");
   std::vector<uint8_t> bytes(size_t(w)*h);video.seekg(row.at("offset").get<uint64_t>());video.read((char*)bytes.data(),bytes.size());if(!video)throw std::runtime_error("truncated video");
   std::vector<uint16_t> luma(bytes.size());for(size_t i=0;i<bytes.size();++i)luma[i]=bytes[i]*4;
   double time=row.at("timeMs");session.push_video(luma.data(),w,h,w,time,0);++frames;
   auto event=session.latest_video_event();
   if(row.contains("eventSeq")!=bool(event)||(event&&row.contains("eventSeq")&&(event->seq!=row.at("eventSeq").get<uint64_t>()||std::abs(event->time_ms-row.at("eventTimeMs").get<double>())>0.001)))++mismatches;
   json detail={{"timeMs",time},{"located",session.current_picture().found},{"confirmed",session.picture_code_confirmed()}};
   if(event){detail["eventSeq"]=event->seq;detail["eventTimeMs"]=event->time_ms;}
   auto pose=session.current_picture();
   if(pose.found){
    auto marker=avsync::read_located_marker_at_pose(luma.data(),w,h,w,pose,640);
    const auto&r=marker.reading;detail["stripOk"]=r.strip_ok;detail["seq"]=r.seq_low24;detail["phase"]=int(r.state);detail["quality"]=r.quality;detail["contrast"]=r.contrast;detail["framePos"]=r.frame_pos_mod16;
    auto g=avsync::frame_geometry(marker.width,marker.height);double mean=0;int n=0;
    for(int y=g.marker_y+g.marker_side/4;y<g.marker_y+3*g.marker_side/4;++y)for(int x=g.marker_x+g.marker_side/4;x<g.marker_x+3*g.marker_side/4;++x){mean+=marker.canonical[y*marker.width+x];++n;}
    if(n)detail["markerMean"]=mean/n;
    int histogram[1024]={};
    for(int y=g.marker_y+g.marker_side/4;y<g.marker_y+3*g.marker_side/4;++y)for(int x=g.marker_x+g.marker_side/4;x<g.marker_x+3*g.marker_side/4;++x)++histogram[marker.canonical[y*marker.width+x]];
    auto percentile=[&](double q){int sum=0;for(int v=0;v<1024;++v){sum+=histogram[v];if(sum>=n*q)return v;}return 1023;};
    detail["markerP05"]=percentile(.05);detail["markerP50"]=percentile(.5);detail["markerP95"]=percentile(.95);
    auto cell=[&](int bit){double sum=0;int count=0;int ix=std::max(1,g.strip_w/5),iy=std::max(1,g.strip_h/5);for(int y=g.strip_y+iy;y<g.strip_y+g.strip_h-iy;++y)for(int x=g.strip_x+bit*g.strip_w+ix;x<g.strip_x+(bit+1)*g.strip_w-ix;++x){sum+=marker.canonical[y*marker.width+x];++count;}return sum/count;};
    double white=(cell(29)+cell(31))*.5,black=cell(30),threshold=(white+black)*.5;
    int whites=0,blacks=0;for(int value=0;value<1024;++value){if(value>=threshold+(white-black)*.1)whites+=histogram[value];if(value<=threshold-(white-black)*.1)blacks+=histogram[value];}
    detail["markerWhiteFraction"]=double(whites)/n;detail["markerBlackFraction"]=double(blacks)/n;

   }
   trace<<detail.dump()<<'\n';
  }else if(kind=="audio"){
   size_t count=row.at("count");if(count>1920000)throw std::runtime_error("invalid audio block");
   std::vector<float> samples(count);audio.seekg(row.at("offset").get<uint64_t>());audio.read((char*)samples.data(),count*sizeof(float));if(!audio)throw std::runtime_error("truncated audio");
   session.push_audio(samples.data(),count,row.at("timeMs"),row.at("sampleRate"));
  }else if(kind=="end"){ended=true;}
 }
 auto report=session.finish();json result={{"frames",frames},{"locatedFrames",session.located_frames()}};
 result["validFrames"]=session.valid_frames();result["videoEvents"]=report.video_events;result["audioPackets"]=report.audio_timecodes.size();result["traceMismatches"]=mismatches;result["complete"]=ended;
 result["externalReference"]=argc==3;result["packets"]=json::array();for(const auto&p:report.audio_timecodes)result["packets"].push_back({{"sourceId",p.source_id},{"videoSendNs",p.video_send_ns},{"audioSendNs",p.audio_send_ns},{"timeMs",p.time_ms}});
 result["events"]=json::array();for(const auto&e:report.events){json r={{"seq",e.seq},{"videoTimeMs",e.video_time_ms},{"audioTimeMs",e.audio_time_ms},{"reason",e.reason},{"grade",e.grade}};if(e.av_offset_ms)r["offset_ms"]=*e.av_offset_ms;result["events"].push_back(r);}
 std::cout<<result.dump(2)<<'\n';return mismatches?2:0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
