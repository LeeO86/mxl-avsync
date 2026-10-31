#import "AVSyncDeviceCalibration.h"
#include "avsync/two_way.hpp"
#include <cmath>

@implementation AVSyncDeviceCalibration
+ (NSDictionary *)solve:(NSDictionary *)input {
 auto number=[&](NSString *key)->std::optional<double>{
  id value=input[key]; if(![value isKindOfClass:NSNumber.class])return std::nullopt;
  double n=[value doubleValue];return std::isfinite(n)?std::optional<double>(n):std::nullopt;
 };
 auto aa=number(@"aOwnMs"), ab=number(@"aOtherMs"), ba=number(@"bOwnMs"), bb=number(@"bOtherMs");
 auto da=number(@"selfDistanceAM"), db=number(@"selfDistanceBM");
 auto mAB=number(@"mABMs"),mBA=number(@"mBAMs"),loopA=number(@"loopbackAMs"),loopB=number(@"loopbackBMs");
 if(!aa||!ab||!ba||!bb||!da||!db||!mAB||!mBA||!loopA||!loopB||*da<0||*db<0||*da>10||*db>10)
  return @{@"error":@"Finite onsets, cross measurements, loopbacks and speaker-to-microphone distances are required"};
 const auto range=avsync::two_way_acoustic({*aa,*ab},{*ba,*bb},*da,*db);
 if(!std::isfinite(range.distance_m)||range.distance_m<0||range.distance_m>1000)
  return @{@"error":@"Recordings imply an invalid acoustic distance"};
 avsync::CrossInput cross;cross.m_ab_ms=*mAB;cross.m_ba_ms=*mBA;cross.loopback_a_ms=*loopA;cross.loopback_b_ms=*loopB;
 cross.distance_m=range.distance_m;cross.self_distance_a_m=*da;cross.self_distance_b_m=*db;
 const auto value=avsync::device_cross(cross,number(@"trustedClockBMinusAMs"),number(@"knownInputAMs"));
 NSMutableDictionary *result=[@{@"experimental":@YES,@"validatedOnDevices":@NO,@"distanceM":@(range.distance_m),
   @"clockOffsetPlusInputSkewMs":@(range.clock_offset_plus_input_skew_ms),@"roundTripMs":@(value.round_trip_ms),
   @"consistencyMs":@(value.consistency_ms),@"individualLatenciesAvailable":@(value.split.has_value()),
   @"limitation":@"Reciprocal recordings alone do not separate clock offset and input latency; a trusted clock and one known latency are required"} mutableCopy];
 if(value.split){const auto&s=*value.split;result[@"individualLatenciesMs"]=@{@"inputA":@(s.in_a_ms),@"outputA":@(s.out_a_ms),@"inputB":@(s.in_b_ms),@"outputB":@(s.out_b_ms)};}
 return result;
}
@end
