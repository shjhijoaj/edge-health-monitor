#ifndef EHM_BOARD_H
#define EHM_BOARD_H

#include <stddef.h>
#include <stdint.h>

/*
 * Board support for the emulated Cortex-M3 target.
 *
 * The firmware is written against a tiny set of memory-mapped registers so it
 * can run on an emulated board without any vendor HAL. Two boards are
 * supported; pick one at build time:
 *
 *   EHM_BOARD_LM3S  (default) QEMU "lm3s6965evb", PL011 UART at 0x4000C000
 *   EHM_BOARD_MPS2            QEMU "mps2-an385",   CMSDK UART at 0x40004000
 *
 * Everything above this file (protocol, measurement helpers, task logic) stays
 * portable, which is the same layering used when the code is moved to a real
 * STM32 HAL project.
 */

#if defined(EHM_BOARD_MPS2)
#define EHM_BOARD_NAME "mps2-an385"
#else
#define EHM_BOARD_NAME "lm3s6965evb"
#endif

#define EHM_CPU_CLOCK_HZ 50000000u

void board_uart_init(void);
void board_uart_putc(char character);
void board_uart_write(const char *text);

/* Prints one protocol frame as "A5 5A 01 01 ..." followed by a newline. */
void board_uart_write_frame_hex(const uint8_t *frame, size_t length);

void board_delay_ms(uint32_t milliseconds);

#endif
