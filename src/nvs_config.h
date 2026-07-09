#pragma once

#include <Arduino.h>

/**
 * @brief 初始化 NVS 存储并加载所有配置项到内存缓存
 *        由 web_config_init() 调用
 */
void nvs_config_init();

/**
 * @brief 配置写入接口 — 供 REST POST handler 调用
 *        返回 true = 值已更新并写入 NVS；false = 无变化或非法值
 */
bool nvs_set_sta_ssid(const String& val);
bool nvs_set_sta_password(const String& val);
bool nvs_set_station_name(const String& val);
bool nvs_set_mqtt_broker(const String& val);
bool nvs_set_mqtt_port(int val);
bool nvs_set_mqtt_user(const String& val);
bool nvs_set_mqtt_pass(const String& val);

/**
 * @brief 3个通道的预计流量时间与水泵动作时间参数读写接口
 */
uint32_t get_expected_dur(int idx);
uint32_t get_pump_work_sec(int idx);
bool nvs_set_expected_dur(int idx, uint32_t val);
bool nvs_set_pump_work_sec(int idx, uint32_t val);

