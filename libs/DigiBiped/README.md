# DigiBiped

Layer 5 biped (humanoid) robot API for DigiCode robotics. 4-servo humanoid built on `DigiMotion` Layer 0-4 foundation.

## Channels

| Index | Const         | Default semantic                  |
|-------|---------------|-----------------------------------|
| 0     | `LEFT_LEG`    | Left hip / thigh servo            |
| 1     | `RIGHT_LEG`   | Right hip / thigh servo           |
| 2     | `LEFT_FOOT`   | Left ankle servo                  |
| 3     | `RIGHT_FOOT`  | Right ankle servo                 |

This indexing is the **single source of truth** for the UI label ↔ lib const reconciliation (case 23 incident D defense per `rules/common/18-setting-hw-reflection-integrity.md` §Discipline 3). `ServoTrimDialog` preset labels in the monorepo frontend (Phase C) and the `pinPresetStore` field names (Phase B-1) MUST follow this mapping.

## API

### Lifecycle

- `attachChannels(IActuatorChannel* ll, IActuatorChannel* rl, IActuatorChannel* lf, IActuatorChannel* rf)` — register the 4 channels with the internal bank. Channel lifetime is the caller's; bank does not own.
- `init()` — `attach()` all 4 channels via `IActuatorChannel`.
- `initWithTrim(ITrimStore& store, int pinLL, int pinRL, int pinLF, int pinRF)` — `init()` + apply per-pin trim from the store via `ITrimStore::applyToChannel()`. Boot-time lifecycle (Session 143 case `c`); runtime overrides via `setChannelTrim`.

### Per-channel 3-axis (E1 case 23 incident A defense)

- `setChannelPulseRange(int idx, int minUs, int maxUs)`
- `setChannelMaxRate(int idx, int unitsPerSec)`
- `setChannelTrim(int idx, int trimDeg)`

Forward to the indexed channel's `IActuatorChannel::set*` method.

### Motion (D8: `walk()` 廃止、Blocking/Async 明示)

For each motion: `<motion>Blocking(...)` and `<motion>Async(...)`. Blocking variants poll internally until idle (ESP32 only — on host tests, blocking is a no-op + caller drives `tick(nowMs)`). Async variants set motion state and return; caller polls `tick(nowMs)` and `isIdle()`.

- `homeBlocking()` / `homeAsync(unsigned long nowMs)` — return all 4 channels to 90°.
- `walkBlocking(steps, direction, speedDegPerSec)` / `walkAsync(...)`.
- `turnBlocking(steps, direction, speedDegPerSec)` / `turnAsync(...)`.
- `jumpBlocking(speedDegPerSec)` / `jumpAsync(...)`.
- `danceBlocking(cycles, speedDegPerSec)` / `danceAsync(...)`.
- `swingBlocking(cycles, speedDegPerSec)` / `swingAsync(...)`.
- `bendBlocking(direction, speedDegPerSec)` / `bendAsync(...)`.
- `moonwalkBlocking(cycles, speedDegPerSec)` / `moonwalkAsync(...)`.
- `stop()` — halt motion + return to home.

### Query

- `isIdle()` / `currentMotion()` / `channelCount()` / `channelAt(int idx)`.
- `tick(unsigned long nowMs)` — advance async motion. Caller invokes from the platform's background pump (ESP32: `getBackgroundPump()`).
- `waitUntilIdle()` — ESP32-only polling helper.

## Motion patterns

Motion shapes (amplitude / phase per channel) are defined as private constants inside `DigiBiped.h`. **All values and structures are new — they are intentionally numerically and structurally distinct from `OttoDIYLib` Otto::walk's hardcoded arrays** (e.g. `int amplitude[4] = {30, 30, 20, 20}`). The 7 motion patterns (walk / turn / jump / dance / swing / bend / moonwalk) use 4-distinct-phase rotations or symmetric pair groupings that differ from Otto's pair-aligned 2-phase pattern. See `T5` 12-item grep gate in `plans/active/60_robotics-redesign-implementation-plan.md` §4.

## License

AGPL-3.0-or-later — see `LICENSE` file. Original implementation; not derived from OttoDIY/OttoDIYLib (GPL-3.0) or any other GPL/AGPL upstream.

## Depends

- `DigiMotion` ≥ 1.0.0 (Layer 0-4 foundation: actuator abstraction, background pump, channel bank, sine oscillator, linear interpolator, trim store)
