#include "i2c_bus.h"

#include <string.h>

/*
 * Register-map model of a BME280, an INA219 and an MPU6050.
 *
 * The values are not magic constants chosen to make the display look nice:
 * the model writes raw sensor registers, and the drivers in sensor_i2c.c apply
 * the real conversion rules to them. Swapping in a physical chip therefore only
 * replaces this file, not the driver code.
 */

#define BME280_REG_CALIB_T 0x88u
#define BME280_REG_CALIB_H1 0xA1u
#define BME280_REG_CALIB_H2 0xE1u
#define BME280_REG_DATA 0xF7u
#define BME280_REG_ID 0xD0u

#define INA219_REG_CURRENT 0x04u
#define INA219_REG_CALIBRATION 0x05u

#define MPU6050_REG_ACCEL_CONFIG 0x1Cu
#define MPU6050_REG_ACCEL_XOUT 0x3Bu
#define MPU6050_REG_PWR_MGMT_1 0x6Bu
#define MPU6050_REG_WHO_AM_I 0x75u

/* Datasheet calibration values for a typical BME280 part. */
#define BME280_DIG_T1 28000u
#define BME280_DIG_T2 26435
#define BME280_DIG_T3 (-1000)
#define BME280_DIG_H1 75u
#define BME280_DIG_H2 355

/*
 * Raw temperature register values. The documented compensation turns these into
 * roughly 60 C .. 81 C over a 24 step cycle, which crosses the 80 C alert limit
 * exactly like the bench simulator does.
 */
#define BME280_RAW_T_BASE 644000
#define BME280_RAW_T_STEP 2900
#define BME280_RAW_H 49152

/* INA219 current register: 1 LSB = 0.1 mA with the calibration used here. */
#define INA219_CURRENT_LSB_DIV 10
#define INA219_NOMINAL_MA 920
#define INA219_PEAK_MA 3100

/* MPU6050 at +/-2g: 16384 LSB per g. */
#define MPU6050_LSB_PER_G 16384
#define MPU6050_NOMINAL_MG 120
#define MPU6050_PEAK_MG 880

static uint32_t g_tick_ms;
static int g_fault;

static void put_u16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xFFu);
}

static void put_s16_be(uint8_t *dst, int16_t value) {
    put_u16(dst, (uint16_t)value);
}

static void put_u16_le(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)(value >> 8);
}

static uint32_t sample_index(void) {
    return g_tick_ms / 100u;
}

static int probe(uint8_t address) {
    if (g_fault) {
        return -1;
    }
    return (address == EHM_I2C_ADDR_BME280 || address == EHM_I2C_ADDR_INA219 ||
            address == EHM_I2C_ADDR_MPU6050)
               ? 0
               : -1;
}

static int read_register(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
    const uint32_t step = sample_index();

    if (g_fault) {
        return -1;
    }
    if (address == EHM_I2C_ADDR_BME280) {
        if (reg == BME280_REG_ID && length == 1u) {
            data[0] = 0x60u;
            return 0;
        }
        if (reg == BME280_REG_CALIB_T && length <= 6u) {
            uint8_t calibration[6];
            put_u16_le(&calibration[0], BME280_DIG_T1);
            put_u16_le(&calibration[2], (uint16_t)BME280_DIG_T2);
            put_u16_le(&calibration[4], (uint16_t)BME280_DIG_T3);
            memcpy(data, calibration, length);
            return 0;
        }
        if (reg == BME280_REG_CALIB_H1 && length == 1u) {
            data[0] = BME280_DIG_H1;
            return 0;
        }
        if (reg == BME280_REG_CALIB_H2 && length <= 2u) {
            uint8_t calibration[2];
            put_u16_le(calibration, (uint16_t)BME280_DIG_H2);
            memcpy(data, calibration, length);
            return 0;
        }
        if (reg == BME280_REG_DATA && length == 8u) {
            const uint32_t raw_temperature = BME280_RAW_T_BASE + (step % 24u) * BME280_RAW_T_STEP;
            /* Bytes 0..2 pressure, 3..5 temperature, 6..7 humidity. */
            data[0] = 0x50u;
            data[1] = 0x00u;
            data[2] = 0x00u;
            data[3] = (uint8_t)((raw_temperature >> 12) & 0xFFu);
            data[4] = (uint8_t)((raw_temperature >> 4) & 0xFFu);
            data[5] = (uint8_t)((raw_temperature << 4) & 0xF0u);
            data[6] = (uint8_t)((BME280_RAW_H >> 8) & 0xFFu);
            data[7] = (uint8_t)(BME280_RAW_H & 0xFFu);
            return 0;
        }
        return -1;
    }

    if (address == EHM_I2C_ADDR_INA219) {
        if (reg == INA219_REG_CURRENT && length == 2u) {
            const int32_t milliamps = (step % 15u == 0u) ? INA219_PEAK_MA : INA219_NOMINAL_MA;
            put_u16(data, (uint16_t)(int16_t)(milliamps * INA219_CURRENT_LSB_DIV));
            return 0;
        }
        if (reg == INA219_REG_CALIBRATION && length == 2u) {
            put_u16(data, 4096u);
            return 0;
        }
        return -1;
    }

    if (address == EHM_I2C_ADDR_MPU6050) {
        if (reg == MPU6050_REG_WHO_AM_I && length == 1u) {
            data[0] = 0x68u;
            return 0;
        }
        if (reg == MPU6050_REG_ACCEL_XOUT && length == 6u) {
            const int32_t vibration_mg = (step % 18u == 0u) ? MPU6050_PEAK_MG : MPU6050_NOMINAL_MG;
            const int32_t deviation = (vibration_mg * MPU6050_LSB_PER_G) / 1000;
            put_s16_be(&data[0], 0);
            put_s16_be(&data[2], 0);
            put_s16_be(&data[4], (int16_t)(MPU6050_LSB_PER_G - deviation));
            return 0;
        }
        if ((reg == MPU6050_REG_PWR_MGMT_1 || reg == MPU6050_REG_ACCEL_CONFIG) && length == 1u) {
            data[0] = 0u;
            return 0;
        }
        return -1;
    }

    return -1;
}

static int write_register(uint8_t address, uint8_t reg, const uint8_t *data, size_t length) {
    if (g_fault) {
        return -1;
    }
    if (address == EHM_I2C_ADDR_INA219 && reg == INA219_REG_CALIBRATION) {
        return 0;
    }
    if (address == EHM_I2C_ADDR_MPU6050 &&
        (reg == MPU6050_REG_PWR_MGMT_1 || reg == MPU6050_REG_ACCEL_CONFIG)) {
        return 0;
    }
    if (address == EHM_I2C_ADDR_BME280 && reg <= 0xF5u) {
        return 0; /* configuration registers are accepted and ignored by the model */
    }
    (void)data;
    (void)length;
    return -1;
}

static const i2c_bus_t g_simulated_bus = {
    "i2c-simulated",
    probe,
    write_register,
    read_register,
};

const i2c_bus_t *i2c_bus_simulated(void) {
    return &g_simulated_bus;
}

void i2c_bus_sim_set_tick(uint32_t tick_ms) {
    g_tick_ms = tick_ms;
}

void i2c_bus_sim_set_fault(int enabled) {
    g_fault = enabled;
}
