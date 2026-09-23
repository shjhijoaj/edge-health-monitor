/*
 * Emulated Cortex-M3 firmware for the Edge Health Monitor.
 *
 * The firmware is deliberately split the same way a real STM32 project would
 * be: a portable protocol layer, a board layer for the UART, and FreeRTOS
 * tasks that own acquisition, transmission, health reporting and recovery.
 *
 * Tasks and priorities:
 *
 *   watchdog_task (highest)  supervises the sampling heartbeat
 *   tx_task                  drains the frame queue to the UART
 *   sample_task              builds one telemetry frame every 100 ms
 *   health_task (lowest)     emits a periodic status line
 *
 * The sampling task can be instructed to wedge on purpose. The watchdog then
 * forces a system reset through the Cortex-M AIRCR register, and the boot
 * banner on the next boot reports how often that rescue happened.
 */

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "board.h"
#include "eh_protocol.h"

#include <stdint.h>

#ifndef EHM_WATCHDOG_DEMO
#define EHM_WATCHDOG_DEMO 1
#endif

#define EHM_SAMPLE_PERIOD_MS 100u
#define EHM_HEALTH_PERIOD_MS 1000u
#define EHM_WATCHDOG_PERIOD_MS 200u
#define EHM_WATCHDOG_TIMEOUT_MS 1500u
#define EHM_STALL_AFTER_MS 8000u
#define EHM_STALL_DURATION_MS 6000u

#define EHM_FRAME_QUEUE_LENGTH 6u
#define EHM_LOG_QUEUE_LENGTH 6u
#define EHM_LOG_LINE_LENGTH 96u

/* Survives a watchdog reset, so recovery can be observed from the UART log. */
__attribute__((section(".noinit"))) static volatile uint32_t g_boot_magic;
__attribute__((section(".noinit"))) static volatile uint32_t g_boot_count;
__attribute__((section(".noinit"))) static volatile uint32_t g_watchdog_resets;

#define EHM_BOOT_MAGIC 0x45484D31u /* "EHM1" */

static QueueHandle_t g_frame_queue;
static QueueHandle_t g_log_queue;

static volatile uint32_t g_sample_heartbeat;
static volatile uint32_t g_frames_queued;
static uint16_t g_sequence = 1u;

/* ------------------------------------------------------------------ helpers */

static char *append_text(char *cursor, const char *text) {
    while (*text != '\0') {
        *cursor++ = *text++;
    }
    return cursor;
}

static char *append_uint(char *cursor, uint32_t value) {
    char digits[11];
    int index = 0;
    if (value == 0u) {
        *cursor++ = '0';
        return cursor;
    }
    while (value > 0u && index < (int)sizeof(digits)) {
        digits[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (index > 0) {
        *cursor++ = digits[--index];
    }
    return cursor;
}

static void log_line(const char *prefix, const char *key, uint32_t value) {
    char line[EHM_LOG_LINE_LENGTH];
    char *cursor = line;
    cursor = append_text(cursor, prefix);
    cursor = append_text(cursor, key);
    cursor = append_text(cursor, "=");
    cursor = append_uint(cursor, value);
    *cursor++ = '\n';
    *cursor = '\0';
    (void)xQueueSend(g_log_queue, line, 0u);
}

static void log_message(const char *prefix, const char *text) {
    char line[EHM_LOG_LINE_LENGTH];
    char *cursor = line;
    cursor = append_text(cursor, prefix);
    cursor = append_text(cursor, text);
    *cursor++ = '\n';
    *cursor = '\0';
    (void)xQueueSend(g_log_queue, line, 0u);
}

static void system_reset(void) {
    /* AIRCR: VECTKEY | SYSRESETREQ */
    *(volatile uint32_t *)0xE000ED0Cu = 0x05FA0004u;
    for (;;) {
    }
}

/* -------------------------------------------------------------- FreeRTOS hooks */

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task;
    (void)name;
    system_reset();
}

void vApplicationMallocFailedHook(void) {
    system_reset();
}

/* ---------------------------------------------------------------------- tasks */

static void watchdog_task(void *argument) {
    (void)argument;
    uint32_t last_heartbeat = g_sample_heartbeat;
    uint32_t stalled_ms = 0u;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(EHM_WATCHDOG_PERIOD_MS));
        if (g_sample_heartbeat == last_heartbeat) {
            stalled_ms += EHM_WATCHDOG_PERIOD_MS;
            if (stalled_ms >= EHM_WATCHDOG_TIMEOUT_MS) {
                log_message("WATCHDOG ", "sampling task stalled, forcing reset");
                /* Give the transmit task a chance to flush the message. */
                vTaskDelay(pdMS_TO_TICKS(120u));
                g_watchdog_resets = g_watchdog_resets + 1u;
                system_reset();
            }
        } else {
            last_heartbeat = g_sample_heartbeat;
            stalled_ms = 0u;
        }
    }
}

static void tx_task(void *argument) {
    (void)argument;
    uint8_t frame[EH_MAX_FRAME_SIZE];
    char line[EHM_LOG_LINE_LENGTH];

    for (;;) {
        if (xQueueReceive(g_frame_queue, frame, 0u) == pdPASS) {
            board_uart_write("FRAME ");
            board_uart_write_frame_hex(frame, 32u);
        } else if (xQueueReceive(g_log_queue, line, 0u) == pdPASS) {
            board_uart_write(line);
        } else {
            vTaskDelay(pdMS_TO_TICKS(2u));
        }
    }
}

static eh_sample_t make_sample(uint16_t sequence) {
    const uint16_t phase = (uint16_t)((sequence - 1u) % 24u);
    eh_sample_t sample;
    sample.temperature_centi_c = (int32_t)(6200 + (int32_t)phase * 90);
    sample.humidity_centi_pct = 4860;
    sample.current_ma = (sequence % 15u == 0u) ? 3100 : 920;
    sample.vibration_rms_mg = (sequence % 18u == 0u) ? 880 : 120;
    sample.status = (sequence % 23u == 0u) ? 1u : 0u;
    sample.uptime_s = (uint32_t)sequence / 10u;
    return sample;
}

static void sample_task(void *argument) {
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
#if EHM_WATCHDOG_DEMO
    uint32_t elapsed_ms = 0u;
    int stall_injected = 0;
#endif

    for (;;) {
#if EHM_WATCHDOG_DEMO
        if (stall_injected == 0 && elapsed_ms >= EHM_STALL_AFTER_MS) {
            stall_injected = 1;
            log_message("FAULT ", "deliberate sensor read hang injected");
            /* Modelling a blocking driver call: no heartbeat update happens. */
            vTaskDelay(pdMS_TO_TICKS(EHM_STALL_DURATION_MS));
        }
#endif
        const eh_sample_t sample = make_sample(g_sequence);
        uint8_t frame[EH_MAX_FRAME_SIZE];
        size_t frame_size = 0u;

        if (eh_encode_sample(&sample, g_sequence, frame, sizeof(frame), &frame_size) != 0) {
            if (xQueueSend(g_frame_queue, frame, 0u) == pdPASS) {
                g_frames_queued = g_frames_queued + 1u;
            }
        }
        g_sequence = (uint16_t)(g_sequence + 1u);
        g_sample_heartbeat = g_sample_heartbeat + 1u;
#if EHM_WATCHDOG_DEMO
        elapsed_ms += EHM_SAMPLE_PERIOD_MS;
#endif

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(EHM_SAMPLE_PERIOD_MS));
    }
}

static void health_task(void *argument) {
    (void)argument;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(EHM_HEALTH_PERIOD_MS));
        log_line("STAT ", "boot", g_boot_count);
        log_line("STAT ", "wd_resets", g_watchdog_resets);
        log_line("STAT ", "frames", g_frames_queued);
        log_line("STAT ", "seq", (uint32_t)g_sequence);
        log_line("STAT ", "uptime_ms", (uint32_t)xTaskGetTickCount());
    }
}

/* ----------------------------------------------------------------------- main */

int main(void) {
    board_uart_init();

    if (g_boot_magic != EHM_BOOT_MAGIC) {
        g_boot_magic = EHM_BOOT_MAGIC;
        g_boot_count = 0u;
        g_watchdog_resets = 0u;
    }
    g_boot_count = g_boot_count + 1u;

    board_uart_write("\nBOOT edge-health-monitor board=");
    board_uart_write(EHM_BOARD_NAME);
    board_uart_write("\n");
    board_uart_write("BOOT boot_count=");
    {
        char number[16];
        char *cursor = append_uint(number, g_boot_count);
        cursor = append_text(cursor, " wd_resets=");
        cursor = append_uint(cursor, g_watchdog_resets);
        *cursor++ = '\n';
        *cursor = '\0';
        board_uart_write(number);
    }

    g_frame_queue = xQueueCreate(EHM_FRAME_QUEUE_LENGTH, EH_MAX_FRAME_SIZE);
    g_log_queue = xQueueCreate(EHM_LOG_QUEUE_LENGTH, EHM_LOG_LINE_LENGTH);
    if (g_frame_queue == 0 || g_log_queue == 0) {
        board_uart_write("FATAL queue creation failed\n");
        for (;;) {
        }
    }

    (void)xTaskCreate(tx_task, "tx", configMINIMAL_STACK_SIZE * 2u, 0, 3, 0);
    (void)xTaskCreate(sample_task, "sample", configMINIMAL_STACK_SIZE * 3u, 0, 2, 0);
    (void)xTaskCreate(health_task, "health", configMINIMAL_STACK_SIZE * 2u, 0, 1, 0);
    (void)xTaskCreate(watchdog_task, "watchdog", configMINIMAL_STACK_SIZE * 2u, 0, 4, 0);

    vTaskStartScheduler();

    board_uart_write("FATAL scheduler returned\n");
    for (;;) {
    }
}
