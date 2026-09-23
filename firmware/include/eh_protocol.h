#ifndef EH_PROTOCOL_H
#define EH_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EH_SYNC_0 0xA5u
#define EH_SYNC_1 0x5Au
#define EH_PROTOCOL_VERSION 1u
#define EH_SAMPLE_PAYLOAD_SIZE 22u
#define EH_MAX_PAYLOAD 64u
#define EH_MAX_FRAME_SIZE (8u + EH_MAX_PAYLOAD + 2u)

typedef struct {
    int32_t temperature_centi_c;
    int32_t humidity_centi_pct;
    int32_t current_ma;
    int32_t vibration_rms_mg;
    uint16_t status;
    uint32_t uptime_s;
} eh_sample_t;

typedef struct {
    uint8_t frame[EH_MAX_FRAME_SIZE];
    uint16_t length;
    uint16_t expected_length;
    uint8_t state;
} eh_decoder_t;

uint16_t eh_crc16(const uint8_t *data, size_t length);

int eh_encode_sample(const eh_sample_t *sample,
                     uint16_t sequence,
                     uint8_t *output,
                     size_t capacity,
                     size_t *written);

void eh_decoder_init(eh_decoder_t *decoder);

/* Feed one byte. Returns 1 when a complete sample is written to output. */
int eh_decoder_feed(eh_decoder_t *decoder,
                    uint8_t byte,
                    eh_sample_t *output,
                    uint16_t *sequence);

#ifdef __cplusplus
}
#endif

#endif
