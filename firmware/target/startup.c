#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

int main(void);

void Reset_Handler(void);
void Default_Handler(void);

/* Supplied by the FreeRTOS Cortex-M3 port. */
void vPortSVCHandler(void);
void xPortPendSVHandler(void);
void xPortSysTickHandler(void);

/*
 * Interrupt vector table. The three entries that FreeRTOS needs are routed to
 * the port handlers; everything else falls through to a trap loop so an
 * unexpected fault is obvious in the serial log during emulation.
 */
__attribute__((section(".vectors"), used))
void (*const g_vectors[])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    Default_Handler,   /* NMI */
    Default_Handler,   /* HardFault */
    Default_Handler,   /* MemManage */
    Default_Handler,   /* BusFault */
    Default_Handler,   /* UsageFault */
    0, 0, 0, 0,
    vPortSVCHandler,   /* SVCall */
    Default_Handler,   /* DebugMon */
    0,
    xPortPendSVHandler, /* PendSV */
    xPortSysTickHandler /* SysTick */
};

void Reset_Handler(void) {
    uint32_t *source = &_sidata;
    uint32_t *destination = &_sdata;

    while (destination < &_edata) {
        *destination++ = *source++;
    }
    for (destination = &_sbss; destination < &_ebss;) {
        *destination++ = 0u;
    }

    (void)main();
    for (;;) {
    }
}

void Default_Handler(void) {
    /*
     * No UART access here: the fault may have happened before the UART was
     * configured. Halting is enough for the emulator to show the fault.
     */
    for (;;) {
    }
}
