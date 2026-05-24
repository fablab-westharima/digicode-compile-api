# DigiMorpher

Layer 5 transformable (walk / roll dual-mode) robot API for DigiCode robotics. 4-servo morphing robot built on `DigiMotion` Layer 0-4 foundation.

## Modes

| Mode         | Phys config        | Locomotion           |
|--------------|--------------------|----------------------|
| `MORPH_WALK` | Legs vertical      | walk / turn (gait)   |
| `MORPH_ROLL` | Legs horizontal    | roll / rollRotate    |

`shiftBlocking(mode)` / `shiftAsync(mode)` physically transforms between the two. Walk-mode motions are no-op (or rejected) in roll mode and vice versa.

## Channels

| Index | Const         | Notes                                |
|-------|---------------|--------------------------------------|
| 0     | `LEFT_HIP`    | Left hip (rotates leg vertical ↔ horizontal) |
| 1     | `RIGHT_HIP`   | Right hip                            |
| 2     | `LEFT_FOOT`   | Left ankle servo                     |
| 3     | `RIGHT_FOOT`  | Right ankle servo                    |

## API summary

- `attachChannels(...)` / `init()` / `initWithTrim(...)` — same surface as `DigiBiped`.
- `setMode(MorphMode mode)` — state flag only, no motion.
- `shiftBlocking(MorphMode targetMode)` / `shiftAsync(...)` — physical transformation.
- Walk-mode motions: `walkBlocking/Async`, `turnBlocking/Async`, `pushupBlocking/Async`, `danceBlocking/Async`.
- Roll-mode motions: `rollBlocking/Async`, `rollRotateBlocking/Async`.
- `homeBlocking()` — return channels to home for the current mode.
- `stop()`, `isIdle()`, `currentMotion()`, `tick(nowMs)`, `waitUntilIdle()`.
- Per-channel 3-axis (E1): `setChannelPulseRange/MaxRate/Trim`.

## Motion patterns

Same discipline as `DigiBiped`: walk / turn / roll / pushup / dance motion patterns are new amplitude/phase designs distinct from `OttoDIYLib` Otto-transform algorithms. T5 §3 (Otto API method name pattern) gate enforces zero hits across the source tree.

## License

AGPL-3.0-or-later — original implementation, no upstream GPL derivation.

## Depends

- `DigiMotion` ≥ 1.0.0
