#include "i2c_bus.h"
#include "sensor.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, \
                    __LINE__);                                                  \
            return 1;                                                           \
        }                                                                       \
    } while (0)

/*
 * The I2C drivers must produce sane engineering units from raw registers. The
 * bus is the same register-map model the firmware uses in emulation, so this
 * test covers the conversion maths, the fault path and the driver interface
 * without any hardware.
 */
static int test_i2c_driver(void) {
    const sensor_driver_t *driver = sensor_driver_i2c();
    sensor_reading_t reading;
    int32_t min_temperature = 100000;
    int32_t max_temperature = -100000;
    int32_t max_current = 0;
    int32_t max_vibration = 0;
    uint32_t step;

    CHECK(driver != 0);
    CHECK(driver->name != 0);
    CHECK(driver->init() == 0);

    for (step = 0u; step < 72u; ++step) {
        i2c_bus_sim_set_fault(0);
        i2c_bus_sim_set_tick(step * 100u);
        CHECK(driver->read(&reading) == 0);
        CHECK(reading.status == EHM_SENSOR_STATUS_OK);
        CHECK(reading.humidity_centi_pct > 3000 && reading.humidity_centi_pct < 7000);
        CHECK(reading.current_ma > 0);

        if (reading.temperature_centi_c < min_temperature) {
            min_temperature = reading.temperature_centi_c;
        }
        if (reading.temperature_centi_c > max_temperature) {
            max_temperature = reading.temperature_centi_c;
        }
        if (reading.current_ma > max_current) {
            max_current = reading.current_ma;
        }
        if (reading.vibration_rms_mg > max_vibration) {
            max_vibration = reading.vibration_rms_mg;
        }
    }

    /* The modelled device must exercise both the normal and the alert range. */
    CHECK(min_temperature < 6500);
    CHECK(max_temperature > 8000);
    CHECK(max_current >= 3000);
    CHECK(max_vibration >= 800);

    /* A missing device must be reported, not silently turned into zeros. */
    i2c_bus_sim_set_fault(1);
    CHECK(driver->read(&reading) != 0);
    CHECK((reading.status & EHM_SENSOR_STATUS_FAULT) != 0u);
    i2c_bus_sim_set_fault(0);

    printf("i2c sensors ok: temperature %.2f..%.2f C, current max %d mA, vibration max %d mg\n",
           (double)min_temperature / 100.0, (double)max_temperature / 100.0, (int)max_current,
           (int)max_vibration);
    return 0;
}

static int test_synthetic_driver(void) {
    const sensor_driver_t *driver = sensor_driver_sim();
    sensor_reading_t reading;

    CHECK(driver != 0);
    CHECK(driver->init() == 0);
    CHECK(driver->read(&reading) == 0);
    CHECK(reading.humidity_centi_pct == 4860);
    CHECK(reading.temperature_centi_c == 6200);
    return 0;
}

static int test_driver_selection(void) {
    const sensor_driver_t *original = sensor_active();

    CHECK(original != 0);
    sensor_select(sensor_driver_sim());
    CHECK(sensor_active() == sensor_driver_sim());
    sensor_select(original);
    return 0;
}

int main(void) {
    if (test_i2c_driver() != 0) {
        return 1;
    }
    if (test_synthetic_driver() != 0) {
        return 1;
    }
    if (test_driver_selection() != 0) {
        return 1;
    }
    puts("sensor tests passed");
    return 0;
}
