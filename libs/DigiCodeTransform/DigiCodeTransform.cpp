/**
 * DigiCodeTransform Library Implementation
 *
 * Copyright (c) 2024 DigiCo LLC
 * Licensed under MIT License
 */

#include "DigiCodeTransform.h"

// ===== TransformOscillator Implementation =====

TransformOscillator::TransformOscillator() {
    _pin = -1;
    _attached = false;
    _reverse = false;
    _trim = 0;
    _position = 90;
    _amplitude = 0;
    _offset = 0;
    _period = 2000;
    _phase = 0;
    _startTime = 0;
    _isOscillating = false;
}

void TransformOscillator::attach(int pin, bool reverse) {
    _pin = pin;
    _reverse = reverse;
    _servo.attach(pin);
    _attached = true;
    setPosition(90);
}

void TransformOscillator::detach() {
    if (_attached) {
        _servo.detach();
        _attached = false;
    }
}

void TransformOscillator::setParameters(int amplitude, int offset, unsigned long period, float phase) {
    _amplitude = amplitude;
    _offset = offset;
    _period = period;
    _phase = phase;
    _startTime = millis();
    _isOscillating = true;
}

void TransformOscillator::refresh() {
    if (!_attached || !_isOscillating) return;

    unsigned long elapsed = millis() - _startTime;
    float cycle = (float)elapsed / (float)_period;
    float angle = _amplitude * sin(2.0 * PI * cycle + _phase) + _offset;

    int pos = (int)(90 + angle);
    pos = constrain(pos, 0, 180);

    if (_reverse) {
        pos = 180 - pos;
    }

    _position = pos + _trim;
    _position = constrain(_position, 0, 180);
    _servo.write(_position);
}

void TransformOscillator::setPosition(int position) {
    _isOscillating = false;
    _position = position + _trim;

    if (_reverse) {
        _position = 180 - _position;
    }

    _position = constrain(_position, 0, 180);

    if (_attached) {
        _servo.write(_position);
    }
}

int TransformOscillator::getPosition() {
    return _position;
}

void TransformOscillator::setTrim(int trim) {
    _trim = trim;
}

int TransformOscillator::getTrim() {
    return _trim;
}

void TransformOscillator::stop() {
    _isOscillating = false;
}

void TransformOscillator::reset() {
    _amplitude = 0;
    _offset = 0;
    _phase = 0;
    _isOscillating = false;
    setPosition(90);
}

// ===== DigiCodeTransform Implementation =====

DigiCodeTransform::DigiCodeTransform() {
    _mode = MODE_WALK;
    _leftOffset = 0;
    _rightOffset = 0;
}

void DigiCodeTransform::init(int pinLeftLeg, int pinRightLeg, int pinLeftFoot, int pinRightFoot) {
    _servos[LEFT_LEG].attach(pinLeftLeg);
    _servos[RIGHT_LEG].attach(pinRightLeg, true);  // Right leg reversed
    _servos[LEFT_FOOT].attach(pinLeftFoot);
    _servos[RIGHT_FOOT].attach(pinRightFoot, true);  // Right foot reversed

    home();
}

void DigiCodeTransform::setMode(const char* mode) {
    if (strcmp(mode, "walk") == 0) {
        _mode = MODE_WALK;
    } else if (strcmp(mode, "roll") == 0) {
        _mode = MODE_ROLL;
    }
}

void DigiCodeTransform::transform(const char* mode) {
    if (strcmp(mode, "walk") == 0) {
        // Transform to walk mode - legs vertical
        int target[4] = {90, 90, 90, 90};
        _moveServos(500, target);
        _mode = MODE_WALK;
    } else if (strcmp(mode, "roll") == 0) {
        // Transform to roll mode - legs horizontal for wheel action
        int target[4] = {90, 90, 0, 180};  // Feet become wheels
        _moveServos(500, target);
        _mode = MODE_ROLL;
    }
}

int DigiCodeTransform::getMode() {
    return _mode;
}

void DigiCodeTransform::alignAngle() {
    int target[4] = {90, 90, 90, 90};
    _moveServos(500, target);
}

void DigiCodeTransform::calibrate(int leftOffset, int rightOffset) {
    _leftOffset = leftOffset;
    _rightOffset = rightOffset;
    _servos[LEFT_LEG].setTrim(leftOffset);
    _servos[LEFT_FOOT].setTrim(leftOffset);
    _servos[RIGHT_LEG].setTrim(rightOffset);
    _servos[RIGHT_FOOT].setTrim(rightOffset);
}

void DigiCodeTransform::setTrims(int leftLeg, int rightLeg, int leftFoot, int rightFoot) {
    _servos[LEFT_LEG].setTrim(leftLeg);
    _servos[RIGHT_LEG].setTrim(rightLeg);
    _servos[LEFT_FOOT].setTrim(leftFoot);
    _servos[RIGHT_FOOT].setTrim(rightFoot);
}

void DigiCodeTransform::home() {
    int target[4] = {90, 90, 90, 90};
    _moveServos(500, target);
}

void DigiCodeTransform::walk(int steps, int period, int direction) {
    int amplitude[4] = {30, 30, 20, 20};
    int offset[4] = {0, 0, 0, 0};
    float phase[4];

    if (direction > 0) {
        // Forward
        phase[0] = 0;
        phase[1] = PI;
        phase[2] = PI / 2;
        phase[3] = PI / 2 + PI;
    } else {
        // Backward
        phase[0] = PI;
        phase[1] = 0;
        phase[2] = PI / 2 + PI;
        phase[3] = PI / 2;
    }

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeTransform::walkPower(int direction, int power) {
    int period = map(power, 0, 100, 2000, 600);
    int amplitude = map(power, 0, 100, 10, 35);

    int amp[4] = {amplitude, amplitude, amplitude / 2, amplitude / 2};
    int offset[4] = {0, 0, 0, 0};
    float phase[4];

    if (direction > 0) {
        phase[0] = 0;
        phase[1] = PI;
        phase[2] = PI / 2;
        phase[3] = PI / 2 + PI;
    } else {
        phase[0] = PI;
        phase[1] = 0;
        phase[2] = PI / 2 + PI;
        phase[3] = PI / 2;
    }

    _execute(amp, offset, period, phase, 2);
}

void DigiCodeTransform::turn(int steps, int period, int direction) {
    int amplitude[4] = {30, 30, 20, 20};
    int offset[4] = {0, 0, 0, 0};
    float phase[4];

    if (direction > 0) {
        // Turn left
        phase[0] = 0;
        phase[1] = 0;
        phase[2] = PI / 2;
        phase[3] = PI / 2;
    } else {
        // Turn right
        phase[0] = PI;
        phase[1] = PI;
        phase[2] = PI / 2 + PI;
        phase[3] = PI / 2 + PI;
    }

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeTransform::trot(int steps, int period) {
    int amplitude[4] = {35, 35, 25, 25};
    int offset[4] = {0, 0, 0, 0};
    float phase[4] = {0, PI, PI / 2, PI / 2 + PI};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeTransform::pushUp(int steps, int period) {
    for (int i = 0; i < steps; i++) {
        // Down
        int down[4] = {90, 90, 60, 120};
        _moveServos(period / 2, down);

        // Up
        int up[4] = {90, 90, 120, 60};
        _moveServos(period / 2, up);
    }
    home();
}

void DigiCodeTransform::lateral(int steps, int period, int direction) {
    int amplitude[4] = {0, 0, 30, 30};
    int offset[4] = {0, 0, 0, 0};
    float phase[4];

    if (direction > 0) {
        // Left
        phase[0] = 0;
        phase[1] = 0;
        phase[2] = 0;
        phase[3] = PI;
    } else {
        // Right
        phase[0] = 0;
        phase[1] = 0;
        phase[2] = PI;
        phase[3] = 0;
    }

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeTransform::dance(int steps, int period) {
    int amplitude[4] = {25, 25, 25, 25};
    int offset[4] = {0, 0, 0, 0};
    float phase[4] = {0, PI / 2, PI, PI * 3 / 2};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeTransform::roll(int direction, int speed) {
    // In roll mode, feet (LEFT_FOOT, RIGHT_FOOT) act as wheels
    // Using 360-degree continuous rotation servos:
    // - 90 = stop
    // - 0 = full speed one direction
    // - 180 = full speed other direction

    speed = constrain(abs(speed), 0, 100);

    // Convert speed (0-100) to servo speed (-100 to +100)
    int servoSpeed = (direction > 0) ? speed : -speed;

    // Left foot: speed to angle (forward = positive speed = angle > 90)
    int leftAngle = map(servoSpeed, -100, 100, 0, 180);

    // Right foot: inverted (mounted opposite side)
    int rightAngle = map(-servoSpeed, -100, 100, 0, 180);

    _servos[LEFT_FOOT].setPosition(leftAngle);
    _servos[RIGHT_FOOT].setPosition(rightAngle);
}

void DigiCodeTransform::rollPower(int direction, int power) {
    int speed = map(power, 0, 100, 0, 100);
    roll(direction, speed);
}

void DigiCodeTransform::rollRotate(int direction, int power) {
    // Spin in place using 360-degree continuous rotation servos
    // Both feet move in same direction for rotation

    power = constrain(abs(power), 0, 100);

    // Convert power to servo angle
    // For spin: both wheels same direction
    int angle = map(power, 0, 100, 90, 180);

    if (direction > 0) {
        // Rotate left: both wheels turn left
        _servos[LEFT_FOOT].setPosition(180 - angle);  // Left foot backward
        _servos[RIGHT_FOOT].setPosition(180 - angle); // Right foot backward (inverted = forward)
    } else {
        // Rotate right: both wheels turn right
        _servos[LEFT_FOOT].setPosition(angle);        // Left foot forward
        _servos[RIGHT_FOOT].setPosition(angle);       // Right foot forward (inverted = backward)
    }
}

void DigiCodeTransform::stop(const char* mode) {
    for (int i = 0; i < 4; i++) {
        _servos[i].stop();
    }

    if (strcmp(mode, "roll") == 0) {
        // Stop roll mode - center the feet
        _servos[LEFT_FOOT].setPosition(90);
        _servos[RIGHT_FOOT].setPosition(90);
    } else {
        // Stop walk mode - return to home
        home();
    }
}

void DigiCodeTransform::moveServos(int time, int target[4]) {
    _moveServos(time, target);
}

void DigiCodeTransform::_moveServos(int time, int target[4]) {
    int currentPos[4];
    for (int i = 0; i < 4; i++) {
        currentPos[i] = _servos[i].getPosition();
    }

    int steps = time / 20;
    if (steps < 1) steps = 1;

    for (int step = 1; step <= steps; step++) {
        for (int i = 0; i < 4; i++) {
            int pos = currentPos[i] + (target[i] - currentPos[i]) * step / steps;
            _servos[i].setPosition(pos);
        }
        delay(20);
    }
}

void DigiCodeTransform::_execute(int amplitude[4], int offset[4], unsigned long period, float phase[4], int steps) {
    for (int i = 0; i < 4; i++) {
        _servos[i].setParameters(amplitude[i], offset[i], period, phase[i]);
    }

    unsigned long startTime = millis();
    unsigned long duration = period * steps;

    while (millis() - startTime < duration) {
        for (int i = 0; i < 4; i++) {
            _servos[i].refresh();
        }
        delay(10);
    }

    for (int i = 0; i < 4; i++) {
        _servos[i].stop();
    }
}

void DigiCodeTransform::_oscillateServos(int amplitude[4], int offset[4], unsigned long period, float phase[4], float cycle) {
    for (int i = 0; i < 4; i++) {
        _servos[i].setParameters(amplitude[i], offset[i], period, phase[i]);
    }

    unsigned long startTime = millis();
    unsigned long duration = (unsigned long)(period * cycle);

    while (millis() - startTime < duration) {
        for (int i = 0; i < 4; i++) {
            _servos[i].refresh();
        }
        delay(10);
    }
}
