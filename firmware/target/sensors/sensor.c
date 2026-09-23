#include "sensor.h"

#ifndef EHM_SENSOR_BACKEND_SIM
#define EHM_SENSOR_BACKEND_SIM 0
#endif

static const sensor_driver_t *g_active;

const sensor_driver_t *sensor_active(void) {
    if (g_active == 0) {
#if EHM_SENSOR_BACKEND_SIM
        g_active = sensor_driver_sim();
#else
        g_active = sensor_driver_i2c();
#endif
    }
    return g_active;
}

void sensor_select(const sensor_driver_t *driver) {
    g_active = driver;
}
