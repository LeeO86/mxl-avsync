#import <Foundation/Foundation.h>
#import "AVSyncFileAnalyzer.h"
int main(int argc,char **argv) {
 @autoreleasepool {
  NSString *uri=[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]].absoluteString;
  NSDictionary *report=[AVSyncFileAnalyzer analyze:uri calibration:argc>2];
  NSData *json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
  puts([[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
  return report[@"error"] ? 1 : 0;
 }
}
