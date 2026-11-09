/* Startup for the CH32X035 (QingKe V4C RISC-V core), written for this project.
 *
 * Layout follows WCH's EVT startup and ch32fun (MIT): address 0 holds a jump
 * to the reset code, then a table of handler addresses indexed by interrupt
 * number. mtvec points to the table with mode 3 (vectored, absolute
 * addresses). Handlers use GCC's standard `interrupt` attribute, so the
 * hardware register stack (HPE) and nesting are switched off (CSR 0x804 = 0).
 */
#include <stdint.h>

#include "ch32x035_regs.h"

extern uint32_t _data_lma, _data_vma, _edata, _sbss, _ebss;
int main(void);

void Default_Handler(void) __attribute__((interrupt));
void EXTI7_0_IRQHandler(void) __attribute__((interrupt, weak, alias("Default_Handler")));

/* Any unexpected interrupt: stop here, the watchdog then resets the chip with
 * all pins back to inputs (LED and buzzer off). */
void Default_Handler(void)
{
    for (;;) {
    }
}

void handle_reset(void) __attribute__((naked, noreturn));
void c_reset(void) __attribute__((noreturn, used));

/* Reset jump (offset 0) followed by the vector table. Entry n = handler of
 * interrupt n. Entry 0 is the reset entry itself, like in WCH's table. */
__attribute__((naked, section(".init"))) void _start(void)
{
    __asm__ volatile(
        ".option push\n"
        ".option norvc\n"
        "j handle_reset\n"
        ".option pop\n"
        ".balign 4\n"
        ".global _vector_base\n"
        "_vector_base:\n"
        ".word _start\n"              /* 0 */
        ".word 0\n"                   /* 1 */
        ".word Default_Handler\n"     /* 2 NMI */
        ".word Default_Handler\n"     /* 3 HardFault / exception */
        ".rept 16\n"                  /* 4-19 */
        ".word Default_Handler\n"
        ".endr\n"
        ".word EXTI7_0_IRQHandler\n"  /* 20 EXTI lines 0-7 (comparator output on PB3) */
        ".rept 28\n"                  /* 21-48 */
        ".word Default_Handler\n"
        ".endr\n");
}

void handle_reset(void)
{
    __asm__ volatile(
        ".option push\n"
        ".option norelax\n"
        "la gp, __global_pointer$\n"
        ".option pop\n"
        "la sp, _eusrstack\n"
        "li t0, 0x1f\n"
        "csrw 0xbc0, t0\n"            /* pipeline + branch prediction on (as WCH EVT) */
        "csrw 0x804, zero\n"          /* INTSYSCR: no hardware stack, no nesting */
        "li t0, 0x1880\n"
        "csrw mstatus, t0\n"          /* stay in machine mode, MPIE set */
        "la t0, _vector_base\n"
        "ori t0, t0, 3\n"
        "csrw mtvec, t0\n"
        "j c_reset\n");
}

/* Normal C (stack is set up now): copy .data, clear .bss, enter main. */
void c_reset(void)
{
    uint32_t *src = &_data_lma;
    for (uint32_t *dst = &_data_vma; dst < &_edata;)
        *dst++ = *src++;
    for (uint32_t *dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;

    /* mret into main: interrupts become enabled (MIE = MPIE). */
    __asm__ volatile("csrw mepc, %0\n mret\n" ::"r"(main));
    for (;;) {
    }
}

/* We link without a C library; GCC may still call these for struct copies. */
void *memset(void *d, int c, unsigned int n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned int n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
