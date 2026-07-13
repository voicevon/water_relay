#ifndef SMART_GATEWAY_H
#define SMART_GATEWAY_H

#include <Arduino.h>
#include <ESP32WifiMqttManager.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// 传感器数据源类型
enum class SensorSource {
    BLE,
    MQTT
};

// 智能网关配置结构体，解耦应用层与库文件
struct SmartGatewayConfig {
    // WiFi 与 MQTT 连接配置
    String wifiSsid;
    String wifiPassword;
    String mqttBroker;
    uint16_t mqttPort = 1883;
    String mqttUsername;
    String mqttPassword;
    String stationName; // 站点名称，用于动态主题拼接

    // BLE 扫描参数
    String targetBleName = "FengBLE";
    uint16_t bleCompanyIdVal = 0xFFFF;
    uint32_t bleScanDurationS = 5;

    // MQTT 协议参数
    String mqttSensorDataSub = "water/sensor/status";
    uint32_t mqttReconnectIntervalMs = 5000;
};

class SmartGateway {
public:
    // 事件回调函数指针定义
    typedef void (*SensorDataCallback)(uint16_t sensor1, uint16_t sensor2, uint16_t sensor3, uint8_t stateByte);
    typedef void (*ConfigDurationCallback)(int sensorId, float durationMinutes);
    typedef void (*ConfigPumpTimeCallback)(int sensorId, float pumpTimeSeconds);

    SmartGateway(SensorSource source = SensorSource::BLE);

    // 初始化网关（WiFi, MQTT, BLE）
    void begin(const SmartGatewayConfig& config);
    
    // 心跳维护函数，需在主循环中非阻塞调用
    void loop();

    // 注册事件回调
    void onSensorData(SensorDataCallback cb);
    void onConfigDuration(ConfigDurationCallback cb);
    void onConfigPumpTime(ConfigPumpTimeCallback cb);

    // 上报数据与状态接口
    bool publishStatus(const char* jsonPayload);
    bool publishSensorState(int sensorId, int stage, const char* remark, float duration, int pumpTime, uint32_t uptime, uint32_t stageStartSec);
    bool publishPhotoTake(const char* targetStation);

    // 获取当前站点名称
    const char* getStationName() const { return _stationName.c_str(); }
    
    // 查询蓝牙连接状态
    bool isBleConnected() const;

    // 查询网络状态
    NetworkState getNetworkState();

    // 更新扫描状态
    void setScanning(bool val) { _isScanning = val; }

private:
    static SmartGateway* _instance;
    SensorSource _sensorSource;
    
    SensorDataCallback _sensorCb = nullptr;
    ConfigDurationCallback _durationCb = nullptr;
    ConfigPumpTimeCallback _pumpTimeCb = nullptr;

    WiFiClient _espClient;
    ESP32WifiMqttManager _netManager;
    BLEScan* _pBLEScan = nullptr;

    String _stationName;
    String _mqttSensorDataSub;
    String _targetBleName;
    uint16_t _bleCompanyIdVal = 0xFFFF;
    uint32_t _bleScanDurationS = 5;

    uint8_t _lastSeqNum = -1;
    bool _bleConnected = false;
    bool _isScanning = false;
    uint32_t _lastBlePacketTime = 0;

    void setupBLE();
    
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static void scanCompleteCB(BLEScanResults results);
    void handleMqttMessage(char* topic, byte* payload, unsigned int length);

    class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
        void onResult(BLEAdvertisedDevice advertisedDevice) override;
    };
};

#endif // SMART_GATEWAY_H
