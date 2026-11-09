/* AV Sync calibrator firmware for the WCH CH32X035G8R6 (QSOP-28).
 *
 * Pins (see hardware/calibrator schematic):
 *   PA4  (pin 15) LED     high = white LED on
 *   PA5  (pin 16) BUZ     high = NPN on = buzzer coil current
 *   PA13 (pin 5)  SENS_A  comparator 3 positive input (C3P0)
 *   PA0  (pin 9)  SENS_A  same net, ADC channel 0
 *   PA2  (pin 11) SENS_B  comparator 3 negative input (C3N0), ADC channel 2
 *   PB3  (pin 18) CMP3 output (C3O). Driven by the comparator, read back
 *                 for the edge interrupt. Must stay unconnected on the board.
 *   PC16/PC17 (pins 2/3) USB D-/D+ to the USB-C receptacle. Button PC17 -> VDD
 *                 enters the WCH ROM bootloader at power-on (update mode).
 *   PC18/PC19     SDI debug (SWDIO/SWCLK), left at reset default.
 *
 * USB IS NEVER ENABLED BY THIS FIRMWARE. The USB and USB-PD blocks get no
 * clock, AFIO->CTLR keeps the D+/D- pull-ups off, and PC16/PC17 (and PC10/PC11,
 * bonded to them inside the package) stay floating inputs. With no pull-up on
 * D+ or D-, a phone never sees a USB device: it only supplies 5 V (asked for
 * by the 5.1 kohm resistors on CC1/CC2). Only the ROM bootloader, entered with
 * the button held at power-on, talks USB.
 *
 * Package notes (datasheet V2.2, QSOP-28): PA13 is bonded to PC15 and PA12 to
 * PC14, PB1 to PB5. Those twins are never outputs here.
 *
 * All decisions (modes, timing, lock-out) are in cal_logic.c. This file only
 * talks to the hardware. NOT TESTED ON HARDWARE YET.
 */
#include "ch32x035_regs.h"
#include "cal_logic.h"

#define LED_PIN (1u << 4) /* PA4 */
#define BUZ_PIN (1u << 5) /* PA5 */
#define CMP_OUT_PIN 3u    /* PB3 */

#define HCLK_HZ 48000000u
#define TICKS_PER_US (HCLK_HZ / 1000000u)
#define TICKS_PER_MS (HCLK_HZ / 1000u)

/* The ADC is ratiometric to VDD. VDD is USB VBUS minus a 10 ohm drop, so we
 * assume 5000 mV. The thresholds are rough, so this is good enough. */
#define VDD_MV 5000u

static volatile uint32_t ms_now;
static cal_state_t cal;

static inline void irq_off(void) { __asm__ volatile("csrc mstatus, 8" ::: "memory"); }
static inline void irq_on(void) { __asm__ volatile("csrs mstatus, 8" ::: "memory"); }

/* ---- clock: HSI 48 MHz, no crystal --------------------------------------- */
/* After reset the core runs from HSI 48 MHz divided by 6. Set 2 flash wait
 * states first (WCH TRM / ch32fun SystemInit for CH32X03x), then HCLK = 48 MHz. */
static void clock_init(void)
{
    FLASH->ACTLR = (FLASH->ACTLR & ~FLASH_ACTLR_LATENCY) | FLASH_ACTLR_LATENCY_2;
    RCC->CFGR0 &= ~RCC_HPRE; /* AHB prescaler /1 */
}

/* ---- time: SysTick runs free at HCLK (64-bit counter, low word used) ----- */
static void time_init(void)
{
    SysTick->CTLR = 0;
    SysTick->CNTL = 0;
    SysTick->CNTH = 0;
    SysTick->CTLR = 5u; /* STE (enable) + STCLK (HCLK), no interrupt */
}

static inline uint32_t ticks(void) { return SysTick->CNTL; }

/* Called from the main loop: advances ms_now from the free-running counter. */
static void time_update(void)
{
    static uint32_t last;
    while ((uint32_t)(ticks() - last) >= TICKS_PER_MS) {
        last += TICKS_PER_MS;
        ms_now++;
    }
}

/* ---- watchdog: about 250 ms ---------------------------------------------- */
/* IWDG clock = HSI / 1024 = ~46.9 kHz (datasheet clock tree). /32 -> ~1.46 kHz,
 * reload 366 -> ~250 ms. */
static void iwdg_init(void)
{
    IWDG->CTLR = 0x5555u; /* unlock PSCR/RLDR */
    IWDG->PSCR = 3u;      /* /32 */
    IWDG->RLDR = 366u;
    IWDG->CTLR = 0xAAAAu; /* reload */
    IWDG->CTLR = 0xCCCCu; /* start */
}

static void iwdg_feed(void) { IWDG->CTLR = 0xAAAAu; }

/* ---- GPIO ---------------------------------------------------------------- */
static void set_mode(GPIO_TypeDef *g, uint32_t pin, uint32_t mode)
{
    volatile uint32_t *reg = pin < 8 ? &g->CFGLR : pin < 16 ? &g->CFGHR : &g->CFGXR;
    uint32_t sh = (pin & 7u) * 4u;
    *reg = (*reg & ~(0xFu << sh)) | (mode << sh);
}

static void gpio_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                      RCC_APB2Periph_GPIOC;

    /* USB stays off: no D+/D- pull-up, USB pin function off (reset values,
     * written again on purpose). */
    AFIO->CTLR &= ~(AFIO_CTLR_UDP_PUE | AFIO_CTLR_UDM_PUE | AFIO_CTLR_USB_IOEN | AFIO_CTLR_USB_PHY_V33);

    /* Port A and B: everything analog (off) first. PC16/PC17 (USB/boot),
     * PC18/PC19 (debug) and PC3 (reset pin option) keep their reset modes. */
    GPIOA->BCR = LED_PIN | BUZ_PIN;
    GPIOA->CFGLR = 0;
    GPIOA->CFGHR = 0;
    GPIOA->CFGXR = 0;
    GPIOB->CFGLR = 0;
    GPIOB->CFGHR = 0;
    GPIOB->CFGXR = 0;
    set_mode(GPIOC, 14, GPIO_MODE_ANALOG); /* PC14 = PA12 twin, unused */
    set_mode(GPIOC, 15, GPIO_MODE_ANALOG); /* PC15 = PA13 twin (SENS_A) */

    set_mode(GPIOA, 4, GPIO_MODE_OUT_PP_50M); /* LED */
    set_mode(GPIOA, 5, GPIO_MODE_OUT_PP_50M); /* BUZ */
    set_mode(GPIOB, CMP_OUT_PIN, GPIO_MODE_AF_PP_50M); /* CMP3 output */
    /* PA0, PA2, PA13 stay analog (sensor inputs). */
}

/* ---- pulse and click ----------------------------------------------------- */
/* LED (optional) and buzzer start with ONE store to BSHR, so they begin less
 * than 1 us apart. Then the buzzer toggles every half period for `cycles`
 * cycles and ends low. The LED goes off after led_us. Busy-waits; the whole
 * thing is a few ms at most. */
static void burst(int use_led, uint32_t led_us, uint32_t cycles, uint32_t half_us)
{
    uint32_t t0 = ticks();
    GPIOA->BSHR = use_led ? (LED_PIN | BUZ_PIN) : BUZ_PIN;

    uint32_t edges = cycles ? 2u * cycles - 1u : 0u; /* last edge turns BUZ off */
    uint32_t i = 1;
    int led_on = use_led;
    while (i <= edges || led_on) {
        uint32_t el = (ticks() - t0) / TICKS_PER_US;
        if (i <= edges && el >= i * half_us) {
            GPIOA->BSHR = (i & 1u) ? (BUZ_PIN << 16) : BUZ_PIN;
            i++;
        }
        if (led_on && el >= led_us) {
            GPIOA->BSHR = LED_PIN << 16;
            led_on = 0;
        }
    }
    GPIOA->BSHR = BUZ_PIN << 16; /* safety: buzzer always ends low */
}

/* ---- comparator 3: A (PA13, P0) vs B (PA2, N0), hysteresis on ------------ */
/* The comparator output goes to its pin C3O = PB3 (MODE = IO). EXTI line 3
 * watches PB3 on both edges; the interrupt reads the pin to know which sensor
 * is now brighter. Comparator -> pin -> EXTI adds well under 1 us. */
static void cmp_init(void)
{
    OPA->CMPKEY = OPA_KEY1; /* unlock the comparator control */
    OPA->CMPKEY = OPA_KEY2;
    OPA->CTLR2 = CMP_FIELD(2, CMP_EN) | CMP_FIELD(2, CMP_MODE_IO) | CMP_FIELD(2, CMP_HYEN);
    /* NSEL = 0 (N0 = PA2), PSEL = 0 (P0 = PA13) */

    AFIO->EXTICR1 = (AFIO->EXTICR1 & ~(3u << (CMP_OUT_PIN * 2))) | (1u << (CMP_OUT_PIN * 2)); /* port B */
    EXTI->RTENR |= 1u << CMP_OUT_PIN;
    EXTI->FTENR |= 1u << CMP_OUT_PIN;
    EXTI->INTFR = 1u << CMP_OUT_PIN;
    EXTI->INTENR |= 1u << CMP_OUT_PIN;
    PFIC->IENR[EXTI7_0_IRQn / 32u] = 1u << (EXTI7_0_IRQn % 32u);
}

void EXTI7_0_IRQHandler(void) __attribute__((interrupt));
void EXTI7_0_IRQHandler(void)
{
    int out = (GPIOB->INDR >> CMP_OUT_PIN) & 1u; /* 1 = A brighter */
    EXTI->INTFR = 1u << CMP_OUT_PIN;
    if (cal_on_edge(&cal, ms_now, out))
        burst(0, 0, cal.cfg.click_cycles, cal.cfg.click_half_period_us); /* click now */
}

/* ---- ADC: one software-triggered conversion per channel ------------------ */
static void adc_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
    RCC->APB2PRSTR |= RCC_ADC1RST;
    RCC->APB2PRSTR &= ~RCC_ADC1RST;
    ADC1->CTLR3 = (ADC1->CTLR3 & ~ADC_CTLR3_CLK_DIV) | ADC_CTLR3_CLK_DIV_3; /* as ch32fun example */
    ADC1->SAMPTR2 = 0x3FFFFFFFu; /* longest sample time on channels 0-9 (high-impedance nodes) */
    ADC1->RSQR1 = 0;             /* one conversion per sequence */
    ADC1->CTLR2 = ADC_ADON | ADC_EXTSEL | ADC_EXTTRIG;
}

static uint32_t adc_read_mv(uint32_t ch)
{
    ADC1->RSQR3 = ch;
    ADC1->CTLR2 |= ADC_SWSTART;
    while (!(ADC1->STATR & ADC_EOC)) {
    }
    return (ADC1->RDATAR & 0xFFFu) * VDD_MV / 4095u;
}

int main(void)
{
    clock_init();
    gpio_init();
    iwdg_init();
    time_init();

    cal_cfg_t cfg;
    cal_default_cfg(&cfg);
    cal_init(&cal, &cfg);

    adc_init();
    cmp_init();

    uint32_t last = ms_now;
    for (;;) {
        iwdg_feed();
        do {
            time_update();
        } while (ms_now == last);
        last = ms_now;

        uint32_t a = adc_read_mv(0); /* PA0 = SENS_A */
        uint32_t b = adc_read_mv(2); /* PA2 = SENS_B */

        irq_off();
        cal_on_sample(&cal, last, a, b);
        cal_action_t act = cal_tick(&cal, last);
        irq_on();

        if (act == CAL_PULSE) {
            burst(1, cal.cfg.led_pulse_us, cal.cfg.click_cycles, cal.cfg.click_half_period_us);
            time_update();
            irq_off();
            cal_pulse_done(&cal, ms_now);
            irq_on();
        }
    }
}
