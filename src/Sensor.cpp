#include "Sensor.h"

Sensor::Sensor(int id, int thresholdOffset)
    : _id(id), _thresholdOffset(thresholdOffset) {
    reset();
}

void Sensor::reset() {
    _rawValue = 0;
    _filteredValue = 0;
    _baselineValue = 0;
    _lastState = SensorState::NO_WATER;

    _maHead = 0;
    _maCount = 0;
    _maSum = 0;
    for (int i = 0; i < MA_WINDOW; i++) {
        _maBuf[i] = 0;
    }

    _baseHead = 0;
    _baseCount = 0;
    _baseSum = 0;
    for (int i = 0; i < BASELINE_WINDOW; i++) {
        _baseBuf[i] = 0;
    }
}

uint16_t Sensor::pushFilter(uint16_t value) {
    if (_maCount < MA_WINDOW) {
        _maBuf[_maHead] = value;
        _maSum += value;
        _maCount++;
    } else {
        _maSum -= _maBuf[_maHead];
        _maBuf[_maHead] = value;
        _maSum += value;
    }
    _maHead = (_maHead + 1) % MA_WINDOW;
    return (uint16_t)(_maSum / _maCount);
}

uint16_t Sensor::pushBaseline(uint16_t value) {
    if (_baseCount < BASELINE_WINDOW) {
        _baseBuf[_baseHead] = value;
        _baseSum += value;
        _baseCount++;
    } else {
        _baseSum -= _baseBuf[_baseHead];
        _baseBuf[_baseHead] = value;
        _baseSum += value;
    }
    _baseHead = (_baseHead + 1) % BASELINE_WINDOW;
    return (uint16_t)(_baseSum / _baseCount);
}

uint16_t Sensor::getThreshold() const {
    if (_lastState == SensorState::NO_WATER) {
        return _baselineValue + _thresholdOffset;
    } else {
        return _baselineValue - _thresholdOffset;
    }
}

void Sensor::pushRaw(uint16_t value) {
    _rawValue = value;
    _filteredValue = pushFilter(value);
    _baselineValue = pushBaseline(_filteredValue);

    uint16_t curThreshold = getThreshold();
    SensorState currentState = _lastState;
    SensorState nextState = currentState;

    if (currentState == SensorState::NO_WATER) {
        if (_filteredValue > curThreshold) {
            nextState = SensorState::HAS_WATER;
        } else {
            nextState = SensorState::NO_WATER;
        }
    } else { // HAS_WATER
        if (_filteredValue < curThreshold) {
            nextState = SensorState::NO_WATER;
        } else {
            nextState = SensorState::HAS_WATER;
        }
    }

    if (nextState != currentState) {
        _lastState = nextState;
        if (_stateChangeCb) {
            _stateChangeCb(_id, nextState);
        }
    }
}

void Sensor::onStateChange(StateChangeCallback cb) {
    _stateChangeCb = cb;
}
