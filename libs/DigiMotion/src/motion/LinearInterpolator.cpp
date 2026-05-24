/*
 * DigiMotion - LinearInterpolator impl (Layer 4)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under the GNU Affero General Public License version 3 or later.
 */

#include "LinearInterpolator.h"

LinearInterpolator::LinearInterpolator()
    : _startMs(0),
      _durationMs(0),
      _startValue(0),
      _endValue(0),
      _started(false) {}

void LinearInterpolator::start(unsigned long nowMs, long startValue,
                                long endValue, unsigned long durationMs) {
    _startMs = nowMs;
    _durationMs = durationMs;
    _startValue = startValue;
    _endValue = endValue;
    _started = true;
}

void LinearInterpolator::stop()                       { _started = false; }
bool LinearInterpolator::isStarted() const            { return _started; }
long LinearInterpolator::getStartValue() const        { return _startValue; }
long LinearInterpolator::getEndValue() const          { return _endValue; }
unsigned long LinearInterpolator::getDuration() const { return _durationMs; }

long LinearInterpolator::valueAt(unsigned long nowMs) const {
    if (!_started || _durationMs == 0) return _endValue;
    if (nowMs <= _startMs) return _startValue;
    const unsigned long elapsed = nowMs - _startMs;
    if (elapsed >= _durationMs) return _endValue;
    // 64-bit intermediate to avoid overflow on large delta * elapsed.
    const long long delta = static_cast<long long>(_endValue) -
                            static_cast<long long>(_startValue);
    const long long offset = delta * static_cast<long long>(elapsed) /
                              static_cast<long long>(_durationMs);
    return static_cast<long>(static_cast<long long>(_startValue) + offset);
}

bool LinearInterpolator::isDone(unsigned long nowMs) const {
    if (!_started) return true;
    if (_durationMs == 0) return true;
    if (nowMs <= _startMs) return false;
    return (nowMs - _startMs) >= _durationMs;
}
