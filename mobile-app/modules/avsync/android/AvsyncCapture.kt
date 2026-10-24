package io.leeo86.mxl.avsync

import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraMetadata
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder

// Android capture. SENSOR_INFO_TIMESTAMP_SOURCE must be REALTIME so camera
// timestamps share CLOCK_BOOTTIME / CLOCK_MONOTONIC with AAudio. A device that
// reports UNKNOWN is unsupported. Compiled by the Android SDK, not by CMake.
//
// Frames and mic buffers are handed to PhoneSession (avsync-core) on that one clock.

class AvsyncCapture {
    fun sensorClockIsRealtime(characteristics: CameraCharacteristics): Boolean {
        val source = characteristics.get(CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE)
        return source == CameraMetadata.SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME
    }

    fun openMic(sampleRate: Int): AudioRecord {
        val min = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT)
        return AudioRecord.Builder()
            .setAudioSource(MediaRecorder.AudioSource.UNPROCESSED)
            .setAudioFormat(
                AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setSampleRate(sampleRate)
                    .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                    .build()
            )
            .setBufferSizeInBytes(min * 2)
            .build()
    }
}
