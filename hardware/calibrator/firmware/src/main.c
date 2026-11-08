/* AV Sync calibrator firmware for PY32F002AF15P6TU (TSSOP-20).
 *
 * Pins (see hardware/calibrator schematic):
 *   PA5  (pin 7)  LED     high = white LED on
 *   PA6  (pin 6)  BUZ     high = NPN on = buzzer coil current
 *   PA3  (pin 14) SENS_A  COMP2 INP, ADC_IN3
 *   PA2  (pin 13) SENS_B  COMP2 INM, ADC_IN2
 *   PA13/PA14            SWD
 *   pin 5 (PB6/BOOT0)    tied to GND on the board: never drive PB6
 *
 * All decisions (modes, timing, lock-out) are in cal_logic.c. This file only
 * talks to the hardware. NOT TESTED ON HARDWARE YET.
 */
#include "py32f002ax5.h"
#include "cal_logic.h"

#define LED_PIN (1u << 5)
#define BUZ_PIN (1u << 6)

/* The ADC is ratiometric to VCC. VCC is USB VBUS minus a 10 ohm drop, so we
 * assume 5000 mV. The thresholds are rough, so this is good enough. */
#define VCC_MV 5000u

static volatile uint32_t ms_now;
static cal_state_t cal;

void SysTick_Handler(void) { ms_now++; }

/* ---- clock: HSI 24 MHz ------------------------------------------------- */
/* Same procedure as SystemInit() in Puya's system_py32f0xx.c and
 * LL_RCC_HSICALIBRATION_24MHz in py32f0xx_ll_rcc.h (py32f0-template):
 * HSI_FS = 4 (24 MHz) plus the factory trim word stored at 0x1FFF0F10.
 * 24 MHz needs 0 flash wait states (LL_SetFlashLatency switches to 1 WS only
 * above 24 MHz). We never erase or program flash, so the FLASH TSx timing
 * registers are left alone. */
static void clock_init(void)
{
    uint32_t trim = *(const volatile uint32_t *)0x1FFF0F10u & 0x1FFFu;
    RCC->ICSCR = (RCC->ICSCR & 0xFFFF0000u) | (4u << RCC_ICSCR_HSI_FS_Pos) | trim;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {
    }
    RCC->CR &= ~RCC_CR_HSIDIV;                                /* HSI / 1 */
    RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE); /* SYSCLK = HSI, /1, /1 */
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    SCB->VTOR = FLASH_BASE;
}

/* ---- watchdog: about 250 ms -------------------------------------------- */
static void iwdg_init(void)
{
    IWDG->KR = 0xCCCCu;   /* start (also starts the LSI, ~32.768 kHz) */
    IWDG->KR = 0x5555u;   /* unlock PR and RLR */
    IWDG->PR = 3u;        /* /32 -> ~1 kHz */
    IWDG->RLR = 255u;     /* ~250 ms */
    while (IWDG->SR) {
    }
    IWDG->KR = 0xAAAAu;
}

static void iwdg_feed(void) { IWDG->KR = 0xAAAAu; }

/* ---- 1 MHz free-running timer for microsecond timing -------------------- */
static void us_timer_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_TIM1EN;
    TIM1->PSC = 23u;           /* 24 MHz / 24 = 1 MHz */
    TIM1->ARR = 0xFFFFu;
    TIM1->EGR = TIM_EGR_UG;    /* load PSC */
    TIM1->CR1 = TIM_CR1_CEN;
}

static inline uint16_t us_now(void) { return (uint16_t)TIM1->CNT; }

/* ---- GPIO -------------------------------------------------------------- */
static void gpio_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOFEN;
    GPIOA->BRR = LED_PIN | BUZ_PIN;
    /* Port A: everything analog (11) except PA13/PA14 (keep SWD) and
     * PA5/PA6 (push-pull outputs, 01). */
    uint32_t keep = (3u << (13 * 2)) | (3u << (14 * 2));
    uint32_t moder = (GPIOA->MODER & keep) | (0xFFFFFFFFu & ~keep);
    moder &= ~((3u << (5 * 2)) | (3u << (6 * 2)));
    moder |= (1u << (5 * 2)) | (1u << (6 * 2));
    GPIOA->MODER = moder;
    /* Unused ports: analog. PB6 shares pin 5 with BOOT0 (tied to GND): analog
     * mode means we never drive it. */
    GPIOB->MODER = 0xFFFFFFFFu;
    GPIOF->MODER = 0xFFFFFFFFu;
}

/* ---- pulse and click ----------------------------------------------------- */
/* LED (optional) and buzzer start with ONE store to BSRR, so they begin less
 * than 1 us apart. Then the buzzer toggles every half period for `cycles`
 * cycles and ends low. The LED goes off after led_us. Busy-waits; the whole
 * thing is a few ms at most. */
static void burst(int use_led, uint32_t led_us, uint32_t cycles, uint32_t half_us)
{
    uint16_t t0 = us_now();
    GPIOA->BSRR = use_led ? (LED_PIN | BUZ_PIN) : BUZ_PIN;

    uint32_t edges = cycles ? 2u * cycles - 1u : 0u; /* last edge turns BUZ off */
    uint32_t i = 1;
    int led_on = use_led;
    while (i <= edges || led_on) {
        uint32_t el = (uint16_t)(us_now() - t0);
        if (i <= edges && el >= i * half_us) {
            GPIOA->BSRR = (i & 1u) ? (BUZ_PIN << 16) : BUZ_PIN;
            i++;
        }
        if (led_on && el >= led_us) {
            GPIOA->BSRR = LED_PIN << 16;
            led_on = 0;
        }
    }
    GPIOA->BSRR = BUZ_PIN << 16; /* safety: buzzer always ends low */
}

/* ---- comparator: COMP2, A (PA3) vs B (PA2) ------------------------------ */
static void comp_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_COMP2EN;
    COMP2->FR = 0;                      /* no digital filter: no added delay */
    COMP2->CSR = COMP_CSR_INPSEL_1      /* INP = IO3 = PA3 */
               | COMP_CSR_INMSEL_3      /* INM = IO3 = PA2 */
               | COMP_CSR_HYST          /* ~20 mV hysteresis */
               /* PWRMODE = 00: high speed (~40-85 ns) */
               | COMP_CSR_EN;
    EXTI->RTSR |= EXTI_RTSR_RT18;       /* COMP2 output is EXTI line 18 */
    EXTI->FTSR |= EXTI_FTSR_FT18;
    EXTI->PR = EXTI_PR_PR18;
    EXTI->IMR |= EXTI_IMR_IM18;
    NVIC_SetPriority(ADC_COMP_IRQn, 1);
    NVIC_EnableIRQ(ADC_COMP_IRQn);
}

void ADC_COMP_IRQHandler(void)
{
    int out = (COMP2->CSR & COMP_CSR_COMP_OUT) ? 1 : 0; /* 1 = A brighter */
    EXTI->PR = EXTI_PR_PR18;
    if (cal_on_edge(&cal, ms_now, out))
        burst(0, 0, cal.cfg.click_cycles, cal.cfg.click_half_period_us); /* click now */
}

/* ---- ADC: one software-triggered conversion per channel ----------------- */
static void adc_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_ADCEN;
    ADC1->CFGR2 = ADC_CFGR2_CKMODE_1;           /* PCLK / 4 = 6 MHz */
    ADC1->SMPR = ADC_SMPR_SMP_2 | ADC_SMPR_SMP_0; /* 41.5 cycles */
    ADC1->CR |= ADC_CR_ADCAL;                   /* calibrate while disabled */
    while (ADC1->CR & ADC_CR_ADCAL) {
    }
    ADC1->CR |= ADC_CR_ADEN;
    for (volatile int i = 0; i < 100; i++) {
    }
}

static uint32_t adc_read_mv(uint32_t chsel)
{
    ADC1->CHSELR = chsel;
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOSEQ;
    ADC1->CR |= ADC_CR_ADSTART;
    while (!(ADC1->ISR & ADC_ISR_EOC)) {
    }
    return (ADC1->DR & 0xFFFu) * VCC_MV / 4095u;
}

int main(void)
{
    clock_init();
    gpio_init();
    iwdg_init();
    us_timer_init();

    cal_cfg_t cfg;
    cal_default_cfg(&cfg);
    cal_init(&cal, &cfg);

    SysTick_Config(24000000u / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 0); /* tiny ISR; keeps the ms clock exact */
    adc_init();
    comp_init();

    uint32_t last = ms_now;
    for (;;) {
        iwdg_feed();
        while (ms_now == last) {
        }
        last = ms_now;

        uint32_t a = adc_read_mv(ADC_CHSELR_CHSEL3);
        uint32_t b = adc_read_mv(ADC_CHSELR_CHSEL2);

        __disable_irq();
        cal_on_sample(&cal, last, a, b);
        cal_action_t act = cal_tick(&cal, last);
        __enable_irq();

        if (act == CAL_PULSE) {
            burst(1, cal.cfg.led_pulse_us, cal.cfg.click_cycles, cal.cfg.click_half_period_us);
            __disable_irq();
            cal_pulse_done(&cal, ms_now);
            __enable_irq();
        }
    }
}
