#include "SmartGateway.h"
#include <ArduinoJson.h>

// WiFi 重连退避参数（#ifndef 便于外部覆盖）：基础间隔起指数退避
// （20s→40s→80s→160s→320s 封顶），避免 STA 反复扫描占用射频导致 AP beacon 缺帧
#ifndef WIFI_RECONNECT_BASE_MS
#define WIFI_RECONNECT_BASE_MS           20000UL
#define WIFI_RECONNECT_BACKOFF_MAX_SHIFT 4
#endif

// WiFi 重连连续失败计数（指数退避用）
static uint8_t s_wifi_fail_count = 0;

// 初始化单例指针
SmartGateway* SmartGateway::_instance = nullptr;

// ============================================================
//  RAII 互斥锁辅助类（保证多任务访问 PubSubClient 线程安全）
// ============================================================
class MqttLock {
public:
    MqttLock(SemaphoreHandle_t mutex) : _mutex(mutex), _locked(false) {
        if (_mutex) {
            _locked = (xSemaphoreTake(_mutex, pdMS_TO_TICKS(3000)) == pdTRUE);
            if (!_locked) {
                Serial.println("[SmartGateway MQTT] WARN: MqttLock timeout (3s), skipping operation.");
            }
        }
    }
    ~MqttLock() {
        if (_mutex && _locked) {
            xSemaphoreGive(_mutex);
        }
    }
    bool locked() const { return _locked; }
private:
    SemaphoreHandle_t _mutex;
    bool _locked;
};

// ============================================================
//  后台异步 DNS 解析与 MQTT 连接任务（FreeRTOS Task）
// ============================================================
void smartGatewayMqttTask(void* pvParameters) {
    SmartGateway* gw = (SmartGateway*)pvParameters;
    if (!gw) {
        vTaskDelete(NULL);
        return;
    }

    unsigned long now = millis();
    // 断线重连时清空了 IP，将强制立即刷新 DNS 查询
    if (gw->_resolvedBrokerIp[0] == 0 || (now - gw->_lastDnsResolveMs > 300000)) {
        gw->_lastDnsResolveMs = now;
        IPAddress tempIP = gw->resolveBrokerIp();
        if (tempIP[0] != 0) {
            gw->_resolvedBrokerIp = tempIP;
            Serial.printf("[Gateway MQTT Task] DNS resolved IP: %s\n", tempIP.toString().c_str());
        } else {
            Serial.println("[Gateway MQTT Task] DNS resolution failed, will fallback to domain.");
        }
    }

    {
        MqttLock lock(gw->_mqttMutex);
        if (lock.locked()) {
            if (gw->_resolvedBrokerIp[0] != 0) {
                gw->_mqttClient.setServer(gw->_resolvedBrokerIp, gw->_mqttPort);
            } else {
                gw->_mqttClient.setServer(gw->_mqttBroker.c_str(), gw->_mqttPort);
            }

            String clientId = "water_brain_client-" + String(random(0xffff), HEX);
            Serial.println("[Gateway MQTT Task] Attempting connection to Broker...");

            bool success;
            if (gw->_mqttUsername.length() > 0 && gw->_mqttPassword.length() > 0) {
                success = gw->_mqttClient.connect(clientId.c_str(), gw->_mqttUsername.c_str(), gw->_mqttPassword.c_str());
            } else {
                success = gw->_mqttClient.connect(clientId.c_str());
            }

            if (success) {
                Serial.println("[Gateway MQTT Task] Connected successfully!");
                // 核心关键：握手成功当场在锁内原子订阅水泵时长与数据配置主题！
                String durationSub = "water/" + gw->_stationName + "/config/duration/+";
                String pumpTimeSub = "water/" + gw->_stationName + "/config/pump_time/+";
                gw->_mqttClient.subscribe(durationSub.c_str());
                gw->_mqttClient.subscribe(pumpTimeSub.c_str());
                Serial.printf("[Gateway MQTT Task] Subscribed to %s and %s\n", durationSub.c_str(), pumpTimeSub.c_str());

                if (gw->_sensorSource == SensorSource::MQTT) {
                    gw->_mqttClient.subscribe(gw->_mqttSensorDataSub.c_str());
                    Serial.printf("[Gateway MQTT Task] Subscribed to %s\n", gw->_mqttSensorDataSub.c_str());
                }
            } else {
                Serial.printf("[Gateway MQTT Task] Connection failed, state = %d\n", gw->_mqttClient.state());
                // 连接失败，主动将 IP 清零，下次重连强制重新解析 DNS
                gw->_resolvedBrokerIp = IPAddress(0, 0, 0, 0);
                // 主动清理旧 socket
                gw->_espClient.stop();
            }
        }
    }

    gw->_mqttConnecting = false;
    vTaskDelete(NULL);
}

// BLE 异步扫描结束后的回调
void SmartGateway::scanCompleteCB(BLEScanResults results) {
    BLEDevice::getScan()->clearResults(); // 清理扫描结果，释放内存
    if (_instance) {
        _instance->_isScanning = false;
    }
}

SmartGateway::SmartGateway(SensorSource source) 
    : _sensorSource(source), _mqttClient(_espClient) {
    _instance = this;
    _mqttMutex = xSemaphoreCreateMutex();
    _lastSeqNum = -1;
    _bleConnected = false;
    _isScanning = false;
    _lastBlePacketTime = 0;
    _resolvedBrokerIp = IPAddress(0, 0, 0, 0);
}

SmartGateway::~SmartGateway() {
    if (_mqttMutex) {
        vSemaphoreDelete(_mqttMutex);
        _mqttMutex = NULL;
    }
}

IPAddress SmartGateway::resolveBrokerIp() {
    IPAddress resolvedIP;
    if (resolvedIP.fromString(_mqttBroker.c_str())) {
        return resolvedIP;
    }
    if (WiFi.hostByName(_mqttBroker.c_str(), resolvedIP)) {
        Serial.printf("[Gateway DNS] Successfully resolved %s to %s via standard DNS\n", 
                      _mqttBroker.c_str(), resolvedIP.toString().c_str());
        return resolvedIP;
    } else {
        Serial.printf("[Gateway DNS] Standard DNS failed for %s\n", _mqttBroker.c_str());
        return IPAddress(0, 0, 0, 0);
    }
}

void SmartGateway::begin(const SmartGatewayConfig& config) {
    _stationName = config.stationName;
    _mqttSensorDataSub = config.mqttSensorDataSub;
    _targetBleName = config.targetBleName;
    _bleCompanyIdVal = config.bleCompanyIdVal;
    _bleScanDurationS = config.bleScanDurationS;
    _mqttReconnectIntervalMs = config.mqttReconnectIntervalMs;

    _wifiSsid = config.wifiSsid;
    _wifiPassword = config.wifiPassword;
    _mqttBroker = config.mqttBroker;
    _mqttPort = config.mqttPort;
    _mqttUsername = config.mqttUsername;
    _mqttPassword = config.mqttPassword;

    _mqttClient.setCallback(mqttCallback);

    // 保持 AP_STA 模式，确保 Web 配置后台热点稳定可用
    WiFi.mode(WIFI_AP_STA);
    // 关闭 SDK 内部自动重连：其后台高频全信道扫描会占用射频，
    // 导致 AP beacon 缺帧、电脑扫不到热点。重连节奏由 loop() 控制
    WiFi.setAutoReconnect(false);
    WiFi.begin(_wifiSsid.c_str(), _wifiPassword.c_str());

    // 阻塞等待连接，最多 20 次 × 500ms = 10s
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("[SmartGateway WiFi] Connected. IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();
        Serial.println("[SmartGateway WiFi] Connect failed. Will retry in background.");
    }

    // 初始解析 Broker IP
    IPAddress brokerIP = resolveBrokerIp();
    if (brokerIP[0] != 0) {
        _resolvedBrokerIp = brokerIP;
        _lastDnsResolveMs = millis();
        _mqttClient.setServer(brokerIP, _mqttPort);
    } else {
        _mqttClient.setServer(_mqttBroker.c_str(), _mqttPort);
    }

    if (_sensorSource == SensorSource::BLE) {
        setupBLE();
    }
}

void SmartGateway::updateWifiCredentials(const String& ssid, const String& pass) {
    _wifiSsid = ssid;
    _wifiPassword = pass;
    WiFi.begin(_wifiSsid.c_str(), _wifiPassword.c_str());
    Serial.printf("[SmartGateway WiFi] Credentials updated, reconnecting to \"%s\"...\n",
                  _wifiSsid.c_str());
}

void SmartGateway::loop() {
    unsigned long now = millis();

    // 1. 维护 WiFi 自动重连（指数退避：失败越多间隔越长，
    //    退避窗口内射频安静，保证 AP 热点稳定广播可被扫描）
    if (WiFi.status() != WL_CONNECTED) {
        uint8_t shift = s_wifi_fail_count < WIFI_RECONNECT_BACKOFF_MAX_SHIFT
                        ? s_wifi_fail_count : WIFI_RECONNECT_BACKOFF_MAX_SHIFT;
        unsigned long interval = WIFI_RECONNECT_BASE_MS << shift;
        if (now - _lastWifiReconnectAttempt >= interval) {
            _lastWifiReconnectAttempt = now;
            s_wifi_fail_count++;
            Serial.printf("[SmartGateway WiFi] Disconnected. Reconnecting (fail=%u, next in %lus)...\n",
                          s_wifi_fail_count, interval / 1000UL);
            WiFi.begin(_wifiSsid.c_str(), _wifiPassword.c_str());
        }
    } else {
        _lastWifiReconnectAttempt = now;
        s_wifi_fail_count = 0;

        // 2. 主线程避让：后台连接任务执行中直接跳过，绝不并发触碰 _mqttClient
        if (!_mqttConnecting) {
            bool connected = false;
            {
                MqttLock lock(_mqttMutex);
                if (lock.locked()) {
                    connected = _mqttClient.connected();
                }
            }

            if (!connected) {
                // 断线时清空 IP 缓存，下次重连立即刷新 DDNS 解析
                _resolvedBrokerIp = IPAddress(0, 0, 0, 0);

                if (now - _lastMqttReconnectAttempt >= _mqttReconnectIntervalMs) {
                    _lastMqttReconnectAttempt = now;
                    _mqttConnecting = true;

                    BaseType_t ret = xTaskCreate(smartGatewayMqttTask, "gw_mqtt_task", 8192, this, 1, NULL);
                    if (ret != pdPASS) {
                        _mqttConnecting = false;
                        Serial.println("[SmartGateway MQTT] Error: Failed to create MQTT task!");
                    }
                }
            } else {
                // 已连接，维持保活
                MqttLock lock(_mqttMutex);
                if (lock.locked()) {
                    _mqttClient.loop();
                }
            }
        }
    }

    // 3. BLE 扫描处理
    if (_sensorSource == SensorSource::BLE) {
        if (_pBLEScan && !_isScanning) {
            _isScanning = true;
            _pBLEScan->start(_bleScanDurationS, scanCompleteCB, false);
        }

        if (_bleConnected && (now - _lastBlePacketTime > 15000)) {
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

bool SmartGateway::publishStatus(const char* jsonPayload) {
    if (_mqttConnecting) return false;
    MqttLock lock(_mqttMutex);
    if (!lock.locked() || !_mqttClient.connected()) return false;
    String topic = "water/" + _stationName + "/system/status";
    return _mqttClient.publish(topic.c_str(), (const uint8_t*)jsonPayload, strlen(jsonPayload), true);
}

bool SmartGateway::publishSensorState(int sensorId, int stage, const char* remark, float duration, int pumpTime, uint32_t uptime, uint32_t stageStartSec) {
    if (_mqttConnecting) return false;
    MqttLock lock(_mqttMutex);
    if (!lock.locked() || !_mqttClient.connected()) return false;
    String topic = "water/" + _stationName + "/state";
    StaticJsonDocument<256> doc;
    doc["sensorId"] = sensorId;
    doc["stage"] = stage;
    doc["remark"] = remark;
    doc["duration"] = duration;
    doc["pumpTime"] = pumpTime;
    doc["uptime"] = uptime;
    doc["stageStartSec"] = stageStartSec;
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    return _mqttClient.publish(topic.c_str(), (const uint8_t*)jsonPayload.c_str(), jsonPayload.length(), true);
}

bool SmartGateway::publishPhotoTake(const char* targetStation) {
    if (_mqttConnecting) return false;
    MqttLock lock(_mqttMutex);
    if (!lock.locked() || !_mqttClient.connected()) return false;
    String topic = "water/photo/take";
    return _mqttClient.publish(topic.c_str(), (const uint8_t*)targetStation, strlen(targetStation), false);
}

bool SmartGateway::isBleConnected() const {
    return _bleConnected;
}

NetworkState SmartGateway::getNetworkState() {
    if (WiFi.status() != WL_CONNECTED) {
        return STATE_DISCONNECTED;
    }
    if (_mqttConnecting) {
        return STATE_MQTT_CONNECTING;
    }
    MqttLock lock(_mqttMutex);
    if (lock.locked() && _mqttClient.connected()) {
        return STATE_MQTT_CONNECTED;
    }
    return STATE_WIFI_CONNECTED;
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
        int sensorId = topicStr.substring(durationPrefix.length()).toInt();
        if (_durationCb) {
            _durationCb(sensorId, value);
        }
    } else if (topicStr.startsWith(pumpTimePrefix)) {
        int sensorId = topicStr.substring(pumpTimePrefix.length()).toInt();
        if (_pumpTimeCb) {
            _pumpTimeCb(sensorId, value);
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

                        if (_instance->_sensorCb) {
                            _instance->_sensorCb(sensor1, sensor2, sensor3, stateByte);
                        }
                    }
                }
            }
        }
    }
}
