#pragma once

#include <Arduino.h>

/**
 * @brief 初始化 Web Server 及其 AP 模式，加载 NVS 中的 WiFi 配置
 */
void web_config_init();

/**
 * @brief 在主循环中驱动 Web Server 客户端请求及 AP_STA 模式防退回
 */
void web_config_loop();

/**
 * @brief 从 NVS 获取配置 of STA Wi-Fi SSID
 */
String get_sta_ssid();

/**
 * @brief 从 NVS 获取配置 of STA Wi-Fi 密码
 */
String get_sta_password();

/**
 * @brief 更新指定物理通道的实时传感器数据，供网页 API 查询
 */
void web_config_update_sensor(int idx, uint16_t raw_val, uint16_t filtered, uint16_t baseline, uint16_t threshold, bool detected);

/**
 * @brief 更新继电器/水泵通道的实时状态，供网页 API 查询
 * @param idx 通道索引 (0-2)
 * @param active 采样是否正在运行
 * @param stage 当前状态机阶段 (0-9)
 * @param pump_on 水泵是否正在运转
 * @param on_count 本周期累计开启次数
 * @param detected 对应的传感器是否有水
 */
void web_config_update_relay(int idx, bool active, int stage, bool pump_on, int on_count, bool detected);

/**
 * @brief 从 NVS 获取配置的站点名称 (STATION_NAME)
 */
String get_station_name();

/**
 * @brief 从 NVS 获取配置的 MQTT Broker 地址
 */
String get_mqtt_broker();

/**
 * @brief 从 NVS 获取配置的 MQTT Broker 端口
 */
int get_mqtt_port();

/**
 * @brief 从 NVS 获取配置的 MQTT 登录用户名
 */
String get_mqtt_user();

/**
 * @brief 从 NVS 获取配置的 MQTT 登录密码
 */
String get_mqtt_pass();
