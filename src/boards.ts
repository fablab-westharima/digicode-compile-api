/**
 * boards.ts — arduino-cli FQBN → PlatformIO platform/board mapping.
 *
 * Mirrors `SUPPORTED_BOARDS` in
 * `variants/ota/frontend/src/stores/boardStore.ts` and the legacy
 * `arduino-compile-server` board handling. Adding a board = appending an
 * entry here (CRUD via SSH file edit + container restart, no rebuild needed).
 */

export interface PioTarget {
  /** PlatformIO `platform` field. */
  platform: string;
  /** PlatformIO `board` ID under that platform. */
  board: string;
  /** Optional extra `build_flags`, `lib_deps`, etc. */
  extra?: Record<string, string>;
}

export const FQBN_TO_PIO: Record<string, PioTarget> = {
  // ESP32 generic
  'esp32:esp32:esp32': { platform: 'espressif32', board: 'esp32dev' },
  'esp32:esp32:esp32s3': { platform: 'espressif32', board: 'esp32-s3-devkitc-1' },
  'esp32:esp32:esp32c3': { platform: 'espressif32', board: 'esp32-c3-devkitm-1' },
  'esp32:esp32:esp32c6': { platform: 'espressif32', board: 'esp32-c6-devkitm-1' },
  // M5Stack
  'm5stack:esp32:m5stack_core': { platform: 'espressif32', board: 'm5stack-core-esp32' },
  // M5StickC Plus has no PIO espressif32 board ID; fall back to base m5stick-c.
  // User-side M5StickCPlus library handles screen/battery diff at runtime.
  'm5stack:esp32:m5stick_c_plus': { platform: 'espressif32', board: 'm5stick-c' },
  'm5stack:esp32:atom_lite': { platform: 'espressif32', board: 'm5stack-atom' },
  'm5stack:esp32:atom_matrix': { platform: 'espressif32', board: 'm5stack-atom' },
  'm5stack:esp32:stamp_pico': { platform: 'espressif32', board: 'm5stamp-pico' },
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

export function isEsp32(fqbn: string): boolean {
  const target = FQBN_TO_PIO[fqbn];
  return target?.platform === 'espressif32';
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
