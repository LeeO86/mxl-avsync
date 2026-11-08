/* AV Sync calibrator: mode and timing logic.
 *
 * Plain C with no hardware headers, so it builds for the MCU and for the
 * host unit tests. All times are milliseconds from a free-running counter
 * (wrap-safe), unless the name ends in _us.
 */
#ifndef CAL_LOGIC_H
#define CAL_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#define CAL_MARKER_PULSES 3
#define CAL_TIMING_PULSES 20

typedef enum { CAL_IDLE = 0, CAL_CAMERA = 1, CAL_DISPLAY = 2 } cal_mode_t;
typedef enum { CAL_NONE = 0, CAL_PULSE = 1 } cal_action_t;

typedef struct {
    /* Pulse and click shape */
    uint32_t led_pulse_us;          /* LED on-time per camera pulse */
    uint32_t click_cycles;          /* buzzer cycles per click */
    uint32_t click_half_period_us;  /* 185 us = 2.7 kHz buzzer resonance */

    /* Camera pulse train (times from the train start) */
    uint32_t marker_ms[CAL_MARKER_PULSES]; /* start pattern */
    uint32_t timing_first_ms;       /* first timing pulse */
    uint32_t timing_period_ms;      /* gap between timing pulses */
    uint32_t timing_count;          /* number of timing pulses (<= CAL_TIMING_PULSES) */
    uint32_t train_pause_ms;        /* pause after the last pulse, then repeat */
    uint32_t blank_after_pulse_ms;  /* ignore sensors until pulse end + this */

    /* Mode entry and exit */
    uint32_t dark_mv;               /* dark = both sensors below this */
    uint32_t camera_enter_ms;       /* dark this long (in IDLE) -> CAMERA */
    uint32_t camera_exit_ms;        /* not dark this long (in CAMERA) -> IDLE */
    uint32_t display_exit_ms;       /* no valid edge this long -> IDLE */

    /* Edge checks */
    uint32_t arm_min_diff_mv;       /* |A-B| must be at least this ... */
    uint32_t arm_min_diff_pct;      /* ... and at least this % of max(A,B) */
    uint32_t arm_hold_ms;           /* an arming sample is valid this long */
    uint32_t lockout_ms;            /* ignore edges this long after a click */
} cal_cfg_t;

typedef struct {
    cal_cfg_t cfg;
    cal_mode_t mode;

    bool dark_valid;  uint32_t dark_since;  /* start of the current dark run */
    bool lit_valid;   uint32_t lit_since;   /* start of the current lit run (camera) */

    int armed_sign;   uint32_t armed_until; /* +1: A brighter, -1: B brighter, 0: none */

    bool clicked;     uint32_t last_click;
    uint32_t last_edge;                     /* last valid edge (display) */

    uint32_t train_start;
    uint32_t pulse_idx;                     /* next pulse in the train */
    bool pulse_active;                      /* pulse running: ignore sensors */
    bool blank_valid; uint32_t blank_until;
} cal_state_t;

/* Defaults as documented in docs/calibrator.md */
void cal_default_cfg(cal_cfg_t *cfg);

void cal_init(cal_state_t *s, const cal_cfg_t *cfg);

/* One ADC sample of both sensor nodes (mV, more light = higher). */
void cal_on_sample(cal_state_t *s, uint32_t now, uint32_t a_mv, uint32_t b_mv);

/* Comparator changed. out = 1 when A is brighter than B.
 * Returns true when the board must click right now. */
bool cal_on_edge(cal_state_t *s, uint32_t now, int out);

/* Call often (every ms). Returns CAL_PULSE when a camera pulse is due. */
cal_action_t cal_tick(cal_state_t *s, uint32_t now);

/* Call when the pulse (LED + click) has finished. */
void cal_pulse_done(cal_state_t *s, uint32_t now);

cal_mode_t cal_mode(const cal_state_t *s);

/* Time of pulse number idx inside one train (ms from the train start). */
uint32_t cal_pulse_offset(const cal_cfg_t *cfg, uint32_t idx);
uint32_t cal_pulses_per_train(const cal_cfg_t *cfg);
uint32_t cal_train_length(const cal_cfg_t *cfg);

#endif
