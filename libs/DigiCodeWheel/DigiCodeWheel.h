/**
 * DigiCodeWheel Library
 *
 * A library for controlling wheeled robots.
 * Supports:
 * - 2-pin mode: Continuous rotation servos (360 degree servos)
 * - 4-pin mode: DC motors with H-bridge driver
 *
 * Copyright (c) 2024 DigiCo LLC
 * Licensed under MIT License
 */

#ifndef DIGICODE_WHEEL_H
#define DIGICODE_WHEEL_H

#include <Arduino.h>
#include <ESP32Servo.h>

class DigiCodeWheel {
public:
    DigiCodeWheel();

    // Initialization
    // 2-pin mode: For continuous rotation servos
    void init(int pinLeft, int pinRight);
    // 4-pin mode: For DC motors with H-bridge
    void init(int pinLeftA, int pinLeftB, int pinRightA, int pinRightB);

    // Basic movements (speed: 0-100)
    void forward(int speed);
    void backward(int speed);
    void turnLeft(int speed);
    void turnRight(int speed);
    void spinLeft(int speed);
    void spinRight(int speed);
    void stop();

    // Advanced control
    void setMotorSpeed(int leftSpeed, int rightSpeed);
    void move(int leftSpeed, int rightSpeed, int duration);

private:
    int _pinLeftA;
    int _pinLeftB;
    int _pinRightA;
    int _pinRightB;
    bool _servoMode;  // true = continuous rotation servo, false = DC motor

    // For servo mode
    Servo _servoLeft;
    Servo _servoRight;

    // For DC motor mode (PWM channels for ESP32)
    int _pwmChannelLA;
    int _pwmChannelLB;
    int _pwmChannelRA;
    int _pwmChannelRB;

    void _setServoSpeed(Servo& servo, int speed, bool invert);
    void _setMotor(int pinA, int pinB, int channelA, int channelB, int speed);
    void _setupPWM();
};

#endif // DIGICODE_WHEEL_H
