#include "data_cache.h"

// 4路传感器缓存（内部私有）
static SensorCache s_sensor_cache[4] = {0};
// 3路继电器/水泵缓存（内部私有）
static RelayCache  s_relay_cache[3]  = {0};

void data_cache_update_sensor(int idx, uint16_t raw_val, uint16_t filtered,
                               uint16_t baseline, uint16_t threshold, bool detected) {
    if (idx < 0 || idx >= 4) return;
    s_sensor_cache[idx].raw_val   = raw_val;
    s_sensor_cache[idx].filtered  = filtered;
    s_sensor_cache[idx].baseline  = baseline;
    s_sensor_cache[idx].threshold = threshold;
    s_sensor_cache[idx].detected  = detected;
}

void data_cache_update_relay(int idx, bool active, int stage,
                              bool pump_on, int on_count, bool detected) {
    if (idx < 0 || idx >= 3) return;
    s_relay_cache[idx].active    = active;
    s_relay_cache[idx].stage     = stage;
    s_relay_cache[idx].pump_on   = pump_on;
    s_relay_cache[idx].on_count  = on_count;
    s_relay_cache[idx].detected  = detected;
}

const SensorCache& data_cache_get_sensor(int idx) {
    if (idx < 0 || idx >= 4) idx = 0;
    return s_sensor_cache[idx];
}

const RelayCache& data_cache_get_relay(int idx) {
    if (idx < 0 || idx >= 3) idx = 0;
    return s_relay_cache[idx];
}
