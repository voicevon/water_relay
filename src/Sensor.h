#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

/**
 * @brief 传感器有水/无水状态枚举
 */
enum class SensorState {
    NO_WATER,   // 干燥正常状态
    HAS_WATER   // 触水触发状态
};

/**
 * @brief 传感器通道控制类
 * 独立维护各通道的滤波、慢速环境基准线追踪以及施密特双向触发器状态机。
 */
class Sensor {
public:
    // 状态跳转回调：参数包含传感器ID和改变后的状态
    typedef void (*StateChangeCallback)(int sensorId, SensorState newState);

    Sensor(int id, int thresholdOffset = 50);

    /**
     * @brief 喂入最新的传感器原始电容值，返回更新后的通道状态
     */
    void pushRaw(uint16_t value);

    // 获取各种运行时状态
    int getId() const { return _id; }
    uint16_t getRaw() const { return _rawValue; }
    uint16_t getFiltered() const { return _filteredValue; }
    uint16_t getBaseline() const { return _baselineValue; }
    uint16_t getThreshold() const;
    SensorState getState() const { return _lastState; }
    bool isDetected() const { return _lastState == SensorState::HAS_WATER; }

    // 注册状态变化回调
    void onStateChange(StateChangeCallback cb);

    // 重置通道状态及滤波数据
    void reset();

private:
    int _id;
    int _thresholdOffset;
    uint16_t _rawValue = 0;
    uint16_t _filteredValue = 0;
    uint16_t _baselineValue = 0;
    SensorState _lastState = SensorState::NO_WATER;
    StateChangeCallback _stateChangeCb = nullptr;

    // 滑动滤波器环形缓冲区结构
    static const int MA_WINDOW = 50;
    static const int BASELINE_WINDOW = 200;

    uint16_t _maBuf[MA_WINDOW];
    int _maHead = 0;
    int _maCount = 0;
    uint32_t _maSum = 0;

    uint16_t _baseBuf[BASELINE_WINDOW];
    int _baseHead = 0;
    int _baseCount = 0;
    uint32_t _baseSum = 0;

    uint16_t pushFilter(uint16_t value);
    uint16_t pushBaseline(uint16_t value);
};

#endif // SENSOR_H
