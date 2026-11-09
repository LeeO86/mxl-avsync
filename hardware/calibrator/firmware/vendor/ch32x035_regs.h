/* Minimal CH32X035 register map for the AV Sync calibrator.
 *
 * Extracted (structs, base addresses, bit values) from ch32fun's
 * ch32fun/ch32x03xhw.h, https://github.com/cnlohr/ch32fun, commit
 * b28d285460ce052a179dba8f4a22c6f73ff8e328 (2026-10-31). MIT licence, see
 * LICENSE-MIT-ch32fun.txt (copyright CNLohr et al. and Nanjing Qinheng
 * Microelectronics). Comparator bit layout of OPA->CTLR2 follows WCH's
 * OPA_CMP_Init() in openwch/ch32x035 EVT (ch32x035_opa.c V1.0.1); only the
 * bit positions were taken, no code.
 *
 * Only what the calibrator firmware uses is here.
 */
#ifndef CH32X035_REGS_H
#define CH32X035_REGS_H

#include <stdint.h>

#define __I  volatile const
#define __O  volatile
#define __IO volatile

typedef struct {
    __IO uint32_t CTLR, CFGR0, INTR, APB2PRSTR, APB1PRSTR, AHBPCENR, APB2PCENR, APB1PCENR;
    __IO uint32_t RESERVED1, RSTSCKR, AHBRSTR;
} RCC_TypeDef;

typedef struct {
    __IO uint32_t ACTLR, KEYR, OBKEYR, STATR, CTLR, ADDR, RESERVED, OBR, WPR, MODEKEYR, BOOT_MODEKEYR;
} FLASH_TypeDef;

typedef struct {
    __IO uint32_t CFGLR;  /* pins 0-7, 4 bits each */
    __IO uint32_t CFGHR;  /* pins 8-15 */
    __I  uint32_t INDR;
    __IO uint32_t OUTDR;
    __IO uint32_t BSHR;   /* low half sets, high half resets */
    __IO uint32_t BCR;
    __IO uint32_t LCKR;
    __IO uint32_t CFGXR;  /* pins 16-23 */
    __IO uint32_t BSXR;
} GPIO_TypeDef;

typedef struct {
    uint32_t RESERVED0;
    __IO uint32_t PCFR1, EXTICR1, EXTICR2;
    uint32_t RESERVED1, RESERVED2;
    __IO uint32_t CTLR;   /* USB pin options (UDP pull-up, USB IO enable) */
} AFIO_TypeDef;

typedef struct {
    __IO uint32_t INTENR, EVENR, RTENR, FTENR, SWIEVR, INTFR;
} EXTI_TypeDef;

typedef struct {
    __IO uint32_t STATR, CTLR1, CTLR2, SAMPTR1, SAMPTR2, IOFR1, IOFR2, IOFR3, IOFR4;
    __IO uint32_t WDHTR, WDLTR, RSQR1, RSQR2, RSQR3, ISQR, IDATAR1, IDATAR2, IDATAR3, IDATAR4;
    __IO uint32_t RDATAR, CTLR3, WDTR1, WDTR2, WDTR3;
} ADC_TypeDef;

typedef struct {
    __IO uint32_t CTLR, PSCR, RLDR, STATR;
} IWDG_TypeDef;

typedef struct {
    __IO uint16_t CFGR1, CFGR2;
    __IO uint32_t CTLR1, CTLR2, OPAKEY, CMPKEY, POLLKEY;
} OPA_TypeDef;

typedef struct {
    __IO uint32_t CTLR, SR, CNTL, CNTH, CMPL, CMPH;
} SysTick_Type;

typedef struct {
    __I  uint32_t ISR[8];
    __I  uint32_t IPR[8];
    __IO uint32_t ITHRESDR, RESERVED, CFGR;
    __I  uint32_t GISR;
    __IO uint8_t  VTFIDR[4];
    uint8_t RESERVED0[12];
    __IO uint32_t VTFADDR[4];
    uint8_t RESERVED1[0x90];
    __O  uint32_t IENR[8];
    uint8_t RESERVED2[0x60];
    __O  uint32_t IRER[8];
    uint8_t RESERVED3[0x60];
    __O  uint32_t IPSR[8];
    uint8_t RESERVED4[0x60];
    __O  uint32_t IPRR[8];
    uint8_t RESERVED5[0x60];
    __IO uint32_t IACTR[8];
    uint8_t RESERVED6[0xE0];
    __IO uint8_t  IPRIOR[256];
} PFIC_Type;

#define PERIPH_BASE      0x40000000u
#define APB1PERIPH_BASE  (PERIPH_BASE)
#define APB2PERIPH_BASE  (PERIPH_BASE + 0x10000u)
#define AHBPERIPH_BASE   (PERIPH_BASE + 0x20000u)
#define CORE_PERIPH_BASE 0xE0000000u

#define IWDG   ((IWDG_TypeDef *)(APB1PERIPH_BASE + 0x3000u))
#define AFIO   ((AFIO_TypeDef *)(APB2PERIPH_BASE + 0x0000u))
#define EXTI   ((EXTI_TypeDef *)(APB2PERIPH_BASE + 0x0400u))
#define GPIOA  ((GPIO_TypeDef *)(APB2PERIPH_BASE + 0x0800u))
#define GPIOB  ((GPIO_TypeDef *)(APB2PERIPH_BASE + 0x0C00u))
#define GPIOC  ((GPIO_TypeDef *)(APB2PERIPH_BASE + 0x1000u))
#define ADC1   ((ADC_TypeDef *)(APB2PERIPH_BASE + 0x2400u))
#define RCC    ((RCC_TypeDef *)(AHBPERIPH_BASE + 0x1000u))
#define FLASH  ((FLASH_TypeDef *)(AHBPERIPH_BASE + 0x2000u))
#define OPA    ((OPA_TypeDef *)(AHBPERIPH_BASE + 0x6000u))
#define PFIC   ((PFIC_Type *)(CORE_PERIPH_BASE + 0xE000u))
#define SysTick ((SysTick_Type *)(CORE_PERIPH_BASE + 0xF000u))

/* RCC */
#define RCC_HPRE                0x000000F0u  /* AHB prescaler field (reset: /6) */
#define RCC_APB2Periph_AFIO     0x00000001u
#define RCC_APB2Periph_GPIOA    0x00000004u
#define RCC_APB2Periph_GPIOB    0x00000008u
#define RCC_APB2Periph_GPIOC    0x00000010u
#define RCC_APB2Periph_ADC1     0x00000200u
#define RCC_ADC1RST             0x00000200u

/* FLASH */
#define FLASH_ACTLR_LATENCY     0x00000003u
#define FLASH_ACTLR_LATENCY_2   0x00000002u  /* 2 wait states, needed at 48 MHz */

/* GPIO 4-bit pin modes (CFGLR/CFGHR/CFGXR) */
#define GPIO_MODE_ANALOG        0x0u
#define GPIO_MODE_IN_FLOAT      0x4u
#define GPIO_MODE_OUT_PP_50M    0x3u
#define GPIO_MODE_AF_PP_50M     0xBu

/* AFIO->CTLR USB pin options: must stay 0 (no pull-up on D+, USB pins off) */
#define AFIO_CTLR_UDP_PUE       0x0000000Cu
#define AFIO_CTLR_UDM_PUE       0x00000003u
#define AFIO_CTLR_USB_PHY_V33   0x00000040u
#define AFIO_CTLR_USB_IOEN      0x00000080u

/* ADC */
#define ADC_EOC                 0x00000002u
#define ADC_ADON                0x00000001u
#define ADC_EXTSEL              0x000E0000u  /* 111 = SWSTART */
#define ADC_EXTTRIG             0x00100000u
#define ADC_SWSTART             0x00400000u
#define ADC_CTLR3_CLK_DIV       0x0000000Fu
#define ADC_CTLR3_CLK_DIV_3     0x00000008u

/* OPA->CTLR2 comparator fields, CMPn (n = 0, 1, 2) at bit 5n:
 * +0 EN, +1 MODE (0 = output to TIM2, 1 = output to the CnO pin),
 * +2 NSEL (0 = N0, 1 = N1), +3 PSEL (0 = P0, 1 = P1), +4 HYEN (hysteresis). */
#define CMP_FIELD(n, bit)       (1u << (5u * (n) + (bit)))
#define CMP_EN                  0u
#define CMP_MODE_IO             1u
#define CMP_NSEL                2u
#define CMP_PSEL                3u
#define CMP_HYEN                4u
#define OPA_KEY1                0x45670123u
#define OPA_KEY2                0xCDEF89ABu

/* Interrupt numbers used here (PFIC) */
#define EXTI7_0_IRQn            20u

#endif
