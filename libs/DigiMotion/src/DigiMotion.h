/*
 * DigiMotion - Foundation library for DigiCode robotics
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for the full terms.
 *
 * Origin declaration: this library is an original implementation.
 * It is NOT derived from OttoDIY/OttoDIYLib (GPL-3.0) or any other
 * GPL/AGPL upstream. All identifiers, APIs, algorithms, and constants
 * are independent designs.
 *
 * Umbrella header (Phase X-1 expand). Includes every public subsystem
 * IF so generator emit / Layer 5 consumers can write a single
 * #include <DigiMotion.h>. Platform-specific concrete classes
 * (NvsTrimStore, DigiBuzzer) live in *_esp32.cpp and are reached
 * through the getTrimStore() / getBuzzer() singleton accessors
 * declared in their IF headers -- the umbrella does not surface
 * those concretes (intentional Platform HAL abstraction).
 */

#ifndef DIGIMOTION_H
#define DIGIMOTION_H

#include <pump/IBackgroundPump.h>
#include <actuator/IActuatorChannel.h>
#include <actuator/ServoChannel180.h>
#include <actuator/ServoChannel270.h>
#include <actuator/ContinuousServoChannel.h>
#include <actuator/DcMotorChannel.h>
#include <actuator/StepperPollChannel.h>
#include <actuator/StepperHwChannel.h>
#include <motion/ChannelBank.h>
#include <motion/SineOscillator.h>
#include <motion/LinearInterpolator.h>
#include <trim/ITrimStore.h>
#include <sound/SoundPresetTable.h>
#include <sound/IBuzzer.h>
#include <sound/GestureLibrary.h>

#endif // DIGIMOTION_H
