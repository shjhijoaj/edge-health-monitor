#ifndef EHM_I2C_BUS_H
#define EHM_I2C_BUS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal I2C bus abstraction.
 *
 * The sensor drivers only know these three operations, so the same driver code
 * runs against the register-map model used in emulation and host tests, and
 * against a real STM32 HAL implementation (see i2c_bus_hal.c).
 */

#define EHM_I2C_ADDR_BME280 0x76u  /* temperature and humidity */
#define EHM_I2C_ADDR_INA219 0x40u  /* current and bus voltage */
#define EHM_I2C_ADDR_MPU6050 0x68u /* acceleration / vibration */

typedef struct {
    const char *name;
    int (*probe)(uint8_t address);
    int (*write)(uint8_t address, uint8_t reg, const uint8_t *data, size_t length);
    int (*read)(uint8_t address, uint8_t reg, uint8_t *data, size_t length);
} i2c_bus_t;

/* Register-map model of the three devices; runs on the target and on the host. */
const i2c_bus_t *i2c_bus_simulated(void);

/* Drives the model's register contents. Call once per sampling period. */
void i2c_bus_sim_set_tick(uint32_t tick_ms);

/* Models a bus problem: devices stop acknowledging reads. */
void i2c_bus_sim_set_fault(int enabled);

/* Real HAL-backed bus. Only available when built with EHM_USE_HAL_I2C. */
const i2c_bus_t *i2c_bus_hal(void);

#endif
