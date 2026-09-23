#include "eh_measurement.h"

#include <stdint.h>

static uint32_t isqrt_u64(uint64_t value) {
    uint64_t low = 0u;
    uint64_t high = 65536u;
    while (low + 1u < high) {
        const uint64_t middle = (low + high) / 2u;
        if (middle * middle <= value) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return (uint32_t)low;
}

int32_t eh_clamp_i32(int32_t value, int32_t minimum, int32_t maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

uint32_t eh_rms_i16(const int16_t *samples, size_t count) {
    uint64_t sum = 0u;
    size_t i;
    if (samples == NULL || count == 0u) {
        return 0u;
    }
    for (i = 0u; i < count; ++i) {
        int32_t value = samples[i];
        sum += (uint64_t)(value * value);
    }
    return isqrt_u64(sum / count);
}
