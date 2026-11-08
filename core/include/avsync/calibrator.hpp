#pragma once

// The AV Sync calibrator board (docs/calibrator.md, signal contract v1): what the
// apps do with its flashes and clicks, and the calibration pattern every generator
// can show. Pure maths on samples and frames; no I/O. Times are milliseconds on one
// capture clock (camera and microphone of the same phone).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace avsync {

// Numbers from the contract ("Timing summary").
struct CalibratorContract {
    static constexpr double click_hz = 2700;      // buzzer: 3 cycles of a square wave
    static constexpr double led_pulse_ms = 1.5;    // camera mode LED on time
    static constexpr double swap_period_ms = 500;  // display mode: pattern halves swap
    static constexpr double sound_delay_ms = 100;  // D: generator sound after each swap
    static constexpr double beep_hz = 1500;        // the generator's own sound (not the buzzer's 2.7 kHz)
    static constexpr double beep_ms = 4;           // 6 cycles at 1.5 kHz
    static constexpr double sound_ms_per_cm = 0.0291; // speed of sound, 343 m/s
};

// A short sound in a recording: a calibrator click or the generator's beep.
struct SoundOnset {
    double time_ms = 0;     // where the envelope first reaches 25 % of its peak
    double peak = 0;        // envelope peak (RMS over 0.25 ms, full scale 1)
    double click_share = 0; // energy at 2.7 kHz / (2.7 kHz + 1.5 kHz) in the 3 ms after the onset
    bool click() const { return click_share >= 0.5; }
};
std::vector<SoundOnset> find_sound_onsets(const float* mono, size_t count, int sample_rate, double start_ms);

// One camera frame, reduced to the mean luma of equal row bands (top to bottom, 0..255).
struct FlashFrame {
    double time_ms = 0;     // exposure start of the first row
    double exposure_ms = 0; // 0: unknown
    double readout_ms = 0;  // first to last row; 0: unknown
    std::vector<float> rows;
};
struct FlashOnset {
    double time_ms = 0;
    bool edge_in_frame = false; // the start of the flash lies inside the frame (rolling shutter edge seen)
    double row = 0;             // where that edge is, 0 top .. 1 bottom
    bool row_timed = false;     // time_ms includes the row's readout delay (readout known or estimated)
};
// Start of each LED flash. When no frame knows its readout time, it is estimated from
// the height of the lit band (max(exposure, LED time) / band height); *readout_ms
// returns what was used (0: none, then camera_calibration fits it).
std::vector<FlashOnset> find_flash_onsets(const std::vector<FlashFrame>& frames, double led_ms = CalibratorContract::led_pulse_ms,
    double* readout_ms = nullptr);

struct CalibratorResult {
    bool ok = false;
    std::string error;          // for the user when !ok
    double offset_ms = 0;       // camera: microphone minus camera; display: sound output minus picture output
    double uncertainty_ms = 0;  // standard error of the median of the pairs kept
    double spread_ms = 0;       // 10-90 % range of the pairs kept
    int pairs = 0;              // pairs kept
    int rejected = 0;           // pairs dropped as outliers
    double direction_ms = 0;    // display: median of one swap direction minus the other (sensor mismatch)
    double readout_ms = 0;      // camera: readout fitted from the pulses (0 when the frames knew it or too few edges)
    std::vector<double> values; // per kept pair
};

struct CameraCalibrationOptions {
    double buzzer_delay_ms = 0;  // coil on to sound; still to be measured per buzzer type (contract open point)
    double mic_distance_cm = 1.5; // buzzer to microphone
    int min_pairs = 8;
};
// Camera mode: flash and click start together; offset = click - flash per pulse. When the
// flash times are not row-timed, the readout is fitted: the flash phase against the frames
// is random, so (click - flash) grows linearly with the edge row by the readout time.
CalibratorResult camera_calibration(const std::vector<FlashOnset>& flashes, const std::vector<SoundOnset>& sounds,
    const CameraCalibrationOptions& options = {});

struct DisplayCalibrationOptions {
    double sound_delay_ms = CalibratorContract::sound_delay_ms;
    double buzzer_delay_ms = 0;
    double click_minus_speaker_path_cm = 0; // calibrator-to-mic distance minus speaker-to-mic distance
    int min_pairs = 8;
};
// Display mode: each swap gives a click (when the screen changes) and then the generator's
// beep D after the swap (when the speaker plays it). offset = (beep - click) - D, so the
// microphone delay cancels. Both swap directions count equally.
CalibratorResult display_calibration(const std::vector<SoundOnset>& sounds, const DisplayCalibrationOptions& options = {});

// The calibration pattern, the same for all generators. Time is from the pattern start;
// swap k happens at k * 500 ms. In even periods the left half is white.
bool calibration_left_white(double ms_since_start);
uint64_t calibration_period(double ms_since_start);
// The generator's beep (mono, starts at sample 0, peak 0.5), played D after each swap.
std::vector<float> calibration_beep(int sample_rate);

} // namespace avsync
