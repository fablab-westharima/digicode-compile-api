# DigiMotion

Foundation library for DigiCode robotics. Provides actuator abstraction, background pump, motion primitives, NVS trim store, sound/gesture. Layer 0-4 of the 5-layer stack; consumed by `DigiBiped` / `DigiMorpher` / `DigiRover` (Layer 5).

## Status

Phase A-beta skeleton (Session 140). Empty subsystem directories. Concrete implementation lands in subsequent Phases per `plans/active/60_robotics-redesign-implementation-plan.md`.

## Layer structure

```
Layer 5  Robot API           (DigiBiped / DigiMorpher / DigiRover / future DigiArm / DigiQuad)
            uses                                                       (platform-agnostic)
Layer 4  Motion primitives   (SineOscillator / LinearInterpolator / Trajectory)
            writes target via                                          (platform-agnostic)
Layer 3  ChannelBank         (N-channel grouping, phase sync, wait barrier)
            owns                                                       (platform-agnostic)
Layer 2  IActuatorChannel    (abstract; holds pulse + speed + trim 3 axes)
            pumped by                                                  (abstract IF; platform-agnostic above this line)
Layer 1  IBackgroundPump     (abstract pump contract: start / stop / register / tick)
            implemented per-platform                                   <-- platform boundary
Layer 0  Platform HAL        (ESP32: FreeRTOS + LEDC + ESP32Servo; future RP2040: SDK alarm + PIO PWM + Arduino-Pico Servo)
```

## Subsystem directories

| Path | Phase | Purpose |
|---|---|---|
| `src/actuator/` | A-gamma | `IActuatorChannel` + 6 concrete channels (ServoChannel180/270, ContinuousServoChannel, StepperPollChannel, StepperHwChannel, DcMotorChannel) |
| `src/pump/` | A-gamma | `IBackgroundPump` + ESP32 FreeRTOS implementation |
| `src/motion/` | A-delta | `ChannelBank` + `SineOscillator` + `LinearInterpolator` |
| `src/trim/` | A-epsilon | `ITrimStore` + NVS-backed implementation (namespace `servo_trim_v2`) |
| `src/sound/` | A-epsilon-2 | `IBuzzer` + `DigiBuzzer` + `GestureLibrary` |

## Origin declaration

This library is an **original implementation**. It is **not** a derivative work of `OttoDIY/OttoDIYLib` or any other GPL/AGPL upstream. All identifiers, public APIs, algorithms, motion patterns, gesture sets, sound presets, EEPROM/NVS layouts, and helper-function signatures are independent designs.

The previous `DigiCodeHumanoid` / `DigiCodeTransform` / `DigiCodeWheel` libraries (declared MIT in their headers) were derivative works of OttoDIYLib and are being replaced by this redesign; they will be removed in Phase A-eta. This library shares no source with them.

`DigiMotion`'s OttoDIY-non-derivation is verified per-commit via a grep gate on 12 identifier patterns (gesture 13, sound 19, Otto class API 20+, Oscillator class shape, EEPROM trim algorithm, `_moveServos` / `_execute` signatures, `playGesture` switch-case mapping, MIT header text, legacy lib instance names, `otto*` store fields). See `plans/active/60_robotics-redesign-implementation-plan.md` Section 4 (T5 12-item checklist) for the grep patterns and the per-Phase verification gates.

## License

GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later). See `LICENSE`.

## Dependencies

- `ESP32Servo` (LGPL-2.1+) — Layer 0 platform HAL on ESP32; consumed only by `src/actuator/ServoChannel180_esp32.cpp` and siblings (added in Phase A-gamma). AGPL-3.0 is compatible with LGPL-2.1+.

Additional dependency added in Phase A-gamma: `gin66/FastAccelStepper@^0.32` (MIT) for HW-peripheral stepper driving — see `plans/active/60_robotics-redesign-implementation-plan.md` Section 8 for the rule-15 four-axis verification record.

## Testing

Host-side cpp unit tests via PlatformIO native env + GoogleTest. See `test/platformio.ini`. From `libs/DigiMotion/test/`, run: `pio test -e native -f test_hello` (PIO filter matches test-directory name, which is `test_<descriptive>` per PIO convention; the file inside is `hello_test.cpp`). Future subsystem tests follow the same pattern: `test/test_<subsystem>/<descriptive>.cpp` + filter `-f test_<subsystem>`.
