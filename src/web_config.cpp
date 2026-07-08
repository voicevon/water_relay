#include "web_config.h"
#include "nvs_config.h"
#include "data_cache.h"
#include "index_html.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>

// ============================================================
//  Web 服务器实例（服务层私有）
// ============================================================
static WebServer s_server(80);

// ============================================================
//  辅助函数：打印当前 WiFi 模式状态
// ============================================================
static void print_wifi_status(const char* label) {
    wifi_mode_t mode = WiFi.getMode();
    const char* mode_str = "UNKNOWN";
    if (mode == WIFI_OFF)         mode_str = "OFF";
    else if (mode == WIFI_STA)    mode_str = "STA";
    else if (mode == WIFI_AP)     mode_str = "AP";
    else if (mode == WIFI_AP_STA) mode_str = "AP_STA";

    Serial.printf("[%s] Current WiFi Mode: %s, AP IP: %s, Station IP: %s, Station Status: %d\n",
                  label, mode_str,
                  WiFi.softAPIP().toString().c_str(),
                  WiFi.localIP().toString().c_str(),
                  WiFi.status());
}

// ============================================================
//  REST API 处理函数
// ============================================================

#include <SmartGateway.h>

extern SmartGateway gateway;

// GET /api/data — 返回实时网关状态及传感器/继电器数据
static void handle_get_data() {
    String json = "{";
    // WiFi 连接状态
    json += "\"station\":\"" + get_station_name() + "\",";
    json += "\"ssid\":\""    + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("")) + "\",";
    json += "\"ip\":\""      + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\",";
    json += "\"rssi\":"      + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",";
    json += "\"wifi_connected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false") + ",";
    json += "\"mqtt_connected\":" + String((gateway.getNetworkState() == STATE_MQTT_CONNECTED) ? "true" : "false") + ",";


    // 3路传感器输入状态
    json += "\"sensors\":[";
    for (int i = 0; i < 3; i++) {
        const SensorCache& s = data_cache_get_sensor(i);
        json += "{";
        json += "\"raw\":"      + String(s.raw_val) + ",";
        json += "\"detected\":" + String(s.detected ? "true" : "false");
        json += "}";
        if (i < 2) json += ",";
    }
    json += "],";

    // 3路继电器/水泵通道状态
    json += "\"relays\":[";
    for (int i = 0; i < 3; i++) {
        const RelayCache& r = data_cache_get_relay(i);
        json += "{";
        json += "\"id\":"       + String(i + 1) + ",";
        json += "\"active\":"   + String(r.active ? "true" : "false") + ",";
        json += "\"stage\":"    + String(r.stage) + ",";
        json += "\"pump_on\":"  + String(r.pump_on ? "true" : "false") + ",";
        json += "\"on_count\":" + String(r.on_count) + ",";
        json += "\"detected\":" + String(r.detected ? "true" : "false");
        json += "}";
        if (i < 2) json += ",";
    }
    json += "]";
    json += "}";
    s_server.send(200, "application/json", json);
}

// GET /api/sysconfig — 返回系统与网络配置
static void handle_get_sysconfig() {
    String json = "{";
    json += "\"ssid\":\""      + get_sta_ssid() + "\",";
    json += "\"pass\":\""      + get_sta_password() + "\",";
    json += "\"name\":\""      + get_station_name() + "\",";
    json += "\"broker\":\""    + get_mqtt_broker() + "\",";
    json += "\"port\":"        + String(get_mqtt_port()) + ",";
    json += "\"user\":\""      + get_mqtt_user() + "\",";
    json += "\"pass_mqtt\":\"" + get_mqtt_pass() + "\"";
    json += "}";
    s_server.send(200, "application/json", json);
}

// POST /api/sysconfig — 保存系统配置到 NVS
static void handle_post_sysconfig() {
    bool changed = false;
    if (s_server.hasArg("ssid"))      changed |= nvs_set_sta_ssid(s_server.arg("ssid"));
    if (s_server.hasArg("password"))  changed |= nvs_set_sta_password(s_server.arg("password"));
    if (s_server.hasArg("name"))      changed |= nvs_set_station_name(s_server.arg("name"));
    if (s_server.hasArg("broker"))    changed |= nvs_set_mqtt_broker(s_server.arg("broker"));
    if (s_server.hasArg("port"))      changed |= nvs_set_mqtt_port(s_server.arg("port").toInt());
    if (s_server.hasArg("user"))      changed |= nvs_set_mqtt_user(s_server.arg("user"));
    if (s_server.hasArg("pass_mqtt")) changed |= nvs_set_mqtt_pass(s_server.arg("pass_mqtt"));

    if (changed) {
        Serial.println("[WebConfig] System & Network credentials updated to NVS.");
    }
    s_server.send(200, "text/plain", "OK");
}

// GET /api/scan — 扫描附近 WiFi 并按信号强度排序返回
static void handle_wifi_scan() {
    int n = WiFi.scanNetworks(false, false);
    String json = "{\"networks\":[";
    if (n > 0) {
        int indices[n];
        for (int i = 0; i < n; i++) indices[i] = i;
        for (int i = 0; i < n - 1; i++) {
            for (int j = i + 1; j < n; j++) {
                if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
                    int temp = indices[i];
                    indices[i] = indices[j];
                    indices[j] = temp;
                }
            }
        }
        int count = n < 15 ? n : 15;
        for (int i = 0; i < count; i++) {
            int idx = indices[i];
            json += "{";
            json += "\"ssid\":\"" + WiFi.SSID(idx) + "\",";
            json += "\"rssi\":"   + String(WiFi.RSSI(idx));
            json += "}";
            if (i < count - 1) json += ",";
        }
    }
    json += "]}";
    WiFi.scanDelete();
    s_server.send(200, "application/json", json);
}

// ============================================================
//  公共接口实现
// ============================================================

void web_config_init() {
    // 1. 初始化 NVS 配置层
    nvs_config_init();

    // 2. 启动 WiFi AP_STA 混合模式
    print_wifi_status("WebConfig BEFORE softAP");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(FACTORY_WIFI_AP_SSID, FACTORY_WIFI_AP_PASSWORD);
    print_wifi_status("WebConfig AFTER softAP");

    // 3. 挂载路由
    s_server.on("/", HTTP_GET, []() {
        s_server.send_P(200, "text/html", INDEX_HTML);
    });
    s_server.on("/api/data",      HTTP_GET,  handle_get_data);
    s_server.on("/api/sysconfig", HTTP_GET,  handle_get_sysconfig);
    s_server.on("/api/sysconfig", HTTP_POST, handle_post_sysconfig);
    s_server.on("/api/wifi",      HTTP_GET,  handle_get_sysconfig);   // 兼容旧接口
    s_server.on("/api/wifi",      HTTP_POST, handle_post_sysconfig);  // 兼容旧接口
    s_server.on("/api/scan",      HTTP_GET,  handle_wifi_scan);

    s_server.begin();
    Serial.println("[WebConfig] Built-in Web Server started on port 80");
}

void web_config_loop() {
    // 防回退机制：检测 WiFi 模式是否被外部库强制切回 STA
    if (WiFi.getMode() == WIFI_STA) {
        Serial.println("[WebConfig] WiFi mode was reverted to STA. Restoring AP_STA and restarting softAP...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(FACTORY_WIFI_AP_SSID, FACTORY_WIFI_AP_PASSWORD);
        print_wifi_status("WebConfig RESTORED AP_STA");
    }
    s_server.handleClient();
}

// ============================================================
//  web_config.h 中声明的缓存更新接口（转发至 data_cache）
// ============================================================
void web_config_update_sensor(int idx, uint16_t raw_val, uint16_t filtered,
                               uint16_t baseline, uint16_t threshold, bool detected) {
    data_cache_update_sensor(idx, raw_val, filtered, baseline, threshold, detected);
}

void web_config_update_relay(int idx, bool active, int stage,
                              bool pump_on, int on_count, bool detected) {
    data_cache_update_relay(idx, active, stage, pump_on, on_count, detected);
}
