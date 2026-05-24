# DigiRover

Layer 5 ground rover API for DigiCode robotics. Dual-mode actuator support (continuous-rotation servo 2-pin / DC motor H-bridge 4-pin) built on `DigiMotion`.

## Modes (mutually exclusive — set once at `init*Mode()` time)

| Mode                 | Channels      | Actuator type                                    |
|----------------------|---------------|--------------------------------------------------|
| `ROVER_SERVO_2PIN`   | 2 channels    | Continuous-rotation servos, 1 per wheel          |
| `ROVER_DC_MOTOR_4PIN`| 4 channels    | H-bridge DC motors, 2 pins per side (A/B)        |

The 4-pin DC motor path was present in the legacy library but unreachable from the Blockly UI; Phase B-2 exposes it as a new block (`rover_init_dc_motor`).

## API

### Lifecycle

- `initServoMode(IActuatorChannel* left, IActuatorChannel* right)` — register the 2 continuous-rotation servo channels.
- `initDcMotorMode(IActuatorChannel* la, IActuatorChannel* lb, IActuatorChannel* ra, IActuatorChannel* rb)` — register the 4 DC motor channels (Left A/B, Right A/B for H-bridge).
- After init, `mode()` returns the selected mode. Re-init resets the mode; cannot mix.

### Velocity commands

- `forward(int speedPercent)` — drive both sides forward at `speedPercent` (0-100).
- `backward(int speedPercent)`.
- `turnLeft(int speedPercent)` — left slower, both forward.
- `turnRight(int speedPercent)`.
- `spinLeft(int speedPercent)` — left backward, right forward (in-place rotation).
- `spinRight(int speedPercent)`.
- `stop()` — both sides to 0.

Velocity commands map differently per mode:

| Mode         | Wheel command         | Channel target                                   |
|--------------|-----------------------|--------------------------------------------------|
| servo 2-pin  | left side forward     | left channel → target=+speedPercent              |
| servo 2-pin  | right side forward    | right channel → target=−speedPercent (mirrored)  |
| DC motor 4-pin | left side forward   | LA→+speedPercent, LB→0                           |
| DC motor 4-pin | left side backward  | LA→0, LB→+speedPercent                           |

(Continuous-rotation servo convention: 0% = stop, ±100% = max forward/back. Channel-side trim adjusts the stop-center.)

### Query

- `isMoving()` / `mode()` / `channelCount()` / `channelAt(int idx)`.

### Per-channel 3-axis (E1)

- `setChannelPulseRange(int idx, int minUs, int maxUs)` — servo-mode only (DC motor channels accept but ignore).
- `setChannelMaxRate(int idx, int unitsPerSec)` — both modes.
- `setChannelTrim(int idx, int trimDeg)` — both modes (servo: deg shift on stop center; DC motor: deadband %).

## License

AGPL-3.0-or-later — original implementation, no upstream GPL derivation.

## Depends

- `DigiMotion` ≥ 1.0.0
