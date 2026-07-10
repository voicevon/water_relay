#include "nvs_config.h"
#include "config.h"
#include <Preferences.h>

// ============================================================
//  NVS 存储实例（内部私有）
// ============================================================
static Preferences s_prefs;

// NVS 命名空间与键名常量，集中管理避免拼写错误
static const char NVS_NAMESPACE[]     = "sensor_map";
static const char NVS_KEY_SSID[]      = "sta_ssid";
static const char NVS_KEY_PASS[]      = "sta_pass";
static const char NVS_KEY_NAME[]      = "sta_name";
static const char NVS_KEY_BROKER[]    = "mqtt_broker";
static const char NVS_KEY_PORT[]      = "mqtt_port";
static const char NVS_KEY_MQTT_USER[] = "mqtt_user";
static const char NVS_KEY_MQTT_PASS[] = "mqtt_pass";

static const char NVS_KEY_DUR_0[]     = "dur_0";
static const char NVS_KEY_PUMP_0[]    = "pump_0";
static const char NVS_KEY_DUR_1[]     = "dur_1";
static const char NVS_KEY_PUMP_1[]    = "pump_1";
static const char NVS_KEY_DUR_2[]     = "dur_2";
static const char NVS_KEY_PUMP_2[]    = "pump_2";

// ============================================================
//  配置项内存缓存（内部私有）
// ============================================================
static String s_sta_ssid     = "";
static String s_sta_password = "";
static String s_sta_name     = "";
static String s_mqtt_broker  = "";
static int    s_mqtt_port    = 1883;
static String s_mqtt_user    = "";
static String s_mqtt_pass    = "";

static uint32_t s_expected_dur[3]  = { DEFAULT_EXPECTED_DUR_CH0, DEFAULT_EXPECTED_DUR_CH1, DEFAULT_EXPECTED_DUR_CH2 };
static uint32_t s_pump_work_sec[3] = { DEFAULT_PUMP_WORK_SEC, DEFAULT_PUMP_WORK_SEC, DEFAULT_PUMP_WORK_SEC };

// ============================================================
//  NVS 初始化
// ============================================================
void nvs_config_init() {
    s_prefs.begin(NVS_NAMESPACE, false);

    s_sta_ssid     = s_prefs.getString(NVS_KEY_SSID,      FACTORY_WIFI_SSID);
    s_sta_password = s_prefs.getString(NVS_KEY_PASS,      FACTORY_WIFI_PASSWORD);
    s_sta_name     = s_prefs.getString(NVS_KEY_NAME,      FACTORY_DEVICE_NAME);
    s_mqtt_broker  = s_prefs.getString(NVS_KEY_BROKER,    FACTORY_MQTT_BROKER);
    s_mqtt_port    = s_prefs.getInt(NVS_KEY_PORT,         FACTORY_MQTT_PORT);
    s_mqtt_user    = s_prefs.getString(NVS_KEY_MQTT_USER, FACTORY_MQTT_USERNAME);
    s_mqtt_pass    = s_prefs.getString(NVS_KEY_MQTT_PASS, FACTORY_MQTT_PASSWORD);

    s_expected_dur[0] = s_prefs.getUInt(NVS_KEY_DUR_0,   DEFAULT_EXPECTED_DUR_CH0);
    s_expected_dur[1] = s_prefs.getUInt(NVS_KEY_DUR_1,   DEFAULT_EXPECTED_DUR_CH1);
    s_expected_dur[2] = s_prefs.getUInt(NVS_KEY_DUR_2,   DEFAULT_EXPECTED_DUR_CH2);

    s_pump_work_sec[0] = s_prefs.getUInt(NVS_KEY_PUMP_0, DEFAULT_PUMP_WORK_SEC);
    s_pump_work_sec[1] = s_prefs.getUInt(NVS_KEY_PUMP_1, DEFAULT_PUMP_WORK_SEC);
    s_pump_work_sec[2] = s_prefs.getUInt(NVS_KEY_PUMP_2, DEFAULT_PUMP_WORK_SEC);

    Serial.printf("[NvsConfig] Loaded WiFi STA SSID: %s\n", s_sta_ssid.c_str());
    Serial.printf("[NvsConfig] Station Name: %s, MQTT Broker: %s:%d\n",
                  s_sta_name.c_str(), s_mqtt_broker.c_str(), s_mqtt_port);
    Serial.printf("[NvsConfig] Sensors Dur: %u, %u, %u | Pump: %u, %u, %u\n",
                  s_expected_dur[0], s_expected_dur[1], s_expected_dur[2],
                  s_pump_work_sec[0], s_pump_work_sec[1], s_pump_work_sec[2]);
}

// ============================================================
//  Getter 实现（函数声明在 web_config.h）
// ============================================================
String get_sta_ssid()      { return s_sta_ssid; }
String get_sta_password()  { return s_sta_password; }
String get_station_name()  { return s_sta_name; }
String get_mqtt_broker()   { return s_mqtt_broker; }
int    get_mqtt_port()     { return s_mqtt_port; }
String get_mqtt_user()     { return s_mqtt_user; }
String get_mqtt_pass()     { return s_mqtt_pass; }

// ============================================================
//  Setter 实现（含变化检测 + NVS 写入）
// ============================================================
bool nvs_set_sta_ssid(const String& val) {
    if (val.length() == 0 || val == s_sta_ssid) return false;
    s_sta_ssid = val;
    s_prefs.putString(NVS_KEY_SSID, val);
    return true;
}

bool nvs_set_sta_password(const String& val) {
    if (val == s_sta_password) return false;
    s_sta_password = val;
    s_prefs.putString(NVS_KEY_PASS, val);
    return true;
}

bool nvs_set_station_name(const String& val) {
    if (val.length() == 0 || val == s_sta_name) return false;
    s_sta_name = val;
    s_prefs.putString(NVS_KEY_NAME, val);
    return true;
}

bool nvs_set_mqtt_broker(const String& val) {
    if (val.length() == 0 || val == s_mqtt_broker) return false;
    s_mqtt_broker = val;
    s_prefs.putString(NVS_KEY_BROKER, val);
    return true;
}

bool nvs_set_mqtt_port(int val) {
    if (val <= 0 || val == s_mqtt_port) return false;
    s_mqtt_port = val;
    s_prefs.putInt(NVS_KEY_PORT, val);
    return true;
}

bool nvs_set_mqtt_user(const String& val) {
    if (val == s_mqtt_user) return false;
    s_mqtt_user = val;
    s_prefs.putString(NVS_KEY_MQTT_USER, val);
    return true;
}

bool nvs_set_mqtt_pass(const String& val) {
    if (val == s_mqtt_pass) return false;
    s_mqtt_pass = val;
    s_prefs.putString(NVS_KEY_MQTT_PASS, val);
    return true;
}

uint32_t get_expected_dur(int idx) {
    if (idx < 0 || idx >= 3) return 0;
    return s_expected_dur[idx];
}

uint32_t get_pump_work_sec(int idx) {
    if (idx < 0 || idx >= 3) return 0;
    return s_pump_work_sec[idx];
}

bool nvs_set_expected_dur(int idx, uint32_t val) {
    if (idx < 0 || idx >= 3 || val == s_expected_dur[idx]) return false;
    s_expected_dur[idx] = val;
    const char* key = (idx == 0) ? NVS_KEY_DUR_0 : ((idx == 1) ? NVS_KEY_DUR_1 : NVS_KEY_DUR_2);
    s_prefs.putUInt(key, val);
    return true;
}

bool nvs_set_pump_work_sec(int idx, uint32_t val) {
    if (idx < 0 || idx >= 3 || val == s_pump_work_sec[idx]) return false;
    s_pump_work_sec[idx] = val;
    const char* key = (idx == 0) ? NVS_KEY_PUMP_0 : ((idx == 1) ? NVS_KEY_PUMP_1 : NVS_KEY_PUMP_2);
    s_prefs.putUInt(key, val);
    return true;
}
