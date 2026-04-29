/**
 * boards.ts — arduino-cli FQBN → PlatformIO platform/board mapping.
 *
 * Mirrors `SUPPORTED_BOARDS` in
 * `variants/ota/frontend/src/stores/boardStore.ts` and the legacy
 * `arduino-compile-server` board handling. Adding a board = appending an
 * entry here (CRUD via SSH file edit + container restart, no rebuild needed).
 */

/**
 * pioarduino fork pin (BUG-059, 2026-04-29).
 *
 * The official `platformio/espressif32@6.13.0` ships arduino-esp32 v2.0.17,
 * which does NOT support ESP32-C6 / C5 / H2 / P4 with the arduino framework
 * (PIO env validation rejects with "This board doesn't support arduino
 * framework!"). The community fork `pioarduino/platform-espressif32` tracks
 * arduino-esp32 v3.x and adds those SoCs.
 *
 * Tag `54.03.21` (Arduino v3.2.1 / ESP-IDF v5.4.2) was selected over the
 * latest `55.03.38-1` because:
 *   - M5Stack's official Stamp-P4 docs pin to this exact tag (production-
 *     tested for Factory Scientist course requirements; see
 *     https://docs.m5stack.com/en/core/Stamp-P4)
 *   - v3.2.1 fully covers C6 (cluster #14, 14 cases / +1.4pp) without
 *     incurring v3.3.x changes that have not been tested against the
 *     vendored libs (NimBLE-Arduino v2.4.0, DigiCodeHumanoid, ...).
 *
 * URL format: pioarduino's own install instructions use the GitHub release
 * zip URL — this is the form `pio platform install` accepts on the CLI and
 * the `platform = ...` line in platformio.ini both consume. The earlier
 * `git+url#tag` form worked in platformio.ini but failed `pio platform
 * install` (build-time pre-install in Dockerfile blew up on this).
 *
 * Scoped to C6 family only — every other ESP32 board stays on the official
 * `espressif32` platform so existing builds are unchanged.
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
  // ESP32 generic (official platformio/espressif32 — arduino-esp32 v2.x)
  'esp32:esp32:esp32': { platform: 'espressif32', board: 'esp32dev' },
  'esp32:esp32:esp32s3': { platform: 'espressif32', board: 'esp32-s3-devkitc-1' },
  'esp32:esp32:esp32c3': { platform: 'espressif32', board: 'esp32-c3-devkitm-1' },
  // ESP32-C6 routes through the pioarduino fork (BUG-059) — the official
  // platform's arduino-esp32 v2.x does not support C6.
  'esp32:esp32:esp32c6': { platform: PIOARDUINO_PLATFORM, board: 'esp32-c6-devkitm-1' },
  // NOTE (deferred): a Seeed-specific FQBN `seeed:esp32:xiao_esp32c6` →
  // PIO board id `seeed_xiao_esp32c6` would let xiao-esp32c6 hit the
  // Seeed-tuned pin defaults. Currently `boardStore.ts:203` aliases
  // xiao-esp32c6 to the same `esp32:esp32:esp32c6` FQBN, so the C6 fix
  // here covers both. Splitting the FQBN is a separate Phase.
  // M5Stack
  'm5stack:esp32:m5stack_core': { platform: 'espressif32', board: 'm5stack-core-esp32' },
  // M5StickC Plus has no PIO espressif32 board ID; fall back to base m5stick-c.
  // User-side M5StickCPlus library handles screen/battery diff at runtime.
  'm5stack:esp32:m5stick_c_plus': { platform: 'espressif32', board: 'm5stick-c' },
  'm5stack:esp32:atom_lite': { platform: 'espressif32', board: 'm5stack-atom' },
  'm5stack:esp32:atom_matrix': { platform: 'espressif32', board: 'm5stack-atom' },
  'm5stack:esp32:stamp_pico': { platform: 'espressif32', board: 'm5stamp-pico' },
  // ATOMS3 Lite (BUG-059 extension, 2026-04-29) — ESP32-S3FN8, no display,
  // RGB LED. M5Stack's own platformio.ini documentation routes this product
  // through the generic `esp32-s3-devkitc-1` board def with USB-CDC build
  // flags so Serial.print() lands on the USB port instead of HW UART. The
  // `m5stack-atoms3` PIO board def exists but targets the AtomS3 (with
  // 0.85" display); the Lite variant uses the same MCU/flash so the devkit
  // board def is the safer choice.
  'm5stack:esp32:m5stack_atoms3': {
    platform: 'espressif32',
    board: 'esp32-s3-devkitc-1',
    extraBuildFlags: [
      '-DARDUINO_USB_MODE=1',
      '-DARDUINO_USB_CDC_ON_BOOT=1',
    ],
  },
  // RP2040
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
 * True for any ESP32-family target — official `espressif32` and the
 * `pioarduino` fork URL pin both qualify. Used by compile.ts to scope
 * ESP32-only lib_deps and the 4-file fullPackage bundle.
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
