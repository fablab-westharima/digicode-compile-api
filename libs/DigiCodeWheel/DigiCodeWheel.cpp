/**
 * DigiCodeWheel Library Implementation
 *
 * Copyright (c) 2024 DigiCo LLC
 * Licensed under MIT License
 *
 * Supports continuous rotation servos and DC motors with H-bridge
 */

#include "DigiCodeWheel.h"

// PWM settings for DC motor mode
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8
#define PWM_MAX 255

// Servo settings
#define SERVO_STOP 90      // 90 degrees = stop for continuous rotation servo
#define SERVO_MIN 0        // Full speed one direction
#define SERVO_MAX 180      // Full speed other direction

// ESP32-Arduino Core version detection
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define USE_NEW_LEDC_API 1
#else
  #define USE_NEW_LEDC_API 0
#endif

DigiCodeWheel::DigiCodeWheel() {
    _pinLeftA = -1;
    _pinLeftB = -1;
    _pinRightA = -1;
    _pinRightB = -1;
    _servoMode = true;  // Default to servo mode
    _pwmChannelLA = 0;
    _pwmChannelLB = 1;
    _pwmChannelRA = 2;
    _pwmChannelRB = 3;
}

// 2-pin mode: Continuous rotation servos
void DigiCodeWheel::init(int pinLeft, int pinRight) {
    _pinLeftA = pinLeft;
    _pinRightA = pinRight;
    _servoMode = true;

    // Attach servos
    _servoLeft.attach(_pinLeftA);
    _servoRight.attach(_pinRightA);

    // Initialize to stop
    _servoLeft.write(SERVO_STOP);
    _servoRight.write(SERVO_STOP);
}

// 4-pin mode: DC motors with H-bridge
void DigiCodeWheel::init(int pinLeftA, int pinLeftB, int pinRightA, int pinRightB) {
    _pinLeftA = pinLeftA;
    _pinLeftB = pinLeftB;
    _pinRightA = pinRightA;
    _pinRightB = pinRightB;
    _servoMode = false;

    _setupPWM();
}

void DigiCodeWheel::_setupPWM() {
#if USE_NEW_LEDC_API
    // ESP32-Arduino Core 3.x: Use new LEDC API
    ledcAttach(_pinLeftA, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(_pinLeftB, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(_pinRightA, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(_pinRightB, PWM_FREQ, PWM_RESOLUTION);
#else
    // ESP32-Arduino Core 2.x: Use old LEDC API
    ledcSetup(_pwmChannelLA, PWM_FREQ, PWM_RESOLUTION);
    ledcSetup(_pwmChannelLB, PWM_FREQ, PWM_RESOLUTION);
    ledcSetup(_pwmChannelRA, PWM_FREQ, PWM_RESOLUTION);
    ledcSetup(_pwmChannelRB, PWM_FREQ, PWM_RESOLUTION);

    ledcAttachPin(_pinLeftA, _pwmChannelLA);
    ledcAttachPin(_pinLeftB, _pwmChannelLB);
    ledcAttachPin(_pinRightA, _pwmChannelRA);
    ledcAttachPin(_pinRightB, _pwmChannelRB);
#endif
}

// Set servo speed: speed -100 to +100, invert for right wheel
void DigiCodeWheel::_setServoSpeed(Servo& servo, int speed, bool invert) {
    // Clamp speed to -100 to +100
    speed = constrain(speed, -100, 100);

    // Invert if needed (for right wheel which is mounted opposite)
    if (invert) {
        speed = -speed;
    }

    // Convert speed (-100 to +100) to servo angle (0 to 180)
    // speed -100 -> angle 0 (full reverse)
    // speed 0    -> angle 90 (stop)
    // speed +100 -> angle 180 (full forward)
    int angle = map(speed, -100, 100, SERVO_MIN, SERVO_MAX);
    servo.write(angle);
}

// DC motor control
void DigiCodeWheel::_setMotor(int pinA, int pinB, int channelA, int channelB, int speed) {
    speed = constrain(speed, -100, 100);

#if USE_NEW_LEDC_API
    if (speed > 0) {
        ledcWrite(pinA, map(speed, 0, 100, 0, PWM_MAX));
        ledcWrite(pinB, 0);
    } else if (speed < 0) {
        ledcWrite(pinA, 0);
        ledcWrite(pinB, map(-speed, 0, 100, 0, PWM_MAX));
    } else {
        ledcWrite(pinA, 0);
        ledcWrite(pinB, 0);
    }
#else
    if (speed > 0) {
        ledcWrite(channelA, map(speed, 0, 100, 0, PWM_MAX));
        ledcWrite(channelB, 0);
    } else if (speed < 0) {
        ledcWrite(channelA, 0);
        ledcWrite(channelB, map(-speed, 0, 100, 0, PWM_MAX));
    } else {
        ledcWrite(channelA, 0);
        ledcWrite(channelB, 0);
    }
#endif
}

void DigiCodeWheel::forward(int speed) {
    speed = abs(speed);
    if (_servoMode) {
        _setServoSpeed(_servoLeft, speed, false);   // Left: forward = positive
        _setServoSpeed(_servoRight, speed, true);   // Right: inverted (opposite side)
    } else {
        _setMotor(_pinLeftA, _pinLeftB, _pwmChannelLA, _pwmChannelLB, speed);
        _setMotor(_pinRightA, _pinRightB, _pwmChannelRA, _pwmChannelRB, speed);
    }
}

void DigiCodeWheel::backward(int speed) {
    speed = abs(speed);
    if (_servoMode) {
        _setServoSpeed(_servoLeft, -speed, false);  // Left: backward = negative
        _setServoSpeed(_servoRight, -speed, true);  // Right: inverted
    } else {
        _setMotor(_pinLeftA, _pinLeftB, _pwmChannelLA, _pwmChannelLB, -speed);
        _setMotor(_pinRightA, _pinRightB, _pwmChannelRA, _pwmChannelRB, -speed);
    }
}

void DigiCodeWheel::turnLeft(int speed) {
    speed = abs(speed);
    if (_servoMode) {
        _setServoSpeed(_servoLeft, speed / 2, false);   // Left slower
        _setServoSpeed(_servoRight, speed, true);       // Right faster
    } else {
        _setMotor(_pinLeftA, _pinLeftB, _pwmChannelLA, _pwmChannelLB, speed / 2);
        _setMotor(_pinRightA, _pinRightB, _pwmChannelRA, _pwmChannelRB, speed);
    }
}

void DigiCodeWheel::turnRight(int speed) {
    speed = abs(speed);
    if (_servoMode) {
        _setServoSpeed(_servoLeft, speed, false);       // Left faster
        _setServoSpeed(_servoRight, speed / 2, true);   // Right slower
    } else {
        _setMotor(_pinLeftA, _pinLeftB, _pwmChannelLA, _pwmChannelLB, speed);
        _setMotor(_pinRightA, _pinRightB, _pwmChannelRA, _pwmChannelRB, speed / 2);
    }
}

void DigiCodeWheel::spinLeft(int speed) {
    speed = abs(speed);
    if (_servoMode) {
        _setServoSpeed(_servoLeft, -speed, false);  // Left backward
        _setServoSpeed(_servoRight, speed, true);   // Right forward
    } else {
        _setMotor(_pinLeftA, _pinLeftB, _pwmChannelLA, _pwmChannelLB, -speed);
        _setMotor(_pinRightA, _pinRightB, _pwmChannelRA, _pwmChannelRB, speed);
    }
}

void DigiCodeWheel::spinRight(int speed) {
    speed = abs(speed);
    if (_servoMode) {
        _setServoSpeed(_servoLeft, speed, false);   // Left forward
        _setServoSpeed(_servoRight, -speed, true);  // Right backward
    } else {
        _setMotor(_pinLeftA, _pinLeftB, _pwmChannelLA, _pwmChannelLB, speed);
        _setMotor(_pinRightA, _pinRightB, _pwmChannelRA, _pwmChannelRB, -speed);
    }
}

void DigiCodeWheel::stop() {
    if (_servoMode) {
        _servoLeft.write(SERVO_STOP);
        _servoRight.write(SERVO_STOP);
    } else {
#if USE_NEW_LEDC_API
        ledcWrite(_pinLeftA, 0);
        ledcWrite(_pinLeftB, 0);
        ledcWrite(_pinRightA, 0);
        ledcWrite(_pinRightB, 0);
#else
        ledcWrite(_pwmChannelLA, 0);
        ledcWrite(_pwmChannelLB, 0);
        ledcWrite(_pwmChannelRA, 0);
        ledcWrite(_pwmChannelRB, 0);
#endif
    }
}

void DigiCodeWheel::setMotorSpeed(int leftSpeed, int rightSpeed) {
    if (_servoMode) {
        _setServoSpeed(_servoLeft, leftSpeed, false);
        _setServoSpeed(_servoRight, rightSpeed, true);
    } else {
        _setMotor(_pinLeftA, _pinLeftB, _pwmChannelLA, _pwmChannelLB, leftSpeed);
        _setMotor(_pinRightA, _pinRightB, _pwmChannelRA, _pwmChannelRB, rightSpeed);
    }
}

void DigiCodeWheel::move(int leftSpeed, int rightSpeed, int duration) {
    setMotorSpeed(leftSpeed, rightSpeed);
    delay(duration);
    stop();
}
