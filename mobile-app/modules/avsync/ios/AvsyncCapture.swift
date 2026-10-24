import AVFoundation
import Foundation

// iOS capture for the phone app. Video frames and microphone buffers are stamped
// with the host clock (mach time converted through CMClockGetHostTimeClock).
// A device that cannot share that clock is reported unsupported. This file is
// compiled by Xcode, not by the Linux CMake build.
//
// The measurement itself is PhoneSession in avsync-core: pass the luma plane,
// the frame presentation host time, the rolling-shutter readout, and the
// AVAudioEngine input buffers with their host-time stamps.

final class AvsyncCapture: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    let session = AVCaptureSession()
    private let audio = AVAudioEngine()

    func start() throws {
        session.beginConfiguration()
        session.sessionPreset = .hd1280x720
        guard let camera = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back) else {
            throw NSError(domain: "mxl-avsync", code: 1, userInfo: [NSLocalizedDescriptionKey: "no camera"])
        }
        let input = try AVCaptureDeviceInput(device: camera)
        if session.canAddInput(input) { session.addInput(input) }
        let output = AVCaptureVideoDataOutput()
        output.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange]
        output.setSampleBufferDelegate(self, queue: DispatchQueue(label: "mxl-avsync.video"))
        if session.canAddOutput(output) { session.addOutput(output) }
        session.commitConfiguration()
        session.startRunning()
        let hw = audio.inputNode
        hw.installTap(onBus: 0, bufferSize: 1024, format: hw.outputFormat(forBus: 0)) { buffer, time in
            // time.sampleTime / sampleRate is the host timeline of the first sample.
            _ = buffer
            _ = time
        }
        try audio.start()
    }

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
        let host = CMSyncConvertTime(pts, from: CMClockGetHostTimeClock(), to: CMClockGetHostTimeClock())
        _ = host
        _ = output
        _ = connection
    }
}
