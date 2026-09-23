#include "eh_protocol.h"

#include <string.h>

static void put_u16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void put_u32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16u) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint16_t get_u16(const uint8_t *src) {
    return (uint16_t)src[0] | (uint16_t)((uint16_t)src[1] << 8u);
}

static uint32_t get_u32(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8u) |
           ((uint32_t)src[2] << 16u) |
           ((uint32_t)src[3] << 24u);
}

static void reset_decoder(eh_decoder_t *decoder) {
    decoder->length = 0u;
    decoder->expected_length = 0u;
    decoder->state = 0u;
}

uint16_t eh_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFFu;
    size_t i;
    for (i = 0u; i < length; ++i) {
        uint8_t bit;
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u ? (uint16_t)((crc >> 1u) ^ 0xA001u)
                                   : (uint16_t)(crc >> 1u);
        }
    }
    return crc;
}

int eh_encode_sample(const eh_sample_t *sample,
                     uint16_t sequence,
                     uint8_t *output,
                     size_t capacity,
                     size_t *written) {
    const size_t frame_size = 8u + EH_SAMPLE_PAYLOAD_SIZE + 2u;
    uint16_t crc;
    if (sample == NULL || output == NULL || written == NULL || capacity < frame_size) {
        return 0;
    }

    output[0] = EH_SYNC_0;
    output[1] = EH_SYNC_1;
    output[2] = EH_PROTOCOL_VERSION;
    output[3] = 0x01u;
    put_u16(&output[4], sequence);
    put_u16(&output[6], EH_SAMPLE_PAYLOAD_SIZE);
    put_u32(&output[8], (uint32_t)sample->temperature_centi_c);
    put_u32(&output[12], (uint32_t)sample->humidity_centi_pct);
    put_u32(&output[16], (uint32_t)sample->current_ma);
    put_u32(&output[20], (uint32_t)sample->vibration_rms_mg);
    put_u16(&output[24], sample->status);
    put_u32(&output[26], sample->uptime_s);
    crc = eh_crc16(output, 8u + EH_SAMPLE_PAYLOAD_SIZE);
    put_u16(&output[30], crc);
    *written = frame_size;
    return 1;
}

void eh_decoder_init(eh_decoder_t *decoder) {
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(*decoder));
    }
}

int eh_decoder_feed(eh_decoder_t *decoder,
                    uint8_t byte,
                    eh_sample_t *output,
                    uint16_t *sequence) {
    uint16_t payload_length;
    uint16_t expected_crc;
    uint16_t actual_crc;
    const uint8_t *payload;

    if (decoder == NULL || output == NULL || sequence == NULL) {
        return 0;
    }

    if (decoder->state == 0u) {
        if (byte == EH_SYNC_0) {
            decoder->frame[0] = byte;
            decoder->length = 1u;
            decoder->state = 1u;
        }
        return 0;
    }
    if (decoder->state == 1u) {
        if (byte != EH_SYNC_1) {
            reset_decoder(decoder);
            if (byte == EH_SYNC_0) {
                decoder->frame[0] = byte;
                decoder->length = 1u;
                decoder->state = 1u;
            }
            return 0;
        }
        decoder->frame[1] = byte;
        decoder->length = 2u;
        decoder->state = 2u;
        return 0;
    }

    if (decoder->length >= EH_MAX_FRAME_SIZE) {
        reset_decoder(decoder);
        return 0;
    }
    decoder->frame[decoder->length++] = byte;

    if (decoder->length == 8u) {
        payload_length = get_u16(&decoder->frame[6]);
        if (decoder->frame[2] != EH_PROTOCOL_VERSION ||
            payload_length != EH_SAMPLE_PAYLOAD_SIZE ||
            payload_length > EH_MAX_PAYLOAD) {
            reset_decoder(decoder);
            return 0;
        }
        decoder->expected_length = (uint16_t)(8u + payload_length + 2u);
    }
    if (decoder->expected_length == 0u || decoder->length < decoder->expected_length) {
        return 0;
    }

    expected_crc = get_u16(&decoder->frame[decoder->expected_length - 2u]);
    actual_crc = eh_crc16(decoder->frame, decoder->expected_length - 2u);
    if (expected_crc != actual_crc || decoder->frame[3] != 0x01u) {
        reset_decoder(decoder);
        return 0;
    }

    payload = &decoder->frame[8];
    output->temperature_centi_c = (int32_t)get_u32(&payload[0]);
    output->humidity_centi_pct = (int32_t)get_u32(&payload[4]);
    output->current_ma = (int32_t)get_u32(&payload[8]);
    output->vibration_rms_mg = (int32_t)get_u32(&payload[12]);
    output->status = get_u16(&payload[16]);
    output->uptime_s = get_u32(&payload[18]);
    *sequence = get_u16(&decoder->frame[4]);
    reset_decoder(decoder);
    return 1;
}
