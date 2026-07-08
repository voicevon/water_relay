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

// 4通道传感器输入数据及有无水状态缓存 (数据计算已移至 sensor 节点)
uint16_t g_sensor_values[4] = {0, 0, 0, 0};
bool g_sensor_states[4] = {false, false, false, false};

// 3通道本地继电器与指示灯物理状态机
SamplingController channels[3] = {
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
void handleSensorData(uint16_t sensor1, uint16_t sensor2, uint16_t sensor3, uint16_t sensor4, uint8_t stateByte) {
    g_sensor_values[0] = sensor1;
    g_sensor_values[1] = sensor2;
    g_sensor_values[2] = sensor3;
    g_sensor_values[3] = sensor4;

    bool prevStates[3];
    for (int i = 0; i < 3; i++) {
        int sensorCh = (i == 0) ? SENSOR_MAP_PUMP_0 : ((i == 1) ? SENSOR_MAP_PUMP_1 : SENSOR_MAP_PUMP_2);
        prevStates[i] = g_sensor_states[sensorCh];
    }

    g_sensor_states[0] = (stateByte & 0x01) != 0;
    g_sensor_states[1] = (stateByte & 0x02) != 0;
    g_sensor_states[2] = (stateByte & 0x04) != 0;
    g_sensor_states[3] = (stateByte & 0x08) != 0;

    Serial.printf("[APP GATEWAY] RX: #1=%u #2=%u #3=%u #4=%u, States: #1=%d #2=%d #3=%d #4=%d\n",
                  sensor1, sensor2, sensor3, sensor4,
                  g_sensor_states[0], g_sensor_states[1], g_sensor_states[2], g_sensor_states[3]);

    // 检测有无水状态变化以发布 MQTT 日志
    for (int i = 0; i < 3; i++) {
        int sensorCh = (i == 0) ? SENSOR_MAP_PUMP_0 : ((i == 1) ? SENSOR_MAP_PUMP_1 : SENSOR_MAP_PUMP_2);
        if (g_sensor_states[sensorCh] != prevStates[i]) {
            char logMsg[64];
            snprintf(logMsg, sizeof(logMsg), "通道%d 传感器检测到%s", i, g_sensor_states[sensorCh] ? "有水" : "无水");
            gateway.publishLog(channels[i].id, logMsg);
            Serial.printf("[APP SENSOR] Channel %d mapped sensor state changed to %s\n", i, g_sensor_states[sensorCh] ? "HAS_WATER" : "NO_WATER");
        }
    }
}

// 2. 接收到 MQTT 配置更改预期总时长
void handleConfigDuration(int channelId, float durationMinutes) {
    for (int i = 0; i < 3; i++) {
        if (channels[i].id == channelId) {
            uint32_t newDur = (uint32_t)(durationMinutes * 60.0f);
            channels[i].updateParameters(newDur, channels[i].pumpWorkTime);
            Serial.printf("[CONFIG] Channel %d duration updated to %d sec via MQTT.\n", channelId, newDur);
            break;
        }
    }
}

// 3. 接收到 MQTT 配置更改单次泵开启时长
void handleConfigPumpTime(int channelId, float pumpTimeSeconds) {
    for (int i = 0; i < 3; i++) {
        if (channels[i].id == channelId) {
            uint32_t newPump = (uint32_t)pumpTimeSeconds;
            channels[i].updateParameters(channels[i].expectedDuration, newPump);
            Serial.printf("[CONFIG] Channel %d pump work time updated to %d sec via MQTT.\n", channelId, newPump);
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
        
        JsonArray channelsArr = doc.createNestedArray("channels");
        for (int i = 0; i < 3; i++) {
            JsonObject chObj = channelsArr.createNestedObject();
            chObj["id"] = channels[i].id;
            chObj["active"] = channels[i].active;
            chObj["stage"] = channels[i].currentStage;
            chObj["pump_on"] = channels[i].lastPumpState;
            chObj["on_count"] = channels[i].onCount;
            chObj["detected"] = channels[i].isDetected;
            
            // 获取对应的传感器物理通道索引并填充值
            int sensorCh = (i == 0) ? SENSOR_MAP_PUMP_0 : ((i == 1) ? SENSOR_MAP_PUMP_1 : SENSOR_MAP_PUMP_2);
            chObj["raw"] = g_sensor_values[sensorCh]; 
            chObj["filtered"] = g_sensor_values[sensorCh];
            chObj["baseline"] = g_sensor_values[sensorCh];
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
        pinMode(channels[i].relayPin, OUTPUT);
        digitalWrite(channels[i].relayPin, LOW); // 默认关闭水泵
        pinMode(channels[i].ledPin, OUTPUT);
        digitalWrite(channels[i].ledPin, LOW);   // 默认关闭通道指示灯
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

    // 启动网关网络通信
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW); // 默认关闭状态指示灯
    gateway.begin();
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
    bool channelPumps[3] = {false, false, false};
    
    // 通道 0：对应 SENSOR_MAP_PUMP_0 的传感器输入
    channelPumps[0] = channels[0].update(g_sensor_states[SENSOR_MAP_PUMP_0]);
    // 通道 1：对应 SENSOR_MAP_PUMP_1 的传感器输入
    channelPumps[1] = channels[1].update(g_sensor_states[SENSOR_MAP_PUMP_1]);
    // 通道 2：对应 SENSOR_MAP_PUMP_2 的传感器输入
    channelPumps[2] = channels[2].update(g_sensor_states[SENSOR_MAP_PUMP_2]);

    // 1. 直接控制 3 路水泵继电器引脚与 3 路传感器指示 LED
    for (int i = 0; i < 3; i++) {
        digitalWrite(channels[i].relayPin, channelPumps[i] ? HIGH : LOW);
        digitalWrite(channels[i].ledPin, channels[i].isDetected ? HIGH : LOW);
    }

    // 2. 确定当前正在运行采样的通道的 Stage
    int activeStage = 0;
    if (channels[0].active) activeStage = channels[0].currentStage;
    else if (channels[1].active) activeStage = channels[1].currentStage;
    else if (channels[2].active) activeStage = channels[2].currentStage;
    else activeStage = channels[0].currentStage; // 缺省空闲阶段

    // 3. 更新 10 步状态机指示灯，仅点亮当前 Stage 对应的指示灯，熄灭其他 9 个
    for (int stage = 0; stage < 10; stage++) {
        digitalWrite(STAGE_LED_PINS[stage], (stage == activeStage) ? HIGH : LOW);
    }

    // 4. 检测状态跳转并发送 MQTT 诊断日志
    for (int i = 0; i < 3; i++) {
        if (channels[i].currentStage != lastStages[i]) {
            lastStages[i] = channels[i].currentStage;
            char logMsg[64];
            snprintf(logMsg, sizeof(logMsg), "通道%d 状态跳转 -> Stage %d", i, channels[i].currentStage);
            gateway.publishLog(channels[i].id, logMsg);
        }
    }

    // 5. 更新 Web 监控缓存数据
    for (int i = 0; i < 4; i++) {
        // 由于 Sensor 类已被删除，我们将滤波/基准值/阈值全部简化为 0，仅保留 Raw 电容值及开关水状态
        web_config_update_sensor(i, g_sensor_values[i], 0, 0, 0, g_sensor_states[i]);
    }
    for (int i = 0; i < 3; i++) {
        int sensorCh = (i == 0) ? SENSOR_MAP_PUMP_0 : ((i == 1) ? SENSOR_MAP_PUMP_1 : SENSOR_MAP_PUMP_2);
        web_config_update_relay(i, channels[i].active, channels[i].currentStage, channels[i].lastPumpState, channels[i].onCount, g_sensor_states[sensorCh]);
    }

    // 6. 定期上报系统状态 JSON (30 秒)
    publishStatus();

    delay(10);
}
