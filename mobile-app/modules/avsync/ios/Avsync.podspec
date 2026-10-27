Pod::Spec.new do |s|
  s.name = 'Avsync'
  s.version = '1.0.0'
  s.summary = 'Recorded AV sync analysis using avsync-core'
  s.description = s.summary
  s.license = 'MIT'
  s.author = 'LeeO86'
  s.homepage = 'https://github.com/LeeO86/mxl-avsync'
  s.source = { :git => s.homepage }
  s.platform = :ios, '15.1'
  s.swift_version = '5.0'
  s.static_framework = true
  s.dependency 'ExpoModulesCore'
  s.frameworks = 'AVFoundation', 'UIKit', 'CoreMedia', 'CoreVideo', 'CoreGraphics', 'Vision'
  s.source_files = '*.{h,mm,cpp,swift}'
  s.exclude_files = 'AvsyncCapture.swift'
  s.public_header_files = 'AVSyncFileAnalyzer.h', 'AVSyncStreamAnalyzer.h', 'AVSyncQRCodeReader.h', 'AVSyncGenerator.h'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'GCC_OPTIMIZATION_LEVEL' => '2',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20',
    'HEADER_SEARCH_PATHS' => '$(inherited) "$(PODS_TARGET_SRCROOT)/../../../../core/include" "$(PODS_TARGET_SRCROOT)/../../../../third_party/pocketfft"'
  }
end
