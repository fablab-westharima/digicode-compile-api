/*
 * DigiMotion - SineOscillator impl (Layer 4)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under the GNU Affero General Public License version 3 or later.
 */

#include "SineOscillator.h"

#include <cmath>

namespace {
// 2π as a double constant. M_PI is POSIX, not standard C++, so define
// locally to keep the file portable across native and arduino-esp32.
constexpr double TWO_PI_D = 6.283185307179586476925286766559;
}

SineOscillator::SineOscillator()
    : _amplitude(0),
      _offset(0),
      _periodMs(1000),
      _phaseRad(0.0),
      _startMs(0),
      _started(false) {}

void SineOscillator::setAmplitude(int amplitude) { _amplitude = amplitude; }
void SineOscillator::setOffset(int offset)       { _offset = offset; }
void SineOscillator::setPeriod(unsigned long periodMs) { _periodMs = periodMs; }
void SineOscillator::setPhase(double phaseRad)   { _phaseRad = phaseRad; }

int SineOscillator::getAmplitude() const          { return _amplitude; }
int SineOscillator::getOffset() const             { return _offset; }
unsigned long SineOscillator::getPeriod() const   { return _periodMs; }
double SineOscillator::getPhase() const           { return _phaseRad; }

void SineOscillator::start(unsigned long nowMs) {
    _startMs = nowMs;
    _started = true;
}

void SineOscillator::stop()             { _started = false; }
bool SineOscillator::isStarted() const  { return _started; }

long SineOscillator::valueAt(unsigned long nowMs) const {
    if (!_started || _periodMs == 0) return static_cast<long>(_offset);
    const double elapsed = static_cast<double>(nowMs - _startMs);
    const double angle = TWO_PI_D * elapsed / static_cast<double>(_periodMs) + _phaseRad;
    const double y = static_cast<double>(_offset) +
                     static_cast<double>(_amplitude) * std::sin(angle);
    return static_cast<long>(y);
}
