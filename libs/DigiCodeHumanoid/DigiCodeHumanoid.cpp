/**
 * DigiCodeHumanoid Library Implementation
 *
 * Copyright (c) 2024 DigiCo LLC
 * Licensed under MIT License
 */

#include "DigiCodeHumanoid.h"
#include <EEPROM.h>
#include <math.h>

#define EEPROM_SIZE 16
#define TRIM_LL_ADDR 0
#define TRIM_RL_ADDR 2
#define TRIM_LF_ADDR 4
#define TRIM_RF_ADDR 6

// ==================== Oscillator Class ====================

Oscillator::Oscillator() {
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

void Oscillator::attach(int pin, bool reverse) {
    _pin = pin;
    _reverse = reverse;
    _servo.attach(_pin);
    _attached = true;
}

void Oscillator::detach() {
    if (_attached) {
        _servo.detach();
        _attached = false;
    }
}

void Oscillator::setParameters(int amplitude, int offset, unsigned long period, float phase) {
    _amplitude = amplitude;
    _offset = offset;
    _period = period;
    _phase = phase;
    _startTime = millis();
    _isOscillating = true;
}

void Oscillator::refresh() {
    if (!_attached || !_isOscillating) return;

    unsigned long elapsed = millis() - _startTime;
    float cycle = (float)elapsed / (float)_period;
    float angle = sin(2.0 * PI * cycle + _phase);

    int pos = (int)(_amplitude * angle) + _offset + 90;
    pos = constrain(pos, 0, 180);

    if (_reverse) {
        pos = 180 - pos;
    }

    _position = pos + _trim;
    _position = constrain(_position, 0, 180);
    _servo.write(_position);
}

void Oscillator::setPosition(int position) {
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

int Oscillator::getPosition() {
    return _position;
}

void Oscillator::setTrim(int trim) {
    _trim = trim;
}

int Oscillator::getTrim() {
    return _trim;
}

void Oscillator::stop() {
    _isOscillating = false;
}

void Oscillator::reset() {
    _amplitude = 0;
    _offset = 0;
    _phase = 0;
    _isOscillating = false;
}

// ==================== DigiCodeHumanoid Class ====================

DigiCodeHumanoid::DigiCodeHumanoid() {
    _buzzerPin = -1;
    _isResting = false;
}

void DigiCodeHumanoid::init(int pinLeftLeg, int pinRightLeg, int pinLeftFoot, int pinRightFoot) {
    init(pinLeftLeg, pinRightLeg, pinLeftFoot, pinRightFoot, true, -1);
}

void DigiCodeHumanoid::init(int pinLeftLeg, int pinRightLeg, int pinLeftFoot, int pinRightFoot,
                        bool loadCalibration, int buzzerPin) {
    _servos[LEFT_LEG].attach(pinLeftLeg);
    _servos[RIGHT_LEG].attach(pinRightLeg);
    _servos[LEFT_FOOT].attach(pinLeftFoot, true);  // Reversed
    _servos[RIGHT_FOOT].attach(pinRightFoot);

    if (buzzerPin >= 0) {
        initBuzzer(buzzerPin);
    }

    if (loadCalibration) {
        EEPROM.begin(EEPROM_SIZE);
        loadTrimsFromEEPROM();
    }

    home();
}

void DigiCodeHumanoid::setTrims(int leftLeg, int rightLeg, int leftFoot, int rightFoot) {
    _servos[LEFT_LEG].setTrim(leftLeg);
    _servos[RIGHT_LEG].setTrim(rightLeg);
    _servos[LEFT_FOOT].setTrim(leftFoot);
    _servos[RIGHT_FOOT].setTrim(rightFoot);
}

void DigiCodeHumanoid::saveTrimsToEEPROM() {
    EEPROM.writeShort(TRIM_LL_ADDR, _servos[LEFT_LEG].getTrim());
    EEPROM.writeShort(TRIM_RL_ADDR, _servos[RIGHT_LEG].getTrim());
    EEPROM.writeShort(TRIM_LF_ADDR, _servos[LEFT_FOOT].getTrim());
    EEPROM.writeShort(TRIM_RF_ADDR, _servos[RIGHT_FOOT].getTrim());
    EEPROM.commit();
}

void DigiCodeHumanoid::loadTrimsFromEEPROM() {
    int ll = EEPROM.readShort(TRIM_LL_ADDR);
    int rl = EEPROM.readShort(TRIM_RL_ADDR);
    int lf = EEPROM.readShort(TRIM_LF_ADDR);
    int rf = EEPROM.readShort(TRIM_RF_ADDR);

    // Validate (avoid corrupted EEPROM values)
    if (abs(ll) < 50 && abs(rl) < 50 && abs(lf) < 50 && abs(rf) < 50) {
        setTrims(ll, rl, lf, rf);
    }
}

void DigiCodeHumanoid::home() {
    int positions[4] = {90, 90, 90, 90};
    _moveServos(300, positions);
    _isResting = false;
}

void DigiCodeHumanoid::setRestState(bool state) {
    _isResting = state;
}

bool DigiCodeHumanoid::getRestState() {
    return _isResting;
}

// ==================== Movement Functions ====================

void DigiCodeHumanoid::walk(int steps, int period, int direction) {
    int amplitude[4] = {30, 30, 20, 20};
    int offset[4] = {0, 0, 0, 0};
    float phase[4];

    if (direction == 1) {  // Forward
        phase[LEFT_LEG] = 0;
        phase[RIGHT_LEG] = PI;
        phase[LEFT_FOOT] = PI / 2;
        phase[RIGHT_FOOT] = PI / 2 + PI;
    } else {  // Backward
        phase[LEFT_LEG] = 0;
        phase[RIGHT_LEG] = PI;
        phase[LEFT_FOOT] = PI / 2 + PI;
        phase[RIGHT_FOOT] = PI / 2;
    }

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::turn(int steps, int period, int direction) {
    int amplitude[4] = {30, 30, 20, 20};
    int offset[4] = {0, 0, 0, 0};
    float phase[4];

    if (direction == 1) {  // Left
        phase[LEFT_LEG] = 0;
        phase[RIGHT_LEG] = PI;
        phase[LEFT_FOOT] = PI / 2 + PI;
        phase[RIGHT_FOOT] = PI / 2 + PI;
    } else {  // Right
        phase[LEFT_LEG] = 0;
        phase[RIGHT_LEG] = PI;
        phase[LEFT_FOOT] = PI / 2;
        phase[RIGHT_FOOT] = PI / 2;
    }

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::jump(int steps, int period) {
    int amplitude[4] = {0, 0, 35, 35};
    int offset[4] = {0, 0, -15, 15};
    float phase[4] = {0, 0, 0, 0};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::bend(int steps, int period, int direction) {
    if (direction == 1) {
        bendLeft(steps, period);
    } else {
        bendRight(steps, period);
    }
}

void DigiCodeHumanoid::bendLeft(int steps, int period) {
    int amplitude[4] = {30, 0, 0, 0};
    int offset[4] = {0, 0, 0, 0};
    float phase[4] = {0, 0, 0, 0};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::bendRight(int steps, int period) {
    int amplitude[4] = {0, 30, 0, 0};
    int offset[4] = {0, 0, 0, 0};
    float phase[4] = {0, 0, 0, 0};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::shakeLeg(int steps, int period, int direction) {
    int amplitude[4];
    int offset[4] = {0, 0, 0, 0};
    float phase[4] = {0, 0, 0, PI};

    if (direction == 1) {  // Left leg
        amplitude[LEFT_LEG] = 30;
        amplitude[RIGHT_LEG] = 0;
        amplitude[LEFT_FOOT] = 30;
        amplitude[RIGHT_FOOT] = 0;
    } else {  // Right leg
        amplitude[LEFT_LEG] = 0;
        amplitude[RIGHT_LEG] = 30;
        amplitude[LEFT_FOOT] = 0;
        amplitude[RIGHT_FOOT] = 30;
    }

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::updown(int steps, int period, int height) {
    int amplitude[4] = {0, 0, height, height};
    int offset[4] = {0, 0, 0, 0};
    float phase[4] = {0, 0, 0, 0};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::swing(int steps, int period, int height) {
    int amplitude[4] = {0, 0, height, height};
    int offset[4] = {0, 0, 0, 0};
    float phase[4] = {0, 0, 0, PI};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::tiptoeSwing(int steps, int period, int height) {
    int amplitude[4] = {0, 0, height, height};
    int offset[4] = {0, 0, height / 2, -height / 2};
    float phase[4] = {0, 0, 0, PI};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::jitter(int steps, int period, int height) {
    int amplitude[4] = {height, height, 0, 0};
    int offset[4] = {0, 0, 0, 0};
    float phase[4] = {0, PI, 0, 0};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::ascendingTurn(int steps, int period, int height) {
    int amplitude[4] = {height, height, height, height};
    int offset[4] = {0, 0, height / 2, -height / 2};
    float phase[4] = {0, PI, PI / 2, PI / 2 + PI};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::moonwalk(int steps, int period, int height, int direction) {
    int amplitude[4] = {25, 25, height, height};
    int offset[4] = {0, 0, 0, 0};
    float phase[4];

    if (direction == 1) {  // Right
        phase[LEFT_LEG] = 0;
        phase[RIGHT_LEG] = PI;
        phase[LEFT_FOOT] = PI / 2 + PI;
        phase[RIGHT_FOOT] = PI / 2;
    } else {  // Left
        phase[LEFT_LEG] = 0;
        phase[RIGHT_LEG] = PI;
        phase[LEFT_FOOT] = PI / 2;
        phase[RIGHT_FOOT] = PI / 2 + PI;
    }

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::crusaito(int steps, int period, int height, int direction) {
    int amplitude[4] = {25, 25, height, height};
    int offset[4] = {0, 0, 0, 0};
    float phase[4];

    if (direction == 1) {  // Left
        phase[LEFT_LEG] = 0;
        phase[RIGHT_LEG] = PI;
        phase[LEFT_FOOT] = PI / 2;
        phase[RIGHT_FOOT] = PI / 2;
    } else {  // Right
        phase[LEFT_LEG] = 0;
        phase[RIGHT_LEG] = PI;
        phase[LEFT_FOOT] = PI / 2 + PI;
        phase[RIGHT_FOOT] = PI / 2 + PI;
    }

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::flapping(int steps, int period, int height, int direction) {
    int amplitude[4] = {height, height, 0, 0};
    int offset[4] = {0, 0, 0, 0};
    float phase[4];

    if (direction == 1) {  // Forward
        phase[LEFT_LEG] = 0;
        phase[RIGHT_LEG] = 0;
        phase[LEFT_FOOT] = 0;
        phase[RIGHT_FOOT] = 0;
    } else {  // Backward
        phase[LEFT_LEG] = PI;
        phase[RIGHT_LEG] = PI;
        phase[LEFT_FOOT] = 0;
        phase[RIGHT_FOOT] = 0;
    }

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::dance(int steps, int period) {
    int amplitude[4] = {25, 25, 25, 25};
    int offset[4] = {0, 0, 0, 0};
    float phase[4] = {0, PI, PI / 4, PI / 4 + PI};

    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::kickLeft(int steps, int period) {
    for (int i = 0; i < steps; i++) {
        int positions1[4] = {60, 90, 90, 90};
        _moveServos(period / 4, positions1);
        int positions2[4] = {60, 90, 60, 90};
        _moveServos(period / 4, positions2);
        int positions3[4] = {90, 90, 90, 90};
        _moveServos(period / 2, positions3);
    }
}

void DigiCodeHumanoid::kickRight(int steps, int period) {
    for (int i = 0; i < steps; i++) {
        int positions1[4] = {90, 120, 90, 90};
        _moveServos(period / 4, positions1);
        int positions2[4] = {90, 120, 90, 120};
        _moveServos(period / 4, positions2);
        int positions3[4] = {90, 90, 90, 90};
        _moveServos(period / 2, positions3);
    }
}

// ==================== Gesture Functions ====================

void DigiCodeHumanoid::playGesture(int gesture) {
    switch (gesture) {
        case Happy:
            sing(S_happy);
            swing(3, 800, 20);
            break;

        case SuperHappy:
            sing(S_superHappy);
            dance(4, 600);
            break;

        case Sad:
            sing(S_sad);
            updown(3, 1000, 15);
            break;

        case Sleeping:
            sing(S_sleeping);
            // Gentle rocking
            for (int i = 0; i < 3; i++) {
                swing(1, 2000, 10);
                delay(500);
            }
            break;

        case Fart:
            sing(S_fart1);
            bend(2, 800, 1);
            break;

        case Confused:
            sing(S_confused);
            jitter(3, 500, 20);
            break;

        case Love:
            sing(S_cuddly);
            crusaito(3, 1000, 25, 1);
            break;

        case Angry:
            sing(S_OhOoh);
            shakeLeg(3, 400, 1);
            shakeLeg(3, 400, -1);
            break;

        case Fretful:
            sing(S_fart2);
            jitter(5, 300, 25);
            break;

        case Magic:
            sing(S_mode3);
            ascendingTurn(4, 800, 20);
            break;

        case Wave:
            for (int i = 0; i < 3; i++) {
                int pos1[4] = {60, 90, 90, 90};
                _moveServos(200, pos1);
                int pos2[4] = {120, 90, 90, 90};
                _moveServos(200, pos2);
            }
            home();
            break;

        case Victory:
            sing(S_superHappy);
            jump(2, 500);
            dance(3, 600);
            break;

        case Fail:
            sing(S_sad);
            updown(2, 1000, 25);
            delay(500);
            sing(S_confused);
            break;

        default:
            break;
    }
}

// ==================== Sound Functions ====================

void DigiCodeHumanoid::initBuzzer(int pin) {
    _buzzerPin = pin;
    pinMode(_buzzerPin, OUTPUT);
}

void DigiCodeHumanoid::sing(int songName) {
    if (_buzzerPin < 0) return;

    switch (songName) {
        case S_connection:
            _playNote(262, 50);
            _playNote(330, 50);
            _playNote(392, 50);
            _playNote(523, 100);
            break;

        case S_disconnection:
            _playNote(523, 50);
            _playNote(392, 50);
            _playNote(330, 50);
            _playNote(262, 100);
            break;

        case S_buttonPushed:
            _playNote(440, 50);
            break;

        case S_mode1:
            _playNote(330, 100);
            _playNote(392, 100);
            break;

        case S_mode2:
            _playNote(392, 100);
            _playNote(494, 100);
            break;

        case S_mode3:
            _playNote(494, 100);
            _playNote(659, 100);
            break;

        case S_surprise:
            bendTone(800, 2150, 1.02, 10, 1);
            delay(50);
            bendTone(2149, 800, 0.98, 10, 1);
            break;

        case S_OhOoh:
            bendTone(880, 2000, 1.04, 8, 3);
            delay(100);
            bendTone(2000, 880, 0.96, 8, 3);
            break;

        case S_OhOoh2:
            bendTone(1000, 1800, 1.02, 10, 2);
            delay(150);
            bendTone(1800, 1000, 0.98, 10, 2);
            break;

        case S_cuddly:
            bendTone(700, 900, 1.01, 15, 5);
            bendTone(900, 700, 0.99, 15, 5);
            break;

        case S_sleeping:
            bendTone(100, 500, 1.02, 30, 10);
            delay(300);
            bendTone(500, 100, 0.98, 30, 10);
            break;

        case S_happy:
            _playNote(392, 100);
            _playNote(494, 100);
            _playNote(523, 100);
            break;

        case S_superHappy:
            _playNote(523, 100);
            _playNote(659, 100);
            _playNote(784, 100);
            _playNote(1047, 150);
            break;

        case S_happy_short:
            _playNote(523, 75);
            _playNote(659, 75);
            break;

        case S_sad:
            _playNote(330, 200);
            delay(50);
            _playNote(294, 200);
            delay(50);
            _playNote(262, 300);
            break;

        case S_confused:
            _playNote(330, 100);
            delay(30);
            _playNote(392, 100);
            delay(30);
            _playNote(330, 100);
            delay(30);
            _playNote(392, 100);
            break;

        case S_fart1:
            bendTone(200, 50, 0.95, 10, 5);
            break;

        case S_fart2:
            bendTone(150, 40, 0.93, 15, 8);
            break;

        case S_fart3:
            bendTone(180, 30, 0.90, 20, 10);
            break;

        default:
            break;
    }
}

void DigiCodeHumanoid::playTone(int frequency, int duration) {
    if (_buzzerPin < 0) return;
    tone(_buzzerPin, frequency, duration);
    delay(duration);
}

void DigiCodeHumanoid::bendTone(int initFreq, int endFreq, float step, int duration, int silentDuration) {
    if (_buzzerPin < 0) return;

    if (initFreq < endFreq) {
        for (int freq = initFreq; freq < endFreq; freq = (int)(freq * step)) {
            tone(_buzzerPin, freq, duration);
            delay(duration + silentDuration);
        }
    } else {
        for (int freq = initFreq; freq > endFreq; freq = (int)(freq * step)) {
            tone(_buzzerPin, freq, duration);
            delay(duration + silentDuration);
        }
    }
}

void DigiCodeHumanoid::clearBuzzer() {
    if (_buzzerPin >= 0) {
        noTone(_buzzerPin);
    }
}

void DigiCodeHumanoid::_playNote(int frequency, int duration) {
    if (_buzzerPin < 0) return;
    tone(_buzzerPin, frequency, duration);
    delay(duration);
    noTone(_buzzerPin);
}

// ==================== Servo Control Functions ====================

void DigiCodeHumanoid::moveServos(int time, int target[4]) {
    _moveServos(time, target);
}

void DigiCodeHumanoid::_moveServos(int time, int target[4]) {
    int increment[4];
    int current[4];

    for (int i = 0; i < 4; i++) {
        current[i] = _servos[i].getPosition();
        increment[i] = target[i] - current[i];
    }

    unsigned long startTime = millis();
    unsigned long elapsed;

    do {
        elapsed = millis() - startTime;
        float progress = (float)elapsed / (float)time;
        progress = constrain(progress, 0.0, 1.0);

        for (int i = 0; i < 4; i++) {
            int pos = current[i] + (int)(increment[i] * progress);
            _servos[i].setPosition(pos);
        }

        delay(10);
    } while (elapsed < (unsigned long)time);

    // Ensure final position
    for (int i = 0; i < 4; i++) {
        _servos[i].setPosition(target[i]);
    }
}

void DigiCodeHumanoid::oscillate(int amplitude[4], int offset[4], int period, float phase[4]) {
    for (int i = 0; i < 4; i++) {
        _servos[i].setParameters(amplitude[i], offset[i], period, phase[i]);
    }
}

void DigiCodeHumanoid::execute(int amplitude[4], int offset[4], int period, float phase[4], int steps) {
    _execute(amplitude, offset, period, phase, steps);
}

void DigiCodeHumanoid::_execute(int amplitude[4], int offset[4], unsigned long period, float phase[4], int steps) {
    unsigned long startTime = millis();
    unsigned long totalTime = period * steps;

    // Set parameters for all oscillators
    for (int i = 0; i < 4; i++) {
        _servos[i].setParameters(amplitude[i], offset[i], period, phase[i]);
    }

    // Run the oscillation
    while (millis() - startTime < totalTime) {
        for (int i = 0; i < 4; i++) {
            _servos[i].refresh();
        }
        delay(10);
    }

    // Stop all oscillators and return to neutral
    for (int i = 0; i < 4; i++) {
        _servos[i].stop();
    }

    home();
}

void DigiCodeHumanoid::_oscillateServos(int amplitude[4], int offset[4], unsigned long period, float phase[4], float cycle) {
    for (int i = 0; i < 4; i++) {
        float angle = sin(2.0 * PI * cycle + phase[i]);
        int pos = (int)(amplitude[i] * angle) + offset[i] + 90;
        _servos[i].setPosition(pos);
    }
}

// ==================== Utility Functions ====================

int DigiCodeHumanoid::getDistance() {
    // Placeholder - implement with actual ultrasonic sensor
    return 0;
}

int DigiCodeHumanoid::getNoise() {
    // Placeholder - implement with actual sound sensor
    return 0;
}
