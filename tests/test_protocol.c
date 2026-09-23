#include "eh_measurement.h"
#include "eh_protocol.h"

#include <stdio.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 0; \
        } \
    } while (0)

static int test_roundtrip(void) {
    const eh_sample_t expected = {6300, 5125, 920, 180, 0, 45};
    eh_sample_t actual = {0};
    eh_decoder_t decoder;
    uint8_t frame[EH_MAX_FRAME_SIZE] = {0};
    size_t frame_size = 0u;
    uint16_t sequence = 0u;
    size_t index;
    int decoded = 0;

    CHECK(eh_encode_sample(&expected, 42u, frame, sizeof(frame), &frame_size) == 1);
    CHECK(frame_size == 32u);
    eh_decoder_init(&decoder);
    for (index = 0u; index < frame_size; ++index) {
        decoded |= eh_decoder_feed(&decoder, frame[index], &actual, &sequence);
    }
    CHECK(decoded == 1);
    CHECK(sequence == 42u);
    CHECK(expected.temperature_centi_c == actual.temperature_centi_c);
    CHECK(expected.humidity_centi_pct == actual.humidity_centi_pct);
    CHECK(expected.current_ma == actual.current_ma);
    CHECK(expected.vibration_rms_mg == actual.vibration_rms_mg);
    CHECK(expected.status == actual.status);
    CHECK(expected.uptime_s == actual.uptime_s);
    return 1;
}

static int test_crc_rejection(void) {
    const eh_sample_t expected = {6300, 5125, 920, 180, 0, 45};
    eh_sample_t actual = {0};
    eh_decoder_t decoder;
    uint8_t frame[EH_MAX_FRAME_SIZE] = {0};
    size_t frame_size = 0u;
    uint16_t sequence = 0u;
    size_t index;

    CHECK(eh_encode_sample(&expected, 7u, frame, sizeof(frame), &frame_size) == 1);
    frame[19] ^= 0x01u;
    eh_decoder_init(&decoder);
    for (index = 0u; index < frame_size; ++index) {
        CHECK(eh_decoder_feed(&decoder, frame[index], &actual, &sequence) == 0);
    }
    return 1;
}

static int test_measurement_helpers(void) {
    const int16_t waveform[] = {-100, 100, -100, 100};
    CHECK(eh_clamp_i32(12, 0, 10) == 10);
    CHECK(eh_clamp_i32(-1, 0, 10) == 0);
    CHECK(eh_rms_i16(waveform, 4u) == 100u);
    return 1;
}

int main(void) {
    if (!test_roundtrip() || !test_crc_rejection() || !test_measurement_helpers()) {
        return 1;
    }
    puts("protocol tests passed");
    return 0;
}
