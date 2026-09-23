#include "board.h"

#include <stdint.h>

#if defined(EHM_BOARD_MPS2)

/* CMSDK UART on the MPS2 AN385 board. */
#define CMSDK_UART0_BASE 0x40004000u

typedef struct {
    volatile uint32_t data;
    volatile uint32_t state;
    volatile uint32_t ctrl;
    volatile uint32_t int_status;
    volatile uint32_t bauddiv;
} cmsdk_uart_t;

#define CMSDK_UART ((cmsdk_uart_t *)CMSDK_UART0_BASE)
#define CMSDK_UART_STATE_TX_FULL (1u << 0)
#define CMSDK_UART_CTRL_TX_EN (1u << 0)
#define CMSDK_UART_CTRL_RX_EN (1u << 1)

static void wait_tx_ready(void) {
    while ((CMSDK_UART->state & CMSDK_UART_STATE_TX_FULL) != 0u) {
    }
}

void board_uart_init(void) {
    CMSDK_UART->bauddiv = 16u;
    CMSDK_UART->ctrl = CMSDK_UART_CTRL_TX_EN | CMSDK_UART_CTRL_RX_EN;
}

void board_uart_putc(char character) {
    wait_tx_ready();
    CMSDK_UART->data = (uint32_t)(uint8_t)character;
}

#else

/* PL011 UART as modelled on the Stellaris LM3S6965 evaluation board. */
#define PL011_UART0_BASE 0x4000C000u

typedef struct {
    volatile uint32_t dr;
    volatile uint32_t rsr_ecr;
    volatile uint32_t reserved0[4];
    volatile uint32_t fr;
    volatile uint32_t reserved1;
    volatile uint32_t ilpr;
    volatile uint32_t ibrd;
    volatile uint32_t fbrd;
    volatile uint32_t lcrh;
    volatile uint32_t cr;
    volatile uint32_t ifls;
    volatile uint32_t imsc;
    volatile uint32_t ris;
    volatile uint32_t mis;
    volatile uint32_t icr;
} pl011_t;

#define PL011_UART ((pl011_t *)PL011_UART0_BASE)
#define PL011_FR_TXFF (1u << 5)
#define PL011_CR_UARTEN (1u << 0)
#define PL011_CR_TXE (1u << 8)
#define PL011_CR_RXE (1u << 9)
#define PL011_LCRH_8N1_NO_FIFO 0x60u

static void wait_tx_ready(void) {
    while ((PL011_UART->fr & PL011_FR_TXFF) != 0u) {
    }
}

void board_uart_init(void) {
    PL011_UART->cr = 0u;                       /* disable while configuring */
    PL011_UART->ibrd = 26u;                    /* ~115200 baud, ignored by the model */
    PL011_UART->fbrd = 3u;
    PL011_UART->lcrh = PL011_LCRH_8N1_NO_FIFO; /* 8 data bits, no parity, 1 stop */
    PL011_UART->ifls = 0x12u;
    PL011_UART->cr = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
}

void board_uart_putc(char character) {
    wait_tx_ready();
    PL011_UART->dr = (uint32_t)(uint8_t)character;
}

#endif

void board_uart_write(const char *text) {
    while (*text != '\0') {
        if (*text == '\n') {
            board_uart_putc('\r');
        }
        board_uart_putc(*text++);
    }
}

static const char kHexDigits[] = "0123456789ABCDEF";

void board_uart_write_frame_hex(const uint8_t *frame, size_t length) {
    size_t index;
    for (index = 0u; index < length; ++index) {
        if (index != 0u) {
            board_uart_putc(' ');
        }
        board_uart_putc(kHexDigits[(frame[index] >> 4) & 0x0Fu]);
        board_uart_putc(kHexDigits[frame[index] & 0x0Fu]);
    }
    board_uart_write("\n");
}

void board_delay_ms(uint32_t milliseconds) {
    /* SysTick is owned by FreeRTOS after the scheduler starts, so use a simple
     * cycle-counted busy loop before that point. */
    while (milliseconds-- > 0u) {
        volatile uint32_t cycles = EHM_CPU_CLOCK_HZ / 2000u;
        while (cycles-- > 0u) {
        }
    }
}
