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
  // M5Stack
  'm5stack:esp32:m5stack_core': { platform: PIOARDUINO_PLATFORM, board: 'm5stack-core-esp32' },
  // M5StickC Plus has no espressif32 board ID; fall back to base m5stick-c.
  // User-side M5StickCPlus library handles screen/battery diff at runtime.
  'm5stack:esp32:m5stick_c_plus': { platform: PIOARDUINO_PLATFORM, board: 'm5stick-c' },
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
  // RP2040 (official platformio/raspberrypi platform — pioarduino does not
  // cover RP2040 cores).
  'rp2040:rp2040:rpipico': { platform: 'raspberrypi', board: 'pico' },
  'rp2040:rp2040:rpipicow': { platform: 'raspberrypi', board: 'pico' },
  'rp2040:rp2040:seeed_xiao_rp2040': { platform: 'raspberrypi', board: 'seeed_xiao_rp2040' },
  'arduino:mbed_nano:nanorp2040connect': {
    platform: 'raspberrypi',
    board: 'pico',
    // arduino-mbed core has no PIO equivalent yet; falling back to rp2040 core.
    // Real Nano RP2040 Connect support is a Phase 2+ task (see 44-A §6).
  },
};

/**
 * True for any ESP32-family target. After BUG-059 X2 unification every
 * ESP32 board uses the pioarduino URL string, but we keep the
 * `espressif32` literal in the predicate so the helper survives a future
 * decision to mix in the official platform again.
 */
export function isEsp32FamilyPlatform(platform: string): boolean {
  return platform === 'espressif32' || platform.includes('pioarduino');
}

export function isEsp32(fqbn: string): boolean {
  const target = FQBN_TO_PIO[fqbn];
  return target ? isEsp32FamilyPlatform(target.platform) : false;
}

export function pioTargetFor(fqbn: string): PioTarget {
  const target = FQBN_TO_PIO[fqbn];
  if (!target) {
    throw new Error(
      `Unsupported FQBN "${fqbn}". Add a mapping to compile-api/src/boards.ts.`,
    );
  }
  return target;
}
