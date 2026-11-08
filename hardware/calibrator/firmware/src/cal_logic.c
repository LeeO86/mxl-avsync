/* AV Sync calibrator: mode and timing logic. See cal_logic.h and
 * docs/calibrator.md for the signal contract. */
#include "cal_logic.h"

#include <string.h>

/* Wrap-safe "a - b" for a free-running 32-bit ms counter. */
static int32_t since(uint32_t now, uint32_t then) { return (int32_t)(now - then); }

void cal_default_cfg(cal_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->led_pulse_us = 1500;
    c->click_cycles = 3;
    c->click_half_period_us = 185;

    c->marker_ms[0] = 0;
    c->marker_ms[1] = 125;
    c->marker_ms[2] = 250;
    c->timing_first_ms = 1000;
    c->timing_period_ms = 500;
    c->timing_count = CAL_TIMING_PULSES;
    c->train_pause_ms = 2000;
    c->blank_after_pulse_ms = 5;

    c->dark_mv = 150;
    c->camera_enter_ms = 1000;
    c->camera_exit_ms = 300;
    c->display_exit_ms = 1500;

    c->arm_min_diff_mv = 80;
    c->arm_min_diff_pct = 25;
    c->arm_hold_ms = 100;
    c->lockout_ms = 200;
}

uint32_t cal_pulses_per_train(const cal_cfg_t *c)
{
    return CAL_MARKER_PULSES + c->timing_count;
}

uint32_t cal_pulse_offset(const cal_cfg_t *c, uint32_t idx)
{
    if (idx < CAL_MARKER_PULSES)
        return c->marker_ms[idx];
    return c->timing_first_ms + (idx - CAL_MARKER_PULSES) * c->timing_period_ms;
}

uint32_t cal_train_length(const cal_cfg_t *c)
{
    return cal_pulse_offset(c, cal_pulses_per_train(c) - 1) + c->train_pause_ms;
}

void cal_init(cal_state_t *s, const cal_cfg_t *cfg)
{
    memset(s, 0, sizeof *s);
    s->cfg = *cfg;
    s->mode = CAL_IDLE;
}

cal_mode_t cal_mode(const cal_state_t *s) { return s->mode; }

static void enter(cal_state_t *s, cal_mode_t m, uint32_t now)
{
    s->mode = m;
    s->dark_valid = false;
    s->lit_valid = false;
    if (m == CAL_CAMERA) {
        s->train_start = now;
        s->pulse_idx = 0;
    }
}

/* True while a camera pulse runs or shortly after it (sensor data is unreliable). */
static bool blanked(const cal_state_t *s, uint32_t now)
{
    if (s->pulse_active)
        return true;
    return s->blank_valid && since(now, s->blank_until) < 0;
}

void cal_on_sample(cal_state_t *s, uint32_t now, uint32_t a_mv, uint32_t b_mv)
{
    const cal_cfg_t *c = &s->cfg;
    if (blanked(s, now))
        return;

    uint32_t hi = a_mv > b_mv ? a_mv : b_mv;
    uint32_t diff = a_mv > b_mv ? a_mv - b_mv : b_mv - a_mv;
    bool dark = hi < c->dark_mv;

    /* Arm the edge check: the two sensors clearly see different light. */
    uint32_t need = hi * c->arm_min_diff_pct / 100;
    if (need < c->arm_min_diff_mv)
        need = c->arm_min_diff_mv;
    if (!dark && diff >= need) {
        s->armed_sign = a_mv > b_mv ? 1 : -1;
        s->armed_until = now + c->arm_hold_ms;
    }

    switch (s->mode) {
    case CAL_IDLE:
        if (!dark) {
            s->dark_valid = false;
        } else {
            if (!s->dark_valid) {
                s->dark_valid = true;
                s->dark_since = now;
            }
            if (since(now, s->dark_since) >= (int32_t)c->camera_enter_ms)
                enter(s, CAL_CAMERA, now);
        }
        break;
    case CAL_CAMERA:
        if (dark) {
            s->lit_valid = false;
        } else {
            if (!s->lit_valid) {
                s->lit_valid = true;
                s->lit_since = now;
            }
            if (since(now, s->lit_since) >= (int32_t)c->camera_exit_ms)
                enter(s, CAL_IDLE, now);
        }
        break;
    case CAL_DISPLAY:
        break;
    }
}

bool cal_on_edge(cal_state_t *s, uint32_t now, int out)
{
    const cal_cfg_t *c = &s->cfg;
    if (blanked(s, now))
        return false;
    if (s->clicked && since(now, s->last_click) < (int32_t)c->lockout_ms)
        return false;
    if (s->armed_sign == 0 || since(now, s->armed_until) > 0)
        return false;
    /* The comparator must have flipped away from the armed state. */
    int new_sign = out ? 1 : -1;
    if (new_sign == s->armed_sign)
        return false;

    s->armed_sign = 0;
    if (s->mode != CAL_DISPLAY)
        enter(s, CAL_DISPLAY, now);
    s->last_edge = now;
    s->clicked = true;
    s->last_click = now;
    return true;
}

cal_action_t cal_tick(cal_state_t *s, uint32_t now)
{
    const cal_cfg_t *c = &s->cfg;
    if (s->mode == CAL_DISPLAY) {
        if (since(now, s->last_edge) >= (int32_t)c->display_exit_ms)
            enter(s, CAL_IDLE, now);
        return CAL_NONE;
    }
    if (s->mode != CAL_CAMERA || s->pulse_active)
        return CAL_NONE;

    if (s->pulse_idx >= cal_pulses_per_train(c)) {
        /* Pause is over when the next train starts. */
        if (since(now, s->train_start) < (int32_t)cal_train_length(c))
            return CAL_NONE;
        s->train_start += cal_train_length(c);
        s->pulse_idx = 0;
    }
    if (since(now, s->train_start) < (int32_t)cal_pulse_offset(c, s->pulse_idx))
        return CAL_NONE;

    s->pulse_idx++;
    s->pulse_active = true;
    return CAL_PULSE;
}

void cal_pulse_done(cal_state_t *s, uint32_t now)
{
    s->pulse_active = false;
    s->blank_valid = true;
    s->blank_until = now + s->cfg.blank_after_pulse_ms;
}
