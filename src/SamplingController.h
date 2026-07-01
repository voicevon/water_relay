#ifndef SAMPLING_CONTROLLER_H
#define SAMPLING_CONTROLLER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief 10步采样与抽空业务逻辑通道管理类 (采样控制器)
 * 独立追踪每个采样通道的状态机、定时参数、水泵继电器状态等。
 */
class SamplingController {
public:
    int id;
    int relayPin; // 物理 GPIO 引脚
    int ledPin;   // 物理 GPIO 引脚
    uint32_t expectedDuration;
    uint32_t pumpWorkTime;
    float safetyFactor;
    
    // 状态机运行时参数
    int currentStage;
    bool active;
    bool isDetected;
    bool lastPumpState;
    uint32_t onCount;
    uint32_t offCount;

    SamplingController(int chId, int rPin, int lPin, uint32_t defaultDur);

    /**
     * @brief 更新该通道的配置参数并重算休眠周期
     */
    void updateParameters(uint32_t newDur, uint32_t newPumpTime);

    /**
     * @brief 核心状态机更新函数（10步采样算法骨架）
     * @param detected 传感器滤波后自适应判定结果
     * @return bool 是否需要运行水泵
     */
    bool update(bool detected);

private:
    uint32_t stageStartTime; // 记录当前阶段开始的时间戳（秒）
    uint32_t restTime;       // 计算出的休眠时长

    void transitionTo(int targetStage, uint32_t nowSec);
    void calculateStages();
};

#endif // SAMPLING_CONTROLLER_H
