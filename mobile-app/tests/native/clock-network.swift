import Foundation
@main struct ClockNetworkProbe {
  static func main() {
    do {
      let port = UInt16(CommandLine.arguments[1])!
      let result = try SntpClient().synchronize(host:"127.0.0.1",port:port,samples:2)
      let data = try JSONSerialization.data(withJSONObject:result,options:.sortedKeys)
      print(String(data:data,encoding:.utf8)!)
    } catch {
      fputs("Clock unavailable: \(error.localizedDescription)\n",stderr)
      exit(1)
    }
  }
}
