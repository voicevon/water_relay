#include "SamplingController.h"

SamplingController::SamplingController(int chId, int rPin, int lPin, uint32_t defaultDur) 
    : id(chId), relayPin(rPin), ledPin(lPin), expectedDuration(defaultDur), 
      pumpWorkTime(DEFAULT_PUMP_WORK_SEC), safetyFactor(DEFAULT_SAFETY_FACTOR),
      currentStage(1), active(false), isDetected(false), lastPumpState(false),
      onCount(0), offCount(0), stageStartTime(millis() / 1000), restTime(0) {
    calculateStages();
}

void SamplingController::updateParameters(uint32_t newDur, uint32_t newPumpTime) {
    expectedDuration = newDur;
    pumpWorkTime = newPumpTime;
    calculateStages();
}

bool SamplingController::update(bool detected) {
    isDetected = detected;
    uint32_t nowSec = millis() / 1000;
    uint32_t elapsed = nowSec - stageStartTime;

    bool hasTransitioned = false;

    // 1. 水流消失时的异常跳转保护
    if (!detected) {
        if (currentStage == 2) {
            // 待稳期水流消失，直接退回 Stage 1
            transitionTo(1, nowSec);
            hasTransitioned = true;
        } 
        else if (currentStage >= 3 && currentStage <= 8) {
            // 已开始采样但中途水流消失，强行跳转到 Stage 9 进行管道排空
            transitionTo(9, nowSec);
            hasTransitioned = true;
        }
        else if (currentStage == 10) {
            // 结束后传感器变干，复位回到 Stage 1
            transitionTo(1, nowSec);
            hasTransitioned = true;
        }
        // Stage 9 必须跑满排空时间，不在此处进行干认
    }

    // 2. 正常时间与信号触发状态机
    if (!hasTransitioned) {
        switch (currentStage) {
            case 1:
                if (detected) transitionTo(2, nowSec);
                break;
            case 2:
                if (elapsed >= STABILIZATION_TIME_SEC) transitionTo(3, nowSec);
                break;
            case 3:
                if (elapsed >= pumpWorkTime) transitionTo(4, nowSec);
                break;
            case 4:
                if (elapsed >= restTime) transitionTo(5, nowSec);
                break;
            case 5:
                if (elapsed >= pumpWorkTime) transitionTo(6, nowSec);
                break;
            case 6:
                if (elapsed >= restTime) transitionTo(7, nowSec);
                break;
            case 7:
                if (elapsed >= pumpWorkTime) transitionTo(8, nowSec);
                break;
            case 8:
                if (elapsed >= SIGNAL_LOST_TIME_SEC) transitionTo(9, nowSec);
                break;
            case 9:
                if (elapsed >= pumpWorkTime) transitionTo(10, nowSec);
                break;
            case 10:
                // 等待 !detected 触发复位，或满 10 分钟 (600秒) 后自动返回 Stage 1
                if (elapsed >= 600) {
                    transitionTo(1, nowSec);
                }
                break;
        }
    }

    // 确认水泵开启状态 (Stage 3, 5, 7, 9 开启)
    bool needsPump = (currentStage == 3 || currentStage == 5 || currentStage == 7 || currentStage == 9);
    
    // 状态更新守卫
    if (needsPump != lastPumpState) {
        lastPumpState = needsPump;
        if (needsPump) onCount++;
        else offCount++;
    }

    return needsPump;
}

void SamplingController::transitionTo(int targetStage, uint32_t nowSec) {
    currentStage = targetStage;
    stageStartTime = nowSec;
    active = (currentStage > 1 && currentStage < 10);
}

void SamplingController::calculateStages() {
    int32_t available_time = (int32_t)(expectedDuration * safetyFactor) - STABILIZATION_TIME_SEC;
    if (available_time < 0) available_time = 0;
    
    int32_t rTime = (available_time - (int32_t)pumpWorkTime * 3) / 2;
    if (rTime < 0) rTime = 0;
    
    restTime = (uint32_t)rTime;
}
