/**
 * boards.ts — arduino-cli FQBN → PlatformIO platform/board mapping.
 *
 * Mirrors `SUPPORTED_BOARDS` in
 * `variants/ota/frontend/src/stores/boardStore.ts` and the legacy
 * `arduino-compile-server` board handling. Adding a board = appending an
 * entry here (CRUD via SSH file edit + container restart, no rebuild needed).
 */

/**
 * BUG-059 deferred (2026-04-30, post-rollback).
 *
 * The "C6-only pioarduino" approach proved structurally impossible: the
 * pioarduino fork's platform.json declares `"name": "espressif32"`, the
 * same name as the official platform. PIO's package store keys platforms
 * by name, so installing pioarduino *replaces* the official espressif32
 * registration globally — and pioarduino's builder imports `yaml` (not in
 * the container's Python deps), causing every ESP32 compile to fail with
 * `ModuleNotFoundError: No module named 'yaml'` (commit f98274a deploy
 * confirmed this regression). Build #6 image was rolled back.
 *
 * Re-architecture options live in `prompt/maintenance/発見バグ/2026-04-29_059_*.md`:
 *   X1 — fork + rename pioarduino's platform name; high maintenance.
 *   X2 — full arduino-esp32 v3.x migration; ~4–8 h, every ESP32 board
 *        needs regression coverage (vendored libs untested vs. v3.x).
 *   X3 — drop C6 / xiao-esp32c6 from boardStore (this commit hides them
 *        from the picker so users do not see broken hardware) and
 *        revisit when one of X1 / X2 is feasible.
 *
 * Until then, C6 entries route to the official `espressif32` (same as
 * before BUG-059) so existing builds stay green; the C6 boards
 * themselves are hidden in the frontend (`stores/boardStore.ts`).
 */

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
  // ESP32-C6: official `espressif32@6.13.0` ships arduino-esp32 v2.x which
  // does NOT support C6. Compiles fail at PIO env validation with "This
  // board doesn't support arduino framework!". Frontend hides this board
  // (BUG-059 X3) so users never select it; the mapping stays here so any
  // probabilistic-debug case still resolves through pioTargetFor() and
  // exercises the existing failure path.
  'esp32:esp32:esp32c6': { platform: 'espressif32', board: 'esp32-c6-devkitm-1' },
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
