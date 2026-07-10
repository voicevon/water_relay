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
    int id;                      // 通道唯一 ID (例如 1, 2, 3，映射到 App 指示灯)
    int relayPin;                // 水泵继电器物理 GPIO 引脚
    int ledPin;                  // 传感器/通道状态指示灯物理 GPIO 引脚
    uint32_t expectedDuration;   // 采样周期预期总时长（秒）
    uint32_t pumpWorkTime;       // 单次抽水/水泵开启时长（秒）
    float safetyFactor;          // 采样安全系数（用以计算实际可用时长）
    
    // 状态机运行时参数
    int currentStage;            // 当前所处的 10 步状态机阶段 (Stage 0 ~ 9)
    bool active;                 // 采样流程是否处于执行中 (Stage 1~8 为 true，Stage 0和9 空闲/结束为 false)
    bool isDetected;             // 当前传感器是否检测到有水
    bool lastPumpState;          // 上一次水泵开启状态（用于防抖判定及计数）
    uint32_t onCount;            // 本次周期内水泵累计开启次数
    uint32_t offCount;           // 本次周期内水泵累计关闭次数
    uint32_t stageStartTime;     // 记录当前阶段开始的时间戳（秒）

    /**
     * @brief 构造函数
     * @param chId 通道唯一 ID
     * @param rPin 继电器控制 GPIO 引脚
     * @param lPin 指示灯控制 GPIO 引脚
     * @param defaultDur 默认的采样预期总时长（秒）
     */
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
    uint32_t restTime;       // 计算出的休眠时长

    void transitionTo(int targetStage, uint32_t nowSec);
    void calculateStages();
};

#endif // SAMPLING_CONTROLLER_H
