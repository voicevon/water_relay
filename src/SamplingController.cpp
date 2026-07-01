#include "SamplingController.h"

SamplingController::SamplingController(int chId, int rPin, int lPin, uint32_t defaultDur) 
    : id(chId), relayPin(rPin), ledPin(lPin), expectedDuration(defaultDur), 
      pumpWorkTime(DEFAULT_PUMP_WORK_SEC), safetyFactor(DEFAULT_SAFETY_FACTOR),
      currentStage(0), active(false), isDetected(false), lastPumpState(false),
      onCount(0), offCount(0), stageStartTime(0), restTime(0) {
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

    // 1. 水流消失时的异常跳转保护
    if (!detected) {
        if (currentStage == 1) {
            // 待稳期水流消失，直接退回 Stage 0
            transitionTo(0, nowSec);
        } 
        else if (currentStage >= 2 && currentStage <= 7) {
            // 已开始采样但中途水流消失，强行跳转到 Stage 8 进行管道排空
            transitionTo(8, nowSec);
        }
        else if (currentStage == 9) {
            // 结束后传感器变干，复位回到 Stage 0
            transitionTo(0, nowSec);
        }
        // Stage 8 必须跑满排空时间，不在此处进行干预
    }

    // 2. 正常时间与信号触发状态机
    switch (currentStage) {
        case 0:
            if (detected) transitionTo(1, nowSec);
            break;
        case 1:
            if (elapsed >= STABILIZATION_TIME_SEC) transitionTo(2, nowSec);
            break;
        case 2:
            if (elapsed >= pumpWorkTime) transitionTo(3, nowSec);
            break;
        case 3:
            if (elapsed >= restTime) transitionTo(4, nowSec);
            break;
        case 4:
            if (elapsed >= pumpWorkTime) transitionTo(5, nowSec);
            break;
        case 5:
            if (elapsed >= restTime) transitionTo(6, nowSec);
            break;
        case 6:
            if (elapsed >= pumpWorkTime) transitionTo(7, nowSec);
            break;
        case 7:
            if (elapsed >= SIGNAL_LOST_TIME_SEC) transitionTo(8, nowSec);
            break;
        case 8:
            if (elapsed >= pumpWorkTime) transitionTo(9, nowSec);
            break;
        case 9:
            // 等待 !detected 触发复位
            break;
    }

    // 确认水泵开启状态 (Stage 2, 4, 6, 8 开启)
    bool needsPump = (currentStage == 2 || currentStage == 4 || currentStage == 6 || currentStage == 8);
    
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
    active = (currentStage > 0 && currentStage < 9);
}

void SamplingController::calculateStages() {
    int32_t available_time = (int32_t)(expectedDuration * safetyFactor) - STABILIZATION_TIME_SEC;
    if (available_time < 0) available_time = 0;
    
    int32_t rTime = (available_time - (int32_t)pumpWorkTime * 3) / 2;
    if (rTime < 0) rTime = 0;
    
    restTime = (uint32_t)rTime;
}
