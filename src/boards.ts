/**
 * boards.ts — arduino-cli FQBN → PlatformIO platform/board mapping.
 *
 * Mirrors `SUPPORTED_BOARDS` in
 * `variants/ota/frontend/src/stores/boardStore.ts` and the legacy
 * `arduino-compile-server` board handling. Adding a board = appending an
 * entry here (CRUD via SSH file edit + container restart, no rebuild needed).
 */

/**
 * pioarduino unification (BUG-059 X2, 2026-04-30).
 *
 * Every ESP32 target now routes through the pioarduino fork
 * (`platform-espressif32` tracking arduino-esp32 v3.x). The fork's
 * `platform.json` declares `name=espressif32`, which means PIO can only
 * register one of the two — picking pioarduino as the canonical
 * `espressif32` keeps the package store consistent and unlocks ESP32-C6 /
 * C5 / H2 / P4 support without a fork+rename hack.
 *
 * Tag pin: `54.03.21` (Arduino v3.2.1 / ESP-IDF v5.4.2). Same tag M5Stack
 * uses for Stamp-P4 (https://docs.m5stack.com/en/core/Stamp-P4); selecting
 * a tested-by-M5Stack version reduces drift relative to the commercial
 * lineup we're aligning with for the Factory Scientist course
 * (memory:factory_scientist_course).
 *
 * URL form: GitHub release zip URL, what pioarduino's docs document and
 * what `platform = <url>` in platformio.ini consumes. `pio platform
 * install <url>` is broken on this URL form (build #4/#5 both failed); the
 * lazy-install path via warmup-pio.ts works. See compile-api/Dockerfile.
 *
 * Risks owners should remember when bumping this tag:
 *   - arduino-esp32 v3.x rewrote the ledc API. ESP32Servo and the
 *     DigiCodeHumanoid / DigiCodeTransform / DigiCodeWheel vendored libs
 *     all touch ledc; verify with humanoid_init + humanoid_walk smoke.
 *   - NimBLE-Arduino v2.4.0 (vendored) supports v3.x but our callback
 *     signatures (BLE v2 callback drift, BUG-066 candidate) are not
 *     production-tested.
 *   - WiFi.mode / hostname / setSleep behavior diverges between v2 and
 *     v3; OTA + MQTT + HA paths need a smoke pass.
 */
const PIOARDUINO_PLATFORM =
  'https://github.com/pioarduino/platform-espressif32/releases/download/54.03.21/platform-espressif32.zip';

export interface PioTarget {
  /** PlatformIO `platform` field. */
  platform: string;
  /** PlatformIO `board` ID under that platform. */
  board: string;
  /** Optional board-specific build flags, appended to the env's build_flags line. */
  extraBuildFlags?: string[];
}

export const FQBN_TO_PIO: Record<string, PioTarget> = {
  // ESP32 generic (pioarduino-driven, arduino-esp32 v3.x)
  'esp32:esp32:esp32': { platform: PIOARDUINO_PLATFORM, board: 'esp32dev' },
  'esp32:esp32:esp32s3': { platform: PIOARDUINO_PLATFORM, board: 'esp32-s3-devkitc-1' },
  'esp32:esp32:esp32c3': { platform: PIOARDUINO_PLATFORM, board: 'esp32-c3-devkitm-1' },
  'esp32:esp32:esp32c6': { platform: PIOARDUINO_PLATFORM, board: 'esp32-c6-devkitm-1' },
  // M5Stack — Basic / Gray / Fire share the same pinout, so we route all of
  // them through pioarduino's `m5stack-fire` board id. The more obvious
  // `m5stack-core-esp32` board.json declares `variant=m5stack_core_esp32`,
  // but pioarduino's variants/ directory only ships `m5stack_core` (no
  // `_esp32` suffix); compiles fail with `pins_arduino.h: No such file or
  // directory`. m5stack-fire's variant `m5stack_fire` exists and uses the
  // same physical pin assignments — only PSRAM / IMU presence differs at
  // runtime, which is irrelevant for compile correctness.
  'm5stack:esp32:m5stack_core': { platform: PIOARDUINO_PLATFORM, board: 'm5stack-fire' },
  // M5StickC Plus has no espressif32 board ID. The obvious fallback
  // `m5stick-c` declares `variant=m5stick_c`, but pioarduino's
  // framework-arduinoespressif32 ships no `m5stick_c` variant directory
  // (variants/ contains only `m5stack_*` and a few unrelated stick-named
  // boards), so compiles fail with `pins_arduino.h: No such file or
  // directory` (BUG-064, 1000-case cluster #12, 2026-04-30). Route through
  // `m5stack-atom` instead — same ESP32-PICO-D4 MCU as M5StickC Plus, same
  // route already used for ATOM Lite / Matrix above, and `m5stack_atom`
  // variant exists. M5StickCPlus user library abstracts screen/IMU/battery
  // diff at runtime, so the M5Stack_ATOM build flag is harmless for
  // compile correctness; physical hardware UAT pending user device.
  'm5stack:esp32:m5stick_c_plus': { platform: PIOARDUINO_PLATFORM, board: 'm5stack-atom' },
  'm5stack:esp32:atom_lite': { platform: PIOARDUINO_PLATFORM, board: 'm5stack-atom' },
  'm5stack:esp32:atom_matrix': { platform: PIOARDUINO_PLATFORM, board: 'm5stack-atom' },
  'm5stack:esp32:stamp_pico': { platform: PIOARDUINO_PLATFORM, board: 'm5stamp-pico' },
  // ATOMS3 Lite (BUG-059 extension, 2026-04-29) — ESP32-S3FN8, no display,
  // RGB LED. M5Stack's own platformio.ini documentation routes this product
  // through the generic `esp32-s3-devkitc-1` board def with USB-CDC build
  // flags so Serial.print() lands on the USB port instead of HW UART. The
  // `m5stack-atoms3` PIO board def exists but targets the AtomS3 (with
  // 0.85" display); the Lite variant uses the same MCU/flash so the devkit
  // board def is the safer choice.
  'm5stack:esp32:m5stack_atoms3': {
    platform: PIOARDUINO_PLATFORM,
    board: 'esp32-s3-devkitc-1',
    extraBuildFlags: [
      '-DARDUINO_USB_MODE=1',
      '-DARDUINO_USB_CDC_ON_BOOT=1',
    ],
  },
};

export function pioTargetFor(fqbn: string): PioTarget {
  const target = FQBN_TO_PIO[fqbn];
  if (!target) {
    throw new Error(
      `Unsupported FQBN "${fqbn}". Add a mapping to compile-api/src/boards.ts.`,
    );
  }
  return target;
}
