/**
 * DigiCodeHumanoid Library
 *
 * A library for controlling bipedal humanoid robots with 4 servos.
 * Designed for DigiCode visual programming environment.
 *
 * Copyright (c) 2024 DigiCo LLC
 * Licensed under MIT License
 */

#ifndef DIGICODE_HUMANOID_H
#define DIGICODE_HUMANOID_H

#include <Arduino.h>
#include <ESP32Servo.h>

// Gesture definitions
#define Happy       0
#define SuperHappy  1
#define Sad         2
#define Sleeping    3
#define Fart        4
#define Confused    5
#define Love        6
#define Angry       7
#define Fretful     8
#define Magic       9
#define Wave        10
#define Victory     11
#define Fail        12

// Sound definitions
#define S_connection     0
#define S_disconnection  1
#define S_buttonPushed   2
#define S_mode1          3
#define S_mode2          4
#define S_mode3          5
#define S_surprise       6
#define S_OhOoh          7
#define S_OhOoh2         8
#define S_cuddly         9
#define S_sleeping       10
#define S_happy          11
#define S_superHappy     12
#define S_happy_short    13
#define S_sad            14
#define S_confused       15
#define S_fart1          16
#define S_fart2          17
#define S_fart3          18

// Oscillator class for smooth servo movements
class Oscillator {
public:
    Oscillator();
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

// Main DigiCodeHumanoid class
class DigiCodeHumanoid {
public:
    DigiCodeHumanoid();

    // Initialization
    void init(int pinLeftLeg, int pinRightLeg, int pinLeftFoot, int pinRightFoot);
    void init(int pinLeftLeg, int pinRightLeg, int pinLeftFoot, int pinRightFoot,
              bool loadCalibration, int buzzerPin = -1);

    // Calibration
    void setTrims(int leftLeg, int rightLeg, int leftFoot, int rightFoot);
    void saveTrimsToEEPROM();
    void loadTrimsFromEEPROM();

    // Basic positions
    void home();
    void setRestState(bool state);
    bool getRestState();

    // Movement functions
    void walk(int steps, int period, int direction);
    void turn(int steps, int period, int direction);
    void jump(int steps, int period);
    void bend(int steps, int period, int direction);
    void shakeLeg(int steps, int period, int direction);
    void updown(int steps, int period, int height);
    void swing(int steps, int period, int height);
    void tiptoeSwing(int steps, int period, int height);
    void jitter(int steps, int period, int height);
    void ascendingTurn(int steps, int period, int height);
    void moonwalk(int steps, int period, int height, int direction);
    void crusaito(int steps, int period, int height, int direction);
    void flapping(int steps, int period, int height, int direction);

    // Dance shortcuts
    void dance(int steps, int period);
    void kickLeft(int steps, int period);
    void kickRight(int steps, int period);

    // Convenience methods with direction as enum
    void bendLeft(int steps, int period);
    void bendRight(int steps, int period);

    // Gesture (emotion expressions)
    void playGesture(int gesture);

    // Sound
    void initBuzzer(int pin);
    void sing(int songName);
    void playTone(int frequency, int duration);
    void bendTone(int initFreq, int endFreq, float step, int duration, int silentDuration);
    void clearBuzzer();

    // Servo access (for advanced use)
    void moveServos(int time, int target[4]);
    void oscillate(int amplitude[4], int offset[4], int period, float phase[4]);
    void execute(int amplitude[4], int offset[4], int period, float phase[4], int steps);

    // Utility
    int getDistance();  // For ultrasonic sensor (if connected)
    int getNoise();     // For sound sensor (if connected)

private:
    Oscillator _servos[4];
    int _buzzerPin;
    bool _isResting;

    // Servo indices
    static const int LEFT_LEG = 0;
    static const int RIGHT_LEG = 1;
    static const int LEFT_FOOT = 2;
    static const int RIGHT_FOOT = 3;

    // Helper functions
    void _execute(int amplitude[4], int offset[4], unsigned long period, float phase[4], int steps);
    void _moveServos(int time, int target[4]);
    void _oscillateServos(int amplitude[4], int offset[4], unsigned long period, float phase[4], float cycle);

    // Sound helpers
    void _playNote(int frequency, int duration);
};

#endif // DIGICODE_HUMANOID_H
