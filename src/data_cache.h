#pragma once

#include <Arduino.h>

// ============================================================
//  4路传感器输入数据缓存结构
// ============================================================
struct SensorCache {
    uint16_t raw_val;    // 原始值
    uint16_t filtered;   // 滤波后值
    uint16_t baseline;   // 基准值
    uint16_t threshold;  // 触发阈值
    bool     detected;   // 是否有水
};

// ============================================================
//  3路继电器/水泵状态缓存结构
// ============================================================
struct RelayCache {
    bool active;    // 采样是否运行
    int  stage;     // 状态机当前阶段 (0-9)
    bool pump_on;   // 水泵是否开启
    int  on_count;  // 本周期累计开启次数
    bool detected;  // 对应传感器是否有水
};

/**
 * @brief 更新传感器数据缓存（供 web_config.cpp 中 /api/data handler 使用）
 */
void data_cache_update_sensor(int idx, uint16_t raw_val, uint16_t filtered,
                               uint16_t baseline, uint16_t threshold, bool detected);

/**
 * @brief 更新继电器/水泵状态缓存
 */
void data_cache_update_relay(int idx, bool active, int stage,
                              bool pump_on, int on_count, bool detected);

/**
 * @brief 获取传感器数据缓存引用（供 REST handler 读取）
 */
const SensorCache& data_cache_get_sensor(int idx);

/**
 * @brief 获取继电器数据缓存引用（供 REST handler 读取）
 */
const RelayCache& data_cache_get_relay(int idx);
