#include "SmartGateway.h"
#include <ArduinoJson.h>

// 初始化单例指针
SmartGateway* SmartGateway::_instance = nullptr;

// BLE 异步扫描结束后的回调
void SmartGateway::scanCompleteCB(BLEScanResults results) {
    BLEDevice::getScan()->clearResults(); // 清理扫描结果，释放内存
    if (_instance) {
        _instance->_isScanning = false;
    }
}

SmartGateway::SmartGateway(SensorSource source) : _netManager(_espClient), _sensorSource(source) {
    _instance = this;
    _lastSeqNum = -1;
    _bleConnected = false;
    _isScanning = false;
    _lastBlePacketTime = 0;
}

void SmartGateway::begin(const SmartGatewayConfig& config) {
    _stationName = config.stationName;
    _mqttSensorDataSub = config.mqttSensorDataSub;
    _targetBleName = config.targetBleName;
    _bleCompanyIdVal = config.bleCompanyIdVal;
    _bleScanDurationS = config.bleScanDurationS;

    NetworkConfig netConfig;
    static String s_ssid;
    static String s_pass;
    static String s_broker;
    static String s_user;
    static String s_pass_mqtt;

    s_ssid = config.wifiSsid;
    s_pass = config.wifiPassword;
    s_broker = config.mqttBroker;
    s_user = config.mqttUsername;
    s_pass_mqtt = config.mqttPassword;

    netConfig.wifiSsid = s_ssid.c_str();
    netConfig.wifiPassword = s_pass.c_str();
    netConfig.mqttBroker = s_broker.c_str();
    netConfig.mqttPort = config.mqttPort;
    netConfig.mqttUsername = s_user.c_str();
    netConfig.mqttPassword = s_pass_mqtt.c_str();
    netConfig.clientIdPrefix = "water_brain_client";
    netConfig.wifiReconnectIntervalMs = 20000;
    netConfig.mqttReconnectIntervalMs = config.mqttReconnectIntervalMs;

    _netManager.begin(netConfig);

    // 订阅主题注册回调
    _netManager.onStateChange([](NetworkState state) {
        if (state == STATE_MQTT_CONNECTED) {
            if (_instance) {
                // 动态构建订阅主题
                String durationSub = "water/" + _instance->_stationName + "/config/duration/+";
                String pumpTimeSub = "water/" + _instance->_stationName + "/config/pump_time/+";
                _instance->_netManager.subscribe(durationSub.c_str());
                _instance->_netManager.subscribe(pumpTimeSub.c_str());
                if (_instance->_sensorSource == SensorSource::MQTT) {
                    _instance->_netManager.subscribe(_instance->_mqttSensorDataSub.c_str());
                }
            }
        }
    });

    _netManager.setMqttCallback(mqttCallback);

    if (_sensorSource == SensorSource::BLE) {
        setupBLE();
    }
}

void SmartGateway::loop() {
    _netManager.loop();

    if (_sensorSource == SensorSource::BLE) {
        // 触发定时扫描 (非阻塞异步方式)
        if (_pBLEScan && !_isScanning) {
            _isScanning = true;
            _pBLEScan->start(_bleScanDurationS, scanCompleteCB, false);
        }

        // 检测 BLE 连接心跳超时（超过 15 秒未收到包则判定为断开）
        if (_bleConnected && (millis() - _lastBlePacketTime > 15000)) {
            _bleConnected = false;
            Serial.println("[BLE DEBUG] BLE connection timeout, set status to disconnected.");
        }
    }
}

void SmartGateway::onSensorData(SensorDataCallback cb) {
    _sensorCb = cb;
}

void SmartGateway::onConfigDuration(ConfigDurationCallback cb) {
    _durationCb = cb;
}

void SmartGateway::onConfigPumpTime(ConfigPumpTimeCallback cb) {
    _pumpTimeCb = cb;
}

void SmartGateway::publishStatus(const char* jsonPayload) {
    String topic = "water/" + _stationName + "/system/status";
    _netManager.publish(topic.c_str(), jsonPayload);
}

void SmartGateway::publishLog(int channelId, const char* logMessage) {
    String topic = "water/" + _stationName + "/log/" + String(channelId);
    _netManager.publish(topic.c_str(), logMessage);
}

bool SmartGateway::isBleConnected() const {
    return _bleConnected;
}

NetworkState SmartGateway::getNetworkState() {
    return _netManager.getState();
}

void SmartGateway::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->handleMqttMessage(topic, payload, length);
    }
}

void SmartGateway::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    String topicStr(topic);
    String stationName = _stationName;

    if (_sensorSource == SensorSource::MQTT && topicStr == _mqttSensorDataSub) {
        Serial.print("MQTT RX ");
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (!error) {
            const char* name = doc["name"] | "";
            if (strcmp(name, stationName.c_str()) == 0) {
                uint16_t sensor1 = doc["sensor1"] | 0;
                uint16_t sensor2 = doc["sensor2"] | 0;
                uint16_t sensor3 = doc["sensor3"] | 0;
                uint8_t stateByte = doc["state"] | 0;
                if (_sensorCb) {
                    _sensorCb(sensor1, sensor2, sensor3, stateByte);
                }
            }
        } else {
            Serial.print(F("[GATEWAY MQTT] Failed to parse sensor JSON: "));
            Serial.println(error.f_str());
        }
        return;
    }

    String valStr;
    for (unsigned int i = 0; i < length; i++) {
        valStr += (char)payload[i];
    }
    float value = valStr.toFloat();

    String durationPrefix = "water/" + stationName + "/config/duration/";
    String pumpTimePrefix = "water/" + stationName + "/config/pump_time/";

    if (topicStr.startsWith(durationPrefix)) {
        int channelId = topicStr.substring(durationPrefix.length()).toInt();
        if (_durationCb) {
            _durationCb(channelId, value);
        }
    } else if (topicStr.startsWith(pumpTimePrefix)) {
        int channelId = topicStr.substring(pumpTimePrefix.length()).toInt();
        if (_pumpTimeCb) {
            _pumpTimeCb(channelId, value);
        }
    }
}

void SmartGateway::setupBLE() {
    BLEDevice::init("");
    _pBLEScan = BLEDevice::getScan();
    _pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(), true);
    _pBLEScan->setActiveScan(true);
    _pBLEScan->setInterval(100);
    _pBLEScan->setWindow(99);
}

void SmartGateway::AdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
    if (_instance && advertisedDevice.getName() == _instance->_targetBleName.c_str()) {
        if (advertisedDevice.haveManufacturerData()) {
            std::string data = advertisedDevice.getManufacturerData();
            if (data.length() == 9 || data.length() == 10) {
                uint8_t cIdLsb = (uint8_t)data[0];
                uint8_t cIdMsb = (uint8_t)data[1];
                uint16_t cId = (cIdMsb << 8) | cIdLsb;
                if (cId == _instance->_bleCompanyIdVal) {
                    uint8_t seqNum = (data.length() == 10) ? (uint8_t)data[9] : (uint8_t)data[8];
                    if (seqNum != _instance->_lastSeqNum) {
                        _instance->_lastSeqNum = seqNum;
                        _instance->_bleConnected = true;
                        _instance->_lastBlePacketTime = millis();

                        uint16_t sensor1 = ((uint8_t)data[2] << 8) | (uint8_t)data[3];
                        uint16_t sensor2 = ((uint8_t)data[4] << 8) | (uint8_t)data[5];
                        uint16_t sensor3 = ((uint8_t)data[6] << 8) | (uint8_t)data[7];
                        uint8_t stateByte = (data.length() == 10) ? (uint8_t)data[8] : 0;

                        // Serial.printf("[GATEWAY BLE] 接收到唯一广播包, seqNum=%u, stateByte=0x%02X\n", seqNum, stateByte);

                        if (_instance->_sensorCb) {
                            _instance->_sensorCb(sensor1, sensor2, sensor3, stateByte);
                        }
                    }
                }
            }
        }
    }
}
