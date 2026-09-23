#ifndef EHM_SENSOR_H
#define EHM_SENSOR_H

#include <stdint.h>

/* Status bit reported to the gateway. */
#define EHM_SENSOR_STATUS_OK 0u
#define EHM_SENSOR_STATUS_FAULT (1u << 0)

typedef struct {
    int32_t temperature_centi_c;
    int32_t humidity_centi_pct;
    int32_t current_ma;
    int32_t vibration_rms_mg;
    uint16_t status;
} sensor_reading_t;

typedef struct {
    const char *name;
    int (*init)(void);
    int (*read)(sensor_reading_t *out);
} sensor_driver_t;

/* Real register-level drivers reading BME280 / INA219 / MPU6050 over an I2C bus. */
const sensor_driver_t *sensor_driver_i2c(void);

/* Direct synthetic values, no bus. Kept for bench demos without any model. */
const sensor_driver_t *sensor_driver_sim(void);

/* Driver used by the firmware sampling task. Defaults to the I2C stack; the
 * simulated backend can be selected at build time or by a test. */
const sensor_driver_t *sensor_active(void);
void sensor_select(const sensor_driver_t *driver);

#endif
