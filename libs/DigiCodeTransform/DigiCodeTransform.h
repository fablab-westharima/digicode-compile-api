/**
 * DigiCodeTransform Library
 *
 * A library for controlling Ninja robots (4-servo biped with Walk & Roll modes).
 * Based on HP Robot's Walk & Roll concept.
 * Designed for DigiCode visual programming environment.
 *
 * Copyright (c) 2024 DigiCo LLC
 * Licensed under MIT License
 */

#ifndef DIGICODE_TRANSFORM_H
#define DIGICODE_TRANSFORM_H

#include <Arduino.h>
#include <ESP32Servo.h>

// Mode definitions
#define MODE_WALK 0
#define MODE_ROLL 1

// Oscillator class for smooth servo movements
class TransformOscillator {
public:
    TransformOscillator();
    void attach(int pin, bool reverse = false);
    void detach();
    void setParameters(int amplitude, int offset, unsigned long period, float phase);
    void refresh();
    void setPosition(int position);
    int getPosition();
    void setTrim(int trim);
    int getTrim();
    void stop();
    void reset();

private:
    Servo _servo;
    int _pin;
    bool _attached;
    bool _reverse;
    int _trim;
    int _position;

    // Oscillation parameters
    int _amplitude;
    int _offset;
    unsigned long _period;
    float _phase;
    unsigned long _startTime;
    bool _isOscillating;
};

// Main DigiCodeTransform class
class DigiCodeTransform {
public:
    DigiCodeTransform();

    // Initialization
    void init(int pinLeftLeg, int pinRightLeg, int pinLeftFoot, int pinRightFoot);

    // Mode control
    void setMode(const char* mode);
    void transform(const char* mode);
    int getMode();

    // Calibration
    void alignAngle();
    void calibrate(int leftOffset, int rightOffset);
    void setTrims(int leftLeg, int rightLeg, int leftFoot, int rightFoot);
    void home();

    // Walk mode movements
    void walk(int steps, int period, int direction);
    void walkPower(int direction, int power);
    void turn(int steps, int period, int direction);
    void trot(int steps, int period);
    void pushUp(int steps, int period);
    void lateral(int steps, int period, int direction);
    void dance(int steps, int period);

    // Roll mode movements
    void roll(int direction, int speed);
    void rollPower(int direction, int power);
    void rollRotate(int direction, int power);

    // Stop
    void stop(const char* mode);

    // Servo access
    void moveServos(int time, int target[4]);

private:
    TransformOscillator _servos[4];
    int _mode;  // 0=walk, 1=roll
    int _leftOffset;
    int _rightOffset;

    // Servo indices and types:
    // LEG servos = 180° standard servos (for walking)
    // FOOT servos = 360° continuous rotation servos (for rolling)
    static const int LEFT_LEG = 0;    // 180° servo
    static const int RIGHT_LEG = 1;   // 180° servo
    static const int LEFT_FOOT = 2;   // 360° continuous rotation
    static const int RIGHT_FOOT = 3;  // 360° continuous rotation

    // Helper functions
    void _execute(int amplitude[4], int offset[4], unsigned long period, float phase[4], int steps);
    void _moveServos(int time, int target[4]);
    void _oscillateServos(int amplitude[4], int offset[4], unsigned long period, float phase[4], float cycle);
};

#endif // DIGICODE_TRANSFORM_H
