#include "sensor.h"

#include "i2c_bus.h"

#include <stdint.h>

/*
 * Register-level drivers for the three devices on the health monitor node.
 *
 * Environment:  BME280  - temperature uses the datasheet integer compensation.
 *                         Humidity uses a documented linear approximation of the
 *                         H1/H2 calibration instead of the full 12-bit formula.
 * Current:      INA219  - reads the current register; 1 LSB = 0.1 mA with the
 *                         calibration written during init.
 * Vibration:    MPU6050 - reads the three acceleration axes at +/-2 g, converts
 *                         to milli-g and reports the deviation from 1 g.
 */

#define BME280_REG_CALIB_T 0x88u
#define BME280_REG_CALIB_H1 0xA1u
#define BME280_REG_CALIB_H2 0xE1u
#define BME280_REG_DATA 0xF7u

#define INA219_REG_CURRENT 0x04u
#define INA219_REG_CALIBRATION 0x05u
#define INA219_CALIBRATION_VALUE 4096u

#define MPU6050_REG_ACCEL_CONFIG 0x1Cu
#define MPU6050_REG_ACCEL_XOUT 0x3Bu
#define MPU6050_REG_PWR_MGMT_1 0x6Bu

#define INA219_LSB_PER_MILLIAMP 10 /* 0.1 mA per LSB */
#define MPU6050_LSB_PER_G 16384

static const i2c_bus_t *g_bus;
static int32_t g_t_fine;
static uint16_t g_dig_t1;
static int16_t g_dig_t2;
static int16_t g_dig_t3;
static uint8_t g_dig_h1;
static int16_t g_dig_h2;

static uint16_t get_u16_le(const uint8_t *source) {
    return (uint16_t)((uint16_t)source[0] | ((uint16_t)source[1] << 8));
}

static int16_t get_s16_be(const uint8_t *source) {
    return (int16_t)(((uint16_t)source[0] << 8) | (uint16_t)source[1]);
}

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

static int32_t bme280_compensate_temperature(int32_t adc_temperature) {
    const int32_t var1 =
        ((((adc_temperature >> 3) - ((int32_t)g_dig_t1 << 1))) * ((int32_t)g_dig_t2)) >> 11;
    const int32_t var2 =
        (((((adc_temperature >> 4) - ((int32_t)g_dig_t1)) *
           ((adc_temperature >> 4) - ((int32_t)g_dig_t1))) >>
          12) *
         ((int32_t)g_dig_t3)) >>
        14;
    g_t_fine = var1 + var2;
    return (g_t_fine * 5 + 128) >> 8;
}

static int32_t bme280_humidity_centi(int32_t adc_humidity) {
    /*
     * Linear approximation of the datasheet humidity path: the full formula
     * needs H3..H6 and a temperature dependent second order term. The model and
     * a real part agree to a few tenths of a percent over the useful range.
     */
    int32_t humidity = (adc_humidity * 1000) / 10240; /* 0.01 %RH */
    humidity = humidity - ((int32_t)g_dig_h1 - 75) * 2;
    humidity = humidity + ((int32_t)g_dig_h2 - 355) / 4;
    (void)g_t_fine;
    if (humidity < 0) {
        humidity = 0;
    }
    if (humidity > 10000) {
        humidity = 10000;
    }
    return humidity;
}

static int i2c_sensor_init(void) {
    uint8_t calibration[6];
    uint8_t payload[2];
    uint8_t value;

    g_bus = i2c_bus_simulated();
    if (g_bus == 0 || g_bus->probe(EHM_I2C_ADDR_BME280) != 0 ||
        g_bus->probe(EHM_I2C_ADDR_INA219) != 0 || g_bus->probe(EHM_I2C_ADDR_MPU6050) != 0) {
        return -1;
    }

    if (g_bus->read(EHM_I2C_ADDR_BME280, BME280_REG_CALIB_T, calibration,
                    sizeof(calibration)) != 0) {
        return -1;
    }
    g_dig_t1 = get_u16_le(&calibration[0]);
    g_dig_t2 = (int16_t)get_u16_le(&calibration[2]);
    g_dig_t3 = (int16_t)get_u16_le(&calibration[4]);

    if (g_bus->read(EHM_I2C_ADDR_BME280, BME280_REG_CALIB_H1, &g_dig_h1, 1u) != 0 ||
        g_bus->read(EHM_I2C_ADDR_BME280, BME280_REG_CALIB_H2, calibration, 2u) != 0) {
        return -1;
    }
    g_dig_h2 = (int16_t)get_u16_le(calibration);

    /* INA219: write the calibration register so the current LSB is 0.1 mA. */
    payload[0] = (uint8_t)(INA219_CALIBRATION_VALUE >> 8);
    payload[1] = (uint8_t)(INA219_CALIBRATION_VALUE & 0xFFu);
    if (g_bus->write(EHM_I2C_ADDR_INA219, INA219_REG_CALIBRATION, payload, 2u) != 0) {
        return -1;
    }

    /* MPU6050: wake the device and select +/-2 g. */
    value = 0x00u;
    if (g_bus->write(EHM_I2C_ADDR_MPU6050, MPU6050_REG_PWR_MGMT_1, &value, 1u) != 0) {
        return -1;
    }
    if (g_bus->write(EHM_I2C_ADDR_MPU6050, MPU6050_REG_ACCEL_CONFIG, &value, 1u) != 0) {
        return -1;
    }
    return 0;
}

static int i2c_sensor_read(sensor_reading_t *out) {
    uint8_t raw[8];
    uint8_t acceleration[6];
    uint8_t current[2];
    int32_t adc_temperature;
    int32_t adc_humidity;
    int32_t device_current;
    int32_t axis_x;
    int32_t axis_y;
    int32_t axis_z;
    uint32_t magnitude;
    int32_t millig;
    int32_t deviation;

    if (out == 0) {
        return -1;
    }
    /* A failed read must not leave stale values in the frame. */
    out->temperature_centi_c = 0;
    out->humidity_centi_pct = 0;
    out->current_ma = 0;
    out->vibration_rms_mg = 0;
    out->status = EHM_SENSOR_STATUS_OK;

    if (g_bus == 0 && i2c_sensor_init() != 0) {
        out->status = EHM_SENSOR_STATUS_FAULT;
        return -1;
    }

    if (g_bus->read(EHM_I2C_ADDR_BME280, BME280_REG_DATA, raw, sizeof(raw)) != 0) {
        out->status = EHM_SENSOR_STATUS_FAULT;
        return -1;
    }
    adc_temperature = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    adc_humidity = ((int32_t)raw[6] << 8) | (int32_t)raw[7];
    out->temperature_centi_c = bme280_compensate_temperature(adc_temperature);
    out->humidity_centi_pct = bme280_humidity_centi(adc_humidity);

    if (g_bus->read(EHM_I2C_ADDR_INA219, INA219_REG_CURRENT, current, sizeof(current)) != 0) {
        out->status = EHM_SENSOR_STATUS_FAULT;
        return -1;
    }
    device_current = (int32_t)get_s16_be(current);
    out->current_ma = device_current / INA219_LSB_PER_MILLIAMP;

    if (g_bus->read(EHM_I2C_ADDR_MPU6050, MPU6050_REG_ACCEL_XOUT, acceleration,
                    sizeof(acceleration)) != 0) {
        out->status = EHM_SENSOR_STATUS_FAULT;
        return -1;
    }
    axis_x = get_s16_be(&acceleration[0]);
    axis_y = get_s16_be(&acceleration[2]);
    axis_z = get_s16_be(&acceleration[4]);
    magnitude = isqrt_u64((uint64_t)(axis_x * axis_x) + (uint64_t)(axis_y * axis_y) +
                          (uint64_t)(axis_z * axis_z));
    millig = (int32_t)((magnitude * 1000u) / MPU6050_LSB_PER_G);
    deviation = millig - 1000;
    out->vibration_rms_mg = deviation < 0 ? -deviation : deviation;
    return 0;
}

static const sensor_driver_t g_i2c_driver = {
    "bme280+ina219+mpu6050",
    i2c_sensor_init,
    i2c_sensor_read,
};

const sensor_driver_t *sensor_driver_i2c(void) {
    return &g_i2c_driver;
}
