#ifndef EH_MEASUREMENT_H
#define EH_MEASUREMENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t eh_clamp_i32(int32_t value, int32_t minimum, int32_t maximum);
uint32_t eh_rms_i16(const int16_t *samples, size_t count);

#ifdef __cplusplus
}
#endif

#endif
