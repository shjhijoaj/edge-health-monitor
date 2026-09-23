#include "i2c_bus.h"

/*
 * Template for a real board. Enable it by building with EHM_USE_HAL_I2C and
 * pointing it at the CubeMX handle, for example:
 *
 *   arm-none-eabi-gcc ... -DEHM_USE_HAL_I2C -DEHM_HAL_I2C_HANDLE=hi2c1
 *
 * These three functions are the only place where STM32 HAL calls belong; the
 * drivers in sensor_i2c.c stay unchanged when moving to hardware.
 */

#if defined(EHM_USE_HAL_I2C)

#include "stm32f1xx_hal.h"

#ifndef EHM_HAL_I2C_HANDLE
#define EHM_HAL_I2C_HANDLE hi2c1
#endif

#define EHM_HAL_I2C_TIMEOUT_MS 50u

static int hal_probe(uint8_t address) {
    return (HAL_I2C_IsDeviceReady(&EHM_HAL_I2C_HANDLE, (uint16_t)(address << 1), 3u,
                                  EHM_HAL_I2C_TIMEOUT_MS) == HAL_OK)
               ? 0
               : -1;
}

static int hal_write(uint8_t address, uint8_t reg, const uint8_t *data, size_t length) {
    const HAL_StatusTypeDef status =
        HAL_I2C_Mem_Write(&EHM_HAL_I2C_HANDLE, (uint16_t)(address << 1), reg,
                          I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, (uint16_t)length,
                          EHM_HAL_I2C_TIMEOUT_MS);
    return (status == HAL_OK) ? 0 : -1;
}

static int hal_read(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
    const HAL_StatusTypeDef status =
        HAL_I2C_Mem_Read(&EHM_HAL_I2C_HANDLE, (uint16_t)(address << 1), reg,
                         I2C_MEMADD_SIZE_8BIT, data, (uint16_t)length, EHM_HAL_I2C_TIMEOUT_MS);
    return (status == HAL_OK) ? 0 : -1;
}

static const i2c_bus_t g_hal_bus = {
    "i2c-hal",
    hal_probe,
    hal_write,
    hal_read,
};

const i2c_bus_t *i2c_bus_hal(void) {
    return &g_hal_bus;
}

#else

const i2c_bus_t *i2c_bus_hal(void) {
    return 0; /* built without a HAL: the driver stays on the simulated bus */
}

#endif
