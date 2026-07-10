#include <Arduino.h>
#include <ArduinoJson.h>
#include <SmartGateway.h>

#include "config.h"
#include "SamplingController.h"
#include "web_config.h"

// ============================================================================
//  GPIO 直接控制引脚定义
// ============================================================================

// 10步阶段指示灯引脚映射数组，方便管理与循环控制
const int STAGE_LED_PINS[10] = {
    LED_PIN_STAGE_0, LED_PIN_STAGE_1, LED_PIN_STAGE_2, LED_PIN_STAGE_3, LED_PIN_STAGE_4,
    LED_PIN_STAGE_5, LED_PIN_STAGE_6, LED_PIN_STAGE_7, LED_PIN_STAGE_8, LED_PIN_STAGE_9
};

// ============================================================================
//  全局对象与变量定义
// ============================================================================

// 3路传感器输入数据及有无水状态缓存 (数据计算已移至 sensor 节点)
uint16_t g_sensor_values[3] = {0, 0, 0};
bool g_sensor_states[3] = {false, false, false};

// 3路水泵传感器对应的传感器物理通道映射关系
static const int SENSOR_MAP[3] = { SENSOR_MAP_PUMP_0, SENSOR_MAP_PUMP_1, SENSOR_MAP_PUMP_2 };

// 3路本地继电器与指示灯物理状态机
SamplingController sensors[3] = {
    SamplingController(1, RELAY_PIN_CH0, LED_PIN_CH0, DEFAULT_EXPECTED_DUR_CH0),
    SamplingController(2, RELAY_PIN_CH1, LED_PIN_CH1, DEFAULT_EXPECTED_DUR_CH1),
    SamplingController(3, RELAY_PIN_CH2, LED_PIN_CH2, DEFAULT_EXPECTED_DUR_CH2)
};

// 智能网关实例
SmartGateway gateway(SensorSource::BLE);

uint32_t lastStatusPublish = 0;
int lastStages[3] = {-1, -1, -1};

// ============================================================================
//  网关事件回调处理
// ============================================================================

// 1. 接收到网关传感器数据 (含节点端判定好的状态位)
void handleSensorData(uint16_t sensor1, uint16_t sensor2, uint16_t sensor3, uint8_t stateByte) {
    g_sensor_values[0] = sensor1;
    g_sensor_values[1] = sensor2;
    g_sensor_values[2] = sensor3;

    g_sensor_states[0] = (stateByte & 0x01) != 0;
    g_sensor_states[1] = (stateByte & 0x02) != 0;
    g_sensor_states[2] = (stateByte & 0x04) != 0;
}

// 2. 接收到 MQTT 配置更改预期总时长
void handleConfigDuration(int sensorId, float durationMinutes) {
    for (int i = 0; i < 3; i++) {
        if (sensors[i].id == sensorId) {
            uint32_t newDur = (uint32_t)(durationMinutes * 60.0f);
            sensors[i].updateParameters(newDur, sensors[i].pumpWorkTime);
            nvs_set_expected_dur(i, newDur);
            Serial.printf("[CONFIG] Sensor %d duration updated to %d sec via MQTT and saved to NVS.\n", sensorId, newDur);
            break;
        }
    }
}

// 3. 接收到 MQTT 配置更改单次泵开启时长
void handleConfigPumpTime(int sensorId, float pumpTimeSeconds) {
    for (int i = 0; i < 3; i++) {
        if (sensors[i].id == sensorId) {
            uint32_t newPump = (uint32_t)pumpTimeSeconds;
            sensors[i].updateParameters(sensors[i].expectedDuration, newPump);
            nvs_set_pump_work_sec(i, newPump);
            Serial.printf("[CONFIG] Sensor %d pump work time updated to %d sec via MQTT and saved to NVS.\n", sensorId, newPump);
            break;
        }
    }
}

// 4. 定时发布状态数据 (JSON 格式)
void publishStatus() {
    if (millis() - lastStatusPublish >= 30000) {
        lastStatusPublish = millis();
        
        StaticJsonDocument<512> doc;
        doc["uptime_seconds"] = millis() / 1000;
        doc["ble_connected"] = gateway.isBleConnected();
        
        JsonArray sensorsArr = doc.createNestedArray("sensors");
        for (int i = 0; i < 3; i++) {
            JsonObject chObj = sensorsArr.createNestedObject();
            chObj["id"] = sensors[i].id;
            chObj["active"] = sensors[i].active;
            chObj["stage"] = sensors[i].currentStage;
            chObj["pump_on"] = sensors[i].lastPumpState;
            chObj["on_count"] = sensors[i].onCount;
            chObj["detected"] = sensors[i].isDetected;
        }
        
        String jsonPayload;
        serializeJson(doc, jsonPayload);
        gateway.publishStatus(jsonPayload.c_str());
    }
}

// ============================================================================
//  Arduino 运行入口
// ============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("Starting water_brain...");

    // 初始化 3 路水泵继电器引脚与 3 路传感器状态指示灯引脚
    for (int i = 0; i < 3; i++) {
        pinMode(sensors[i].relayPin, OUTPUT);
        digitalWrite(sensors[i].relayPin, LOW); // 默认关闭水泵
        pinMode(sensors[i].ledPin, OUTPUT);
        digitalWrite(sensors[i].ledPin, LOW);   // 默认关闭传感器指示灯
    }

    // 初始化 10 步状态机阶段指示灯引脚
    for (int i = 0; i < 10; i++) {
        pinMode(STAGE_LED_PINS[i], OUTPUT);
        digitalWrite(STAGE_LED_PINS[i], LOW);   // 默认关闭阶段指示灯
    }

    // 注册网关事件回调函数
    gateway.onSensorData(handleSensorData);
    gateway.onConfigDuration(handleConfigDuration);
    gateway.onConfigPumpTime(handleConfigPumpTime);

    // 启动 Web 服务器和 AP 模式 (动态从 NVS 中加载 STA WiFi 凭证)
    web_config_init();

    // 从 NVS 中加载并更新各传感器的运行参数
    for (int i = 0; i < 3; i++) {
        uint32_t dur = get_expected_dur(i);
        uint32_t pump = get_pump_work_sec(i);
        sensors[i].updateParameters(dur, pump);
        Serial.printf("[MAIN] Sensor %d parameters loaded from NVS: expectedDuration = %u s, pumpWorkTime = %u s\n",
                      sensors[i].id, dur, pump);
    }

    // 启动网关网络通信
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW); // 默认关闭状态指示灯

    SmartGatewayConfig config;
    config.wifiSsid = get_sta_ssid();
    config.wifiPassword = get_sta_password();
    config.mqttBroker = get_mqtt_broker();
    config.mqttPort = get_mqtt_port();
    config.mqttUsername = get_mqtt_user();
    config.mqttPassword = get_mqtt_pass();
    config.stationName = get_station_name();
    config.targetBleName = TARGET_BLE_NAME;
    config.bleCompanyIdVal = BLE_COMPANY_ID_VAL;
    config.bleScanDurationS = BLE_SCAN_DURATION_S;
    config.mqttSensorDataSub = MQTT_SENSOR_DATA_SUB;
    config.mqttReconnectIntervalMs = MQTT_RECONNECT_INTERVAL_MS;

    gateway.begin(config);
}

void loop() {
    // 维持网关心跳（自动处理 WiFi/MQTT 连接及非阻塞 BLE 扫描）
    gateway.loop();

    // 驱动 Web Server 客户端请求
    web_config_loop();

    // 维持状态指示灯 LED 闪烁逻辑 (板载 LED, GPIO 2)
    unsigned long now = millis();
    NetworkState netState = gateway.getNetworkState();
    unsigned long period = 0;

    if (netState == STATE_WIFI_CONNECTED || netState == STATE_MQTT_CONNECTING) {
        period = 2000; // WiFi 已连接，MQTT 未连接：2 秒周期
    } else if (netState == STATE_MQTT_CONNECTED) {
        period = 1000; // WiFi 与 MQTT 均连接：1 秒周期
    } else {
        period = 0;    // 未连接 WiFi，常闭
    }

    if (period == 0) {
        digitalWrite(STATUS_LED_PIN, LOW);
    } else {
        if ((now % period) < (period / 2)) {
            digitalWrite(STATUS_LED_PIN, HIGH);
        } else {
            digitalWrite(STATUS_LED_PIN, LOW);
        }
    }

    // 映射对应传感数据到继电器状态机，并计算结果
    bool sensorPumps[3] = {false, false, false};
    
    // 传感器 0：对应 SENSOR_MAP_PUMP_0 的传感器输入
    sensorPumps[0] = sensors[0].update(g_sensor_states[SENSOR_MAP_PUMP_0]);
    // 传感器 1：对应 SENSOR_MAP_PUMP_1 的传感器输入
    sensorPumps[1] = sensors[1].update(g_sensor_states[SENSOR_MAP_PUMP_1]);
    // 传感器 2：对应 SENSOR_MAP_PUMP_2 的传感器输入
    sensorPumps[2] = sensors[2].update(g_sensor_states[SENSOR_MAP_PUMP_2]);

    // 1. 直接控制 3 路水泵继电器引脚与 3 路传感器指示 LED
    for (int i = 0; i < 3; i++) {
        digitalWrite(sensors[i].relayPin, sensorPumps[i] ? HIGH : LOW);
        digitalWrite(sensors[i].ledPin, sensors[i].isDetected ? HIGH : LOW);
    }

    // 2. 确定当前正在运行采样的传感器的 Stage
    int activeStage = 0;
    if (sensors[0].active) activeStage = sensors[0].currentStage;
    else if (sensors[1].active) activeStage = sensors[1].currentStage;
    else if (sensors[2].active) activeStage = sensors[2].currentStage;
    else activeStage = sensors[0].currentStage; // 缺省空闲阶段

    // 3. 更新 10 步状态机指示灯，仅点亮当前 Stage 对应的指示灯，熄灭其他 9 个
    for (int stage = 0; stage < 10; stage++) {
        digitalWrite(STAGE_LED_PINS[stage], (stage == activeStage) ? HIGH : LOW);
    }

    // 定义流程状态中文描述模板
    static const char* STAGE_REMARKS[10] = {
        "空闲等待",
        "待稳准备",
        "正在取头样",
        "第1次延时待命 (waiting_2)",
        "正在取中样",
        "第2次延时待命 (waiting_3)",
        "正在取尾样",
        "采样完成等待排空 (waiting_4)",
        "正在进行管道排空",
        "采样流程结束"
    };

    // 4. 检测状态跳转并发送 MQTT 实时保留状态
    for (int i = 0; i < 3; i++) {
        if (sensors[i].currentStage != lastStages[i]) {
            lastStages[i] = sensors[i].currentStage;
            int stg = sensors[i].currentStage;
            const char* remark = (stg >= 0 && stg < 10) ? STAGE_REMARKS[stg] : "未知状态";
            gateway.publishSensorState(
                sensors[i].id, 
                stg, 
                remark, 
                sensors[i].expectedDuration / 60.0f, 
                sensors[i].pumpWorkTime, 
                now / 1000, 
                sensors[i].stageStartTime
            );
        }
    }

    // 4b. 检测有无水状态变化并执行本地串口日志打印
    static bool lastSensorStatesInLoop[3] = {false, false, false};
    for (int i = 0; i < 3; i++) {
        int sensorCh = SENSOR_MAP[i];
        if (g_sensor_states[sensorCh] != lastSensorStatesInLoop[sensorCh]) {
            lastSensorStatesInLoop[sensorCh] = g_sensor_states[sensorCh];
            Serial.printf("[APP SENSOR] Sensor %d mapped sensor state changed to %s\n", 
                          i, g_sensor_states[sensorCh] ? "HAS_WATER" : "NO_WATER");
        }
    }

    // 4c. 检测 MQTT 网络重新连接，主动同步当前活跃状态以刷新 Broker 的保留消息
    static NetworkState lastMqttState = STATE_DISCONNECTED;
    NetworkState currentMqttState = gateway.getNetworkState();
    if (currentMqttState == STATE_MQTT_CONNECTED && lastMqttState != STATE_MQTT_CONNECTED) {
        Serial.println("[MQTT RECONNECT] Restoring active sensors' Retain states...");
        bool anyActive = false;
        for (int i = 0; i < 3; i++) {
            if (sensors[i].active) {
                anyActive = true;
                int stg = sensors[i].currentStage;
                const char* remark = (stg >= 0 && stg < 10) ? STAGE_REMARKS[stg] : "未知状态";
                gateway.publishSensorState(
                    sensors[i].id, 
                    stg, 
                    remark, 
                    sensors[i].expectedDuration / 60.0f, 
                    sensors[i].pumpWorkTime, 
                    now / 1000, 
                    sensors[i].stageStartTime
                );
            }
        }
        if (!anyActive) {
            int stg = sensors[0].currentStage;
            const char* remark = (stg >= 0 && stg < 10) ? STAGE_REMARKS[stg] : "未知状态";
            gateway.publishSensorState(
                sensors[0].id, 
                stg, 
                remark, 
                sensors[0].expectedDuration / 60.0f, 
                sensors[0].pumpWorkTime, 
                now / 1000, 
                sensors[0].stageStartTime
            );
        }
    }
    lastMqttState = currentMqttState;

    // 5. 更新 Web 监控缓存数据
    for (int i = 0; i < 3; i++) {
        // 仅保留 Raw 电容值及开关水状态
        web_config_update_sensor(i, g_sensor_values[i], 0, 0, 0, g_sensor_states[i]);
    }
    for (int i = 0; i < 3; i++) {
        int sensorCh = SENSOR_MAP[i];
        web_config_update_relay(i, sensors[i].active, sensors[i].currentStage, sensors[i].lastPumpState, sensors[i].onCount, g_sensor_states[sensorCh]);
    }

    // 6. 定期上报系统状态 JSON (30 秒)
    publishStatus();

    delay(10);
}
