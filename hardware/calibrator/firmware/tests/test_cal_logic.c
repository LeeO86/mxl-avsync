/* Host unit tests for cal_logic.c. Run with: make test */
#include "../src/cal_logic.h"

#include <stdio.h>
#include <stdlib.h>

static int checks, failures;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

static cal_cfg_t cfg;
static cal_state_t st;

static void reset(void)
{
    cal_default_cfg(&cfg);
    cal_init(&st, &cfg);
}

/* Feed one sample per ms from t0 to t1 (inclusive), with ticks. Any due pulse
 * is finished at once. Returns the number of pulses. */
static int run(uint32_t t0, uint32_t t1, uint32_t a, uint32_t b)
{
    int pulses = 0;
    for (uint32_t t = t0; t <= t1; t++) {
        cal_on_sample(&st, t, a, b);
        if (cal_tick(&st, t) == CAL_PULSE) {
            pulses++;
            cal_pulse_done(&st, t);
        }
    }
    return pulses;
}

/* Bring the logic into DISPLAY: armed with A brighter, then edge to B brighter. */
static uint32_t go_display(uint32_t t)
{
    cal_on_sample(&st, t, 3000, 100);
    CHECK(cal_on_edge(&st, t + 1, 0));
    CHECK(cal_mode(&st) == CAL_DISPLAY);
    return t + 1;
}

static void test_power_up_idle(void)
{
    reset();
    CHECK(cal_mode(&st) == CAL_IDLE);
    CHECK(cal_tick(&st, 0) == CAL_NONE);
    CHECK(run(0, 5000, 2000, 2000) == 0); /* lit, no edges: stays silent */
    CHECK(cal_mode(&st) == CAL_IDLE);
}

static void test_camera_entry(void)
{
    reset();
    run(0, 998, 10, 10);
    cal_on_sample(&st, 999, 10, 10);
    CHECK(cal_mode(&st) == CAL_IDLE);   /* dark for 999 ms */
    cal_on_sample(&st, 1000, 10, 10);
    CHECK(cal_mode(&st) == CAL_CAMERA); /* dark for 1000 ms */

    /* a lit sample in between restarts the dark run */
    reset();
    run(0, 500, 10, 10);
    run(501, 501, 2000, 2000);
    run(502, 1499, 10, 10);
    CHECK(cal_mode(&st) == CAL_IDLE);
    run(1500, 1502, 10, 10);
    CHECK(cal_mode(&st) == CAL_CAMERA);
}

static void test_camera_schedule(void)
{
    reset();
    run(0, 999, 10, 10);
    cal_on_sample(&st, 1000, 10, 10);
    CHECK(cal_mode(&st) == CAL_CAMERA);
    const uint32_t start = 1000;

    uint32_t times[64];
    int n = 0;
    for (uint32_t t = start; t < start + 2 * 12500 && n < 64; t++) {
        cal_on_sample(&st, t, 10, 10);
        if (cal_tick(&st, t) == CAL_PULSE) {
            times[n++] = t - start;
            cal_pulse_done(&st, t);
        }
    }
    CHECK(n == 46);
    CHECK(times[0] == 0 && times[1] == 125 && times[2] == 250);
    for (int k = 0; k < 20; k++)
        CHECK(times[3 + k] == 1000u + 500u * (uint32_t)k);
    CHECK(times[22] == 10500);
    CHECK(times[23] == 12500); /* 2000 ms pause, then the marker again */
    CHECK(times[24] == 12625 && times[25] == 12750);
    CHECK(cal_train_length(&cfg) == 12500);
}

static void test_blanking(void)
{
    reset();
    run(0, 999, 10, 10);
    cal_on_sample(&st, 1000, 10, 10);
    CHECK(cal_tick(&st, 1000) == CAL_PULSE);
    /* during the pulse: samples and edges are ignored */
    cal_on_sample(&st, 1000, 3000, 100);
    CHECK(!cal_on_edge(&st, 1001, 0));
    CHECK(cal_tick(&st, 1001) == CAL_NONE); /* no second pulse while active */
    cal_pulse_done(&st, 1002);
    /* until pulse end + 5 ms */
    cal_on_sample(&st, 1006, 3000, 100);
    CHECK(!cal_on_edge(&st, 1006, 0));
    CHECK(cal_mode(&st) == CAL_CAMERA);
    /* after the blanking: arming works again */
    cal_on_sample(&st, 1007, 3000, 100);
    CHECK(cal_on_edge(&st, 1008, 0));
    CHECK(cal_mode(&st) == CAL_DISPLAY);
}

static void test_valid_edge(void)
{
    reset();
    run(0, 100, 3000, 100);       /* lit, A brighter: armed, still idle */
    CHECK(cal_mode(&st) == CAL_IDLE);
    CHECK(cal_on_edge(&st, 101, 0)); /* flips to B brighter: click */
    CHECK(cal_mode(&st) == CAL_DISPLAY);

    /* B brighter armed, edge to A brighter */
    reset();
    run(0, 10, 100, 3000);
    CHECK(cal_on_edge(&st, 11, 1));
}

static void test_chatter_rejected(void)
{
    reset();
    /* both sensors see the same light: never armed */
    run(0, 100, 2000, 1950);
    CHECK(!cal_on_edge(&st, 101, 1));
    CHECK(!cal_on_edge(&st, 102, 0));
    /* dark: never armed, even with a large relative difference */
    run(103, 200, 100, 0);
    CHECK(!cal_on_edge(&st, 201, 0));
    /* small absolute difference below 80 mV */
    run(202, 300, 300, 230);
    CHECK(!cal_on_edge(&st, 301, 0));
    CHECK(cal_mode(&st) == CAL_IDLE);
}

static void test_same_sign_rejected(void)
{
    reset();
    run(0, 10, 3000, 100);         /* A brighter */
    CHECK(!cal_on_edge(&st, 11, 1)); /* says A brighter again: not a swap */
    CHECK(cal_on_edge(&st, 12, 0));
}

static void test_lockout(void)
{
    reset();
    uint32_t t = go_display(0);    /* click at t = 1 */
    cal_on_sample(&st, t + 150, 100, 3000);
    CHECK(!cal_on_edge(&st, t + 199, 1)); /* 199 ms: locked */
    cal_on_sample(&st, t + 199, 100, 3000);
    CHECK(cal_on_edge(&st, t + 200, 1));  /* 200 ms, re-armed: click */

    /* edge after lockout without re-arming is rejected */
    reset();
    t = go_display(0);
    CHECK(!cal_on_edge(&st, t + 300, 1));
}

static void test_arm_expiry(void)
{
    reset();
    cal_on_sample(&st, 0, 3000, 100);
    CHECK(cal_on_edge(&st, 100, 0));  /* exactly 100 ms: still armed */
    reset();
    cal_on_sample(&st, 0, 3000, 100);
    CHECK(!cal_on_edge(&st, 101, 0)); /* 101 ms: expired */
}

static void test_display_exit(void)
{
    reset();
    uint32_t t = go_display(0);    /* last valid edge at 1 */
    CHECK(run(t + 1, t + 1499, 3000, 3000) == 0);
    CHECK(cal_mode(&st) == CAL_DISPLAY);
    cal_tick(&st, t + 1500);
    CHECK(cal_mode(&st) == CAL_IDLE);
}

static void test_camera_exit(void)
{
    reset();
    run(0, 1000, 10, 10);
    CHECK(cal_mode(&st) == CAL_CAMERA);
    /* The pulse at 1000 blanks samples until 1005, so the lit run starts at
     * 1005. Lit (no difference, so no arming) for 299 ms: stays. */
    run(1001, 1303, 2000, 2000);
    CHECK(cal_mode(&st) == CAL_CAMERA);
    run(1304, 1304, 2000, 2000);
    CHECK(cal_mode(&st) == CAL_CAMERA);
    run(1305, 1305, 2000, 2000);
    CHECK(cal_mode(&st) == CAL_IDLE);
}

static void test_edge_in_camera(void)
{
    reset();
    run(0, 1000, 10, 10);
    CHECK(cal_mode(&st) == CAL_CAMERA);
    run(1001, 1100, 10, 10);       /* after the marker pulse blanking */
    cal_on_sample(&st, 1101, 3000, 100);
    CHECK(cal_on_edge(&st, 1102, 0));
    CHECK(cal_mode(&st) == CAL_DISPLAY);
    /* no more camera pulses, even at the next scheduled time */
    for (uint32_t t = 1103; t < 2500; t++)
        CHECK(cal_tick(&st, t) == CAL_NONE);
}

static void test_wraparound(void)
{
    reset();
    uint32_t t0 = 0xFFFFFF00u;     /* counter wraps during the dark run */
    for (uint32_t i = 0; i < 1000; i++)
        cal_on_sample(&st, t0 + i, 10, 10);
    CHECK(cal_mode(&st) == CAL_IDLE);
    cal_on_sample(&st, t0 + 1000, 10, 10);
    CHECK(cal_mode(&st) == CAL_CAMERA);
}

int main(void)
{
    test_power_up_idle();
    test_camera_entry();
    test_camera_schedule();
    test_blanking();
    test_valid_edge();
    test_chatter_rejected();
    test_same_sign_rejected();
    test_lockout();
    test_arm_expiry();
    test_display_exit();
    test_camera_exit();
    test_edge_in_camera();
    test_wraparound();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
