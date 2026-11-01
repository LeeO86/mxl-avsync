import AppKit
import AVFoundation
import QuartzCore
import Vision

// Uses the production native renderer/waveform, with one frozen host/UTC map.
// Display/speaker presentation and receiver capture delays remain uncalibrated.
func cg(_ image: NSImage) -> CGImage? { image.cgImage(forProposedRect:nil,context:nil,hints:nil) }
final class Canvas: NSView {
  override var isFlipped: Bool { true }
  let picture=CALayer(), marker=CALayer(), dot=CALayer(), center=CALayer(), foreground=CALayer()
  override init(frame: NSRect) {
    super.init(frame:frame);wantsLayer=true
    layer!.backgroundColor=NSColor.black.cgColor
    for item in [picture,marker,dot,foreground] { layer!.addSublayer(item) }
    dot.addSublayer(center);center.backgroundColor=NSColor.black.cgColor
  }
  required init?(coder:NSCoder){fatalError()}
  override func layout() {
    super.layout()
    let scale=min(bounds.width/1280,bounds.height/720)
    for item in [picture,marker,dot,foreground] {item.setAffineTransform(.identity)}
    picture.frame=CGRect(x:(bounds.width-1280*scale)/2,y:(bounds.height-720*scale)/2,width:1280*scale,height:720*scale)
    foreground.frame=picture.frame
  }
  func rect(_ x:CGFloat,_ y:CGFloat,_ w:CGFloat,_ h:CGFloat)->CGRect {
    let s=picture.bounds.width/1280
    return CGRect(x:picture.frame.minX+x*s,y:picture.frame.minY+y*s,width:w*s,height:h*s)
  }
}
@available(macOS 14.0, *)
final class Sender: NSObject, NSApplicationDelegate, NSWindowDelegate {
  let window=NSWindow(contentRect:NSRect(x:0,y:0,width:1120,height:720),styleMask:[.titled,.closable,.resizable,.miniaturizable],backing:.buffered,defer:false)
  let canvas=Canvas(frame:.zero), status=NSTextField(labelWithString:""), button=NSButton(title:"Start",target:nil,action:nil)
  let engine=AVAudioEngine(), player=AVAudioPlayerNode(), worker=DispatchQueue(label:"avsync.mac.render",qos:.userInitiated)
  var plan:AVSyncGenerator?, peer:PeerClockHost?, link:CADisplayLink?, images=[UInt64:CGImage](), generation=0
  var shown:UInt64=0,next:UInt64=1,lastFrame:UInt64?,skips=0,epoch=0.0,utcMap=0.0,outputLatency=0.0,lastStatus=0.0
  var sleepToken:NSObjectProtocol?,audioToken:NSObjectProtocol?,activity:NSObjectProtocol?
  let rate=CommandLine.arguments.contains("--one-hz") ? 1:2
  func applicationDidFinishLaunching(_ note:Notification) {
    window.title="AVsync · Direct device test";window.delegate=self
    let root=window.contentView!
    canvas.translatesAutoresizingMaskIntoConstraints=false;status.translatesAutoresizingMaskIntoConstraints=false;button.translatesAutoresizingMaskIntoConstraints=false
    for view in [canvas,status,button] {root.addSubview(view)}
    status.maximumNumberOfLines=3;status.font = .monospacedSystemFont(ofSize:12,weight:.regular)
    button.target=self;button.action=#selector(toggle)
    NSLayoutConstraint.activate([
      canvas.topAnchor.constraint(equalTo:root.topAnchor),canvas.leadingAnchor.constraint(equalTo:root.leadingAnchor),canvas.trailingAnchor.constraint(equalTo:root.trailingAnchor),canvas.bottomAnchor.constraint(equalTo:status.topAnchor,constant:-8),
      status.leadingAnchor.constraint(equalTo:root.leadingAnchor,constant:16),status.trailingAnchor.constraint(equalTo:button.leadingAnchor,constant:-16),status.bottomAnchor.constraint(equalTo:root.bottomAnchor,constant:-12),status.heightAnchor.constraint(equalToConstant:56),
      button.trailingAnchor.constraint(equalTo:root.trailingAnchor,constant:-16),button.centerYAnchor.constraint(equalTo:status.centerYAnchor),button.widthAnchor.constraint(equalToConstant:120)])
    engine.attach(player);engine.connect(player,to:engine.mainMixerNode,format:AVAudioFormat(standardFormatWithSampleRate:48000,channels:1))
    sleepToken=NSWorkspace.shared.notificationCenter.addObserver(forName:NSWorkspace.willSleepNotification,object:nil,queue:.main){[weak self] _ in self?.stop("Mac sleeping · start a new source after wake")}
    audioToken=NotificationCenter.default.addObserver(forName:.AVAudioEngineConfigurationChange,object:engine,queue:.main){[weak self] _ in guard let self,self.link != nil else{return};self.stop("Audio configuration changed · start and pair again")}
    window.center();window.makeKeyAndOrderFront(nil);NSApp.activate(ignoringOtherApps:true)
    status.stringValue="Native picture + sound, no WebRTC. Start, allow Local Network, then pair the shown code in AVsync Settings. Green window button enables full screen."
  }
  @objc func toggle(){if link==nil{start()}else{stop("Stopped · restart creates a new source and pairing code")}}
  func start() {
    do {
      engine.prepare();try engine.start();outputLatency=player.outputPresentationLatency
      guard outputLatency.isFinite,outputLatency>=0,outputLatency<1 else {throw PeerClockPacket.error("Audio output timing unavailable")}
      player.play();activity=ProcessInfo.processInfo.beginActivity(options:[.idleDisplaySleepDisabled,.userInitiated],reason:"AVsync direct test signal")
      link=window.displayLink(target:self,selector:#selector(tick(_:)));link?.add(to:.main,forMode:.common);button.title="Stop"
    } catch{stop(error.localizedDescription)}
  }
  func begin(_ target:Double) throws {
    let before=CACurrentMediaTime(),unix=Date().timeIntervalSince1970,after=CACurrentMediaTime()
    utcMap=unix-(before+after)/2;epoch=target+1.5
    let source=AVSyncGenerator(testID:"direct-"+UUID().uuidString,rate:rate,epochTaiNs:Int64((utcMap+37+epoch)*1e9),epochHost:epoch)
    source.setSource(name:"Direct Mac source",subtitle:"Native display and sound · sender-relative clock",image:nil)
    plan=source;let sourceId=(source.geometry()["sourceId"] as! NSNumber).uint32Value
    let host=PeerClockHost(sourceId:sourceId,unixMs:(utcMap+after)*1000,monotonicMs:after*1000,taiUtcSeconds:37)
    try host.start();peer=host;next=1;shown=0;skips=0;lastFrame=nil;generation+=1
    guard let first=cg(source.image(sequence:1)),let fg=cg(source.syncForeground()) else{throw PeerClockPacket.error("Cannot render source")}
    images[1]=first;canvas.picture.contents=first;canvas.foreground.contents=fg
    try fill(after)
  }
  func fill(_ now:Double) throws {
    guard let plan else{return}
    while plan.hostTime(sequence:next)<now+2.5 {
      let host=plan.hostTime(sequence:next)-outputLatency
      guard host>CACurrentMediaTime()+0.03 else{throw PeerClockPacket.error("Audio deadline missed · restart signal")}
      let data=plan.audio(sequence:next),format=AVAudioFormat(standardFormatWithSampleRate:48000,channels:1)!
      let count=AVAudioFrameCount(data.count/4),buffer=AVAudioPCMBuffer(pcmFormat:format,frameCapacity:count)!
      buffer.frameLength=count;_ = data.withUnsafeBytes{memcpy(buffer.floatChannelData![0],$0.baseAddress!,data.count)}
      player.scheduleBuffer(buffer,at:AVAudioTime(hostTime:AVAudioTime.hostTime(forSeconds:host)),options:[])
      if images[next]==nil {let seq=next,gen=generation
        worker.async{[weak self] in let image=cg(plan.image(sequence:seq));DispatchQueue.main.async{[weak self] in guard let self,self.generation==gen,self.plan===plan else{return};self.images[seq]=image}}
      };next+=1
    };images=images.filter{$0.key+1>=shown}
  }
  @objc func tick(_ display:CADisplayLink) {
    do {
      guard engine.isRunning else{throw PeerClockPacket.error("Audio device changed or stopped · restart signal")}
      if plan==nil{try begin(display.targetTimestamp)}
      guard let plan else{return};let now=CACurrentMediaTime();try fill(now)
      guard now-epoch<23*3600,abs(Date().timeIntervalSince1970-now-utcMap)<0.1 else{throw PeerClockPacket.error("Clock changed or session expired · restart and pair again")}
      if display.targetTimestamp>=epoch {
        let frame=plan.frame(at:display.targetTimestamp),seq=(frame["sequence"] as! NSNumber).uint64Value,pos=(frame["position"] as! NSNumber).uint64Value,current=(seq-1)*UInt64(60/rate)+pos
        if let previous=lastFrame,current>previous+1{skips+=Int(current-previous-1)};lastFrame=current
        CATransaction.begin();CATransaction.setDisableActions(true)
        if seq != shown {guard let image=images[seq] else{CATransaction.commit();throw PeerClockPacket.error("Picture deadline missed · restart signal")};shown=seq;canvas.picture.contents=image}
        func n(_ key:String)->CGFloat{CGFloat((frame[key] as! NSNumber).doubleValue)}
        let g=plan.geometry();func v(_ key:String)->CGFloat{CGFloat((g[key] as! NSNumber).doubleValue)}
        canvas.marker.frame=canvas.rect(v("markerX"),v("markerY"),v("markerSide"),v("markerSide"));canvas.marker.backgroundColor=(frame["white"] as? Bool==true ? NSColor.white:NSColor.black).cgColor
        canvas.dot.frame=canvas.rect(n("syncX"),n("syncY"),n("syncSize"),n("syncSize"));canvas.dot.backgroundColor=NSColor(srgbRed:n("syncR")/255,green:n("syncG")/255,blue:n("syncB")/255,alpha:1).cgColor
        let scale=canvas.dot.bounds.width/n("syncSize");canvas.center.frame=CGRect(x:(canvas.dot.bounds.width-2*scale)/2,y:0,width:2*scale,height:canvas.dot.bounds.height)
        CATransaction.commit()
      }
      if now-lastStatus>0.5 {lastStatus=now;let ready=peer?.info()["ready"] as? Bool==true
        status.stringValue="Direct clock \(peer?.code ?? "—") · \(ready ? "ready":"starting — allow Local Network") · \(rate) events/s · display skips \(skips)\nSender-relative estimate · physical display / speaker / capture timing unknown\nAudio presentation lead \(Int((outputLatency*1000).rounded())) ms · no WebRTC"}
    }catch{stop(error.localizedDescription)}
  }
  func stop(_ message:String) {
    link?.invalidate();link=nil;peer?.stop();peer=nil;generation+=1;player.stop();engine.stop();plan=nil;images.removeAll()
    canvas.picture.contents=nil;canvas.foreground.contents=nil;canvas.marker.backgroundColor=NSColor.black.cgColor;canvas.dot.backgroundColor=NSColor.clear.cgColor
    if let activity{ProcessInfo.processInfo.endActivity(activity)};activity=nil;button.title="Start";status.stringValue=message
  }
  func windowWillClose(_ notification:Notification){stop("Closed");NSApp.terminate(nil)}
  func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool{true}
}
func selfTest() throws {
  let plan=AVSyncGenerator(testID:"mac-self-test",rate:2,epochTaiNs:1793450000000000000,epochHost:100)
  plan.setSource(name:"Direct test",subtitle:"Native renderer",image:nil)
  guard let image=cg(plan.image(sequence:1)),image.width==1280,image.height==720,
        (plan.geometry()["sourceId"] as? NSNumber)?.uint32Value != nil,
        plan.audio(sequence:1).count>1000,(plan.frame(at:100)["white"] as? Bool)==true,
        (plan.frame(at:100.2)["white"] as? Bool)==false else{throw PeerClockPacket.error("Native generator self-test failed")}
  let request=VNDetectBarcodesRequest();request.symbologies=[.qr]
  try VNImageRequestHandler(cgImage:image,options:[:]).perform([request])
  guard request.results?.contains(where:{$0.payloadStringValue?.hasPrefix("AVS2:")==true})==true else{throw PeerClockPacket.error("Mac QR raster failed Vision decoding")}
  let rep=NSBitmapImageRep(cgImage:image)
  if let i=CommandLine.arguments.firstIndex(of:"--self-test"),CommandLine.arguments.count>i+1 {
    try rep.representation(using:.png,properties:[:])!.write(to:URL(fileURLWithPath:CommandLine.arguments[i+1]))
  }
  print("PASS native Mac renderer, waveform, source identity and flash schedule")
}
if CommandLine.arguments.contains("--self-test") {do{try selfTest()}catch{fputs("\(error)\n",stderr);exit(1)}}
else if #available(macOS 14.0, *) {
  let app=NSApplication.shared,delegate=Sender();app.setActivationPolicy(.regular);app.delegate=delegate;app.run()
}else{fputs("AVsync direct test requires macOS 14 or newer\n",stderr);exit(1)}
