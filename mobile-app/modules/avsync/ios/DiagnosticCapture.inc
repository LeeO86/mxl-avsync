// Explicit developer diagnostic build only. Never included in ordinary EAS builds.
// The serial capture queue only moves existing buffers into a bounded writer queue.
#include <atomic>
#include <cstdio>
namespace avsync_phone {
class DiagnosticCapture : public std::enable_shared_from_this<DiagnosticCapture> {
 dispatch_queue_t queue=dispatch_queue_create("avsync.raw-diagnostic",DISPATCH_QUEUE_SERIAL);
 FILE *videoFile=nullptr,*audioFile=nullptr,*indexFile=nullptr;
 std::atomic<int> queued{0},droppedVideo{0},droppedAudio{0};
 std::atomic<bool> complete{false},failed{false};
 double origin=-1; bool finishing=false; uint64_t videoBytes=0,audioBytes=0;
 NSString *directory;
 void row(NSDictionary *value){
  NSData *json=[NSJSONSerialization dataWithJSONObject:value options:0 error:nil];
  if(!json||!indexFile||fwrite(json.bytes,1,json.length,indexFile)!=json.length||fputc('\n',indexFile)==EOF)failed=true;
 }
 void finish(){
  if(finishing)return;finishing=true;
  auto self=shared_from_this();dispatch_async(queue,^{
   self->row(@{@"kind":@"end",@"droppedVideo":@(self->droppedVideo.load()),@"droppedAudio":@(self->droppedAudio.load()),@"videoBytes":@(self->videoBytes),@"audioBytes":@(self->audioBytes),@"failed":@(self->failed.load())});
   if(self->videoFile){fclose(self->videoFile);self->videoFile=nullptr;}
   if(self->audioFile){fclose(self->audioFile);self->audioFile=nullptr;}
   if(self->indexFile){fclose(self->indexFile);self->indexFile=nullptr;}
   self->complete=true;
  });
 }
 bool active(double time){
  if(finishing||failed)return false;
  if(origin<0)origin=time;
  if(time-origin>5250){finish();return false;}
  return time-origin<=5000;
 }
 public:
 DiagnosticCapture(){
  NSString *root=NSProcessInfo.processInfo.environment[@"AVSYNC_DEBUG_CAPTURE_DIR"];
  if(!root)root=[NSSearchPathForDirectoriesInDomains(NSCachesDirectory,NSUserDomainMask,YES).firstObject stringByAppendingPathComponent:@"avsync-debug-capture"];
  directory=[root stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  if(![NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil]){failed=true;return;}
  videoFile=fopen([[directory stringByAppendingPathComponent:@"video.gray"] fileSystemRepresentation],"wb");
  audioFile=fopen([[directory stringByAppendingPathComponent:@"audio.f32"] fileSystemRepresentation],"wb");
  indexFile=fopen([[directory stringByAppendingPathComponent:@"index.jsonl"] fileSystemRepresentation],"wb");
  if(!videoFile||!audioFile||!indexFile){failed=true;return;}
  row(@{@"kind":@"start",@"schema":@1,@"input":@"same-luma-pcm-v1",@"durationLimitMs":@5000,@"videoByteLimit":@(512ULL*1024*1024)});
 }
 ~DiagnosticCapture(){if(videoFile)fclose(videoFile);if(audioFile)fclose(audioFile);if(indexFile)fclose(indexFile);}
 void video(std::vector<uint16_t>&& pixels,int width,int height,double time,NSDictionary *trace){
  if(!active(time))return;
  if(queued.load()>=8){++droppedVideo;return;}
  ++queued;auto self=shared_from_this();auto data=std::make_shared<std::vector<uint16_t>>(std::move(pixels));
  dispatch_async(queue,^{
   if(self->videoBytes+data->size()>512ULL*1024*1024){++self->droppedVideo;--self->queued;return;}
   std::vector<uint8_t> bytes(data->size());for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t((*data)[i]/4);
   NSMutableDictionary *entry=[trace mutableCopy];entry[@"kind"]=@"video";entry[@"timeMs"]=@(time);entry[@"width"]=@(width);entry[@"height"]=@(height);entry[@"offset"]=@(self->videoBytes);
   if(fwrite(bytes.data(),1,bytes.size(),self->videoFile)!=bytes.size())self->failed=true;
   self->videoBytes+=bytes.size();self->row(entry);--self->queued;
  });
 }
 void audio(std::vector<float>&& samples,int rate,double time){
  if(!active(time))return;
  if(queued.load()>=8){++droppedAudio;return;}
  ++queued;auto self=shared_from_this();auto data=std::make_shared<std::vector<float>>(std::move(samples));
  dispatch_async(queue,^{
   self->row(@{@"kind":@"audio",@"timeMs":@(time),@"sampleRate":@(rate),@"count":@(data->size()),@"offset":@(self->audioBytes)});
   const size_t bytes=data->size()*sizeof(float);
   if(fwrite(data->data(),1,bytes,self->audioFile)!=bytes)self->failed=true;
   self->audioBytes+=bytes;--self->queued;
  });
 }
 void note(NSDictionary *value){if(finishing||origin<0||failed)return;auto self=shared_from_this();dispatch_async(queue,^{self->row(value);});}
 bool accepting()const{return !finishing&&!failed;}
 NSDictionary *status(){return @{@"directory":directory?:@"",@"complete":@(complete.load()),@"failed":@(failed.load()),@"droppedVideo":@(droppedVideo.load()),@"droppedAudio":@(droppedAudio.load())};}
};
inline std::shared_ptr<DiagnosticCapture> diagnostic_capture(){
 static auto value=[]()->std::shared_ptr<DiagnosticCapture>{
  if(![NSProcessInfo.processInfo.environment[@"AVSYNC_DEBUG_CAPTURE"] isEqual:@"1"])return {};
  return std::make_shared<DiagnosticCapture>();
 }();return value;
}
}
