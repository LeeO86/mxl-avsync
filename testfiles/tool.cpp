#include "file_tool.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
int main(int argc,char**argv){
 if(argc<2)return 78;
 std::map<std::string,std::string>args;
 for(int i=2;i+1<argc;i+=2)args[argv[i]]=argv[i+1];
 auto value=[&](std::string key,std::string fallback){return args.count(key)?args[key]:fallback;};
 std::string error;int result=78;
 if(std::string(argv[1])=="generate"){
  avsync::GenerateRequest r;
  r.format=value("--format","1080p50");r.duration_s=std::stod(value("--duration","10"));r.offset_ms=std::stod(value("--offset","0"));
  r.container=value("--container","mp4");r.video_codec=value("--video-codec","h264");r.audio_codec=value("--audio-codec","aac");r.out_path=value("--out","");
  r.test_id=value("--test-id","TEST-FILES");r.title="AV-SYNC TEST SIGNAL";r.source=r.test_id;r.subtitle="Expected audio offset: "+value("--offset","0")+" ms";
  r.allow_container_shift=true;result=avsync::run_generate(r,error);
 }else if(std::string(argv[1])=="analyze"){
  avsync::AnalyzeRequest r;r.path=value("--input","");r.manifest=value("--manifest","");r.json_out=value("--json","");result=avsync::run_analyze(r,error);
 }
 if(result)std::cerr<<error<<"\n";
 return result;
}
