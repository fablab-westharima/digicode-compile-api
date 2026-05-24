/*
 * DigiMotion - IActuatorChannel abstract (Layer 2)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. The 3-axis (pulse / speed /
 * trim) integration is the case 23 incident A structural defense per
 * judgment-mistakes-history; the IF surface is new.
 *
 * Platform abstraction (59.md §1.0.1): this header has no Arduino.h or
 * freertos/ includes. Concrete channel headers conditionally include their
 * HW deps under #ifdef ARDUINO_ARCH_ESP32 so Layer 3+ consumers may depend
 * on this IF without dragging ESP32 headers into the build.
 */

#ifndef DIGIMOTION_ACTUATOR_IACTUATORCHANNEL_H
#define DIGIMOTION_ACTUATOR_IACTUATORCHANNEL_H

#include "../pump/IBackgroundPump.h"  // IPumpable

// One physical actuator (servo / stepper / DC motor) wrapped behind a
// uniform 3-axis settings API: pulse range (us) + max rate + trim offset.
// All write paths through this IF observe the 3-axis settings — case 23
// incident A structural defense.
//
// Unit interpretation per concrete channel:
//   ServoChannel180 / 270  : target=deg, maxRate=deg/sec, trim=deg
//   ContinuousServoChannel : target=%, maxRate=%/sec (accel), trim=% (center)
//   StepperPollChannel     : target=steps, maxRate=step/sec, trim=step offset
//   StepperHwChannel       : target=steps, maxRate=step/sec, trim=step offset
//   DcMotorChannel         : target=%, maxRate=%/sec (accel), trim=% (deadband)
//
// pulse range only meaningful for servo families; stepper / DC motor channels
// accept the call but ignore it (documented per-channel).
class IActuatorChannel : public IPumpable {
public:
    virtual ~IActuatorChannel() = default;

    // === lifecycle ===
    virtual bool attach() = 0;
    virtual void detach() = 0;
    virtual bool isAttached() const = 0;

    // === target control ===
    virtual void setTarget(long target) = 0;
    virtual long getTarget() const = 0;
    virtual long getCurrent() const = 0;
    virtual bool hasReachedTarget() const = 0;

    // === 3-axis settings (case 23 incident A structural integration) ===
    virtual void setPulseRange(int minUs, int maxUs) = 0;
    virtual void setMaxRate(int unitsPerSec) = 0;  // 0 = unlimited
    virtual void setTrim(int offset) = 0;

    virtual int getPulseMin() const = 0;
    virtual int getPulseMax() const = 0;
    virtual int getMaxRate() const = 0;
    virtual int getTrim() const = 0;

    // === pump() inherited from IPumpable ===
    // Implementations check attach state + rate cap, advance _current toward
    // _target by one unit, then call the platform-specific HW write.
    //
    // === isActive() inherited from IPumpable ===
    // Returns true iff attached AND current != target. Idle channels are
    // skipped by the pump scan, keeping each tick cheap.

    // === test/diagnostic observation ===
    // Last value (post-trim, post-clamp) written to the HW. Captured even on
    // native (where the actual HW call is a no-op) so unit tests can verify
    // trim + clamp behavior without subclassing.
    virtual int getLastWrittenHw() const = 0;
};

#endif // DIGIMOTION_ACTUATOR_IACTUATORCHANNEL_H
