#import <Foundation/Foundation.h>
#import "AVSyncDeviceCalibration.h"
#include <cmath>
int main(){@autoreleasepool{
 const double d=1500.0/343, own=100.0/343;
 NSMutableDictionary *v=[@{@"aOwnMs":@(25+own),@"aOtherMs":@(535+d),@"bOwnMs":@(637+own),@"bOtherMs":@(127+d),
 @"mABMs":@(127+d),@"mBAMs":@(-65+d),@"loopbackAMs":@(25+own),@"loopbackBMs":@(37+own),@"selfDistanceAM":@0.1,@"selfDistanceBM":@0.1}mutableCopy];
 NSDictionary *unknown=[AVSyncDeviceCalibration solve:v];
 assert(!unknown[@"error"] && ![unknown[@"individualLatenciesAvailable"] boolValue]);
 assert(std::abs([unknown[@"distanceM"] doubleValue]-1.5)<1e-6);
 v[@"trustedClockBMinusAMs"]=@100;v[@"knownInputAMs"]=@5;
 NSDictionary *known=[AVSyncDeviceCalibration solve:v];
 assert([known[@"individualLatenciesAvailable"] boolValue]);
 assert(std::abs([known[@"individualLatenciesMs"][@"outputA"] doubleValue]-20)<1e-6);
 assert(std::abs([known[@"individualLatenciesMs"][@"inputB"] doubleValue]-7)<1e-6);
 assert(std::abs([known[@"individualLatenciesMs"][@"outputB"] doubleValue]-30)<1e-6);
 assert([AVSyncDeviceCalibration solve:@{}][@"error"]);
 puts("Native calibration maths bridge passed: distance, insufficient references, fully referenced split, invalid input");
}}
