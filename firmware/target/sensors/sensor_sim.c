#include "sensor.h"

/*
 * Direct synthetic backend: no bus and no register model. Useful when the point
 * is to exercise the protocol and the alert rules without any device model.
 */

static uint32_t g_index;

static int sim_init(void) {
    g_index = 0u;
    return 0;
}

static int sim_read(sensor_reading_t *out) {
    const uint32_t step = g_index++;

    if (out == 0) {
        return -1;
    }
    out->temperature_centi_c = 6200 + (int32_t)(step % 24u) * 90;
    out->humidity_centi_pct = 4860;
    out->current_ma = (step % 15u == 0u) ? 3100 : 920;
    out->vibration_rms_mg = (step % 18u == 0u) ? 880 : 120;
    out->status = (step % 23u == 0u) ? EHM_SENSOR_STATUS_FAULT : EHM_SENSOR_STATUS_OK;
    return 0;
}

static const sensor_driver_t g_sim_driver = {
    "synthetic",
    sim_init,
    sim_read,
};

const sensor_driver_t *sensor_driver_sim(void) {
    return &g_sim_driver;
}
