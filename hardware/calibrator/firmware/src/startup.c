/* Minimal startup for PY32F002A (Cortex-M0+). Written for this project:
 * vector table, copy .data, clear .bss, call main(). */
#include <stdint.h>

extern uint32_t _estack, _etext, _sdata, _edata, _sbss, _ebss;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
void ADC_COMP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

void Reset_Handler(void)
{
    uint32_t *src = &_etext;
    for (uint32_t *dst = &_sdata; dst < &_edata;)
        *dst++ = *src++;
    for (uint32_t *dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;
    main();
    for (;;) {
    }
}

/* Unexpected interrupt or fault: stop here. The watchdog then resets the
 * chip, and at reset all pins are inputs (LED and buzzer off). */
void Default_Handler(void)
{
    for (;;) {
    }
}

/* 16 core vectors + 32 device vectors (see IRQn_Type in py32f002ax5.h).
 * Every unused slot goes to Default_Handler. */
__attribute__((section(".isr_vector"), used))
void (*const vectors[48])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    Default_Handler, /* NMI */
    Default_Handler, /* HardFault */
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    SysTick_Handler, /* SysTick */
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    ADC_COMP_IRQHandler, /* IRQ 12: ADC and COMP */
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
};

/* We link without a C library (-nostdlib). The compiler may still call these
 * two for struct copies and clears, so provide small versions. */
#include <stddef.h>
void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
