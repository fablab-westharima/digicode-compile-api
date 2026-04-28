/**
 * warmup-pio.ts — Build-time primer that pre-downloads PlatformIO frameworks
 * and lib tarballs into the image, so a fresh container's first compile does
 * not pay for ~120s of framework download + lib_deps fetch.
 *
 * Phase 1 finding (45.md §5.2 + 改定log 第51回 教訓 2):
 *   COMPILE_TIMEOUT_MS=180000 + lazy-DL framework (~600 MB) + lib_deps DL
 *   (~200-300 MB) on a fresh container exceeded the timeout (Mac UAT 1
 *   reproduced @180s SIGTERM). Phase 2 cutover puts ML30 into a fresh docker
 *   container, so the same path would hit the same cliff.
 *
 * Strategy (45.md §3 Q1 = C, primer scope A2):
 *   1. Build a tiny throw-away project that pulls in BOTH platforms and the
 *      same lib_deps mix the runtime uses (DRY via compile.ts buildLibDeps).
 *   2. Run `pio run` once at image build time. PlatformIO downloads:
 *        framework-arduinoespressif32 + framework-arduinopico → /root/.platformio/packages/
 *        all lib tarballs                                      → /root/.platformio/.cache/
 *      These survive in the image since they live under HOME, not a VOLUME.
 *   3. Delete the primer project. Frameworks + tarball cache stay.
 *
 * Compile failures are tolerated — the goal is the populate step, not a
 * shippable artifact. Framework + tarball downloads happen before any source
 * compilation, so a build-phase failure does not undo the cache fill.
 */

import { execSync } from 'node:child_process';
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';

import { buildLibDeps } from '../src/compile.js';

const PRIMER_DIR = '/tmp/digicode-warmup';
const LIBS_DIR = process.env.LIBS_DIR ?? '/opt/digicode-compile/libs';
const PIO_BIN = process.env.PIO_BIN ?? 'pio';

interface PrimerEnv {
  envName: string;
  platform: string;
  board: string;
}

const PRIMER_ENVS: readonly PrimerEnv[] = [
  { envName: 'esp32_primer', platform: 'espressif32', board: 'esp32dev' },
  { envName: 'rp2040_primer', platform: 'raspberrypi', board: 'pico' },
];

const PRIMER_MAIN_CPP = `// Minimal source — primer's job is to fill ~/.platformio/, not produce artifacts.
#include <Arduino.h>

void setup() {}
void loop() {}
`;

function buildPrimerIni(): string {
  return PRIMER_ENVS.map((env) => {
    const libDeps = buildLibDeps(LIBS_DIR, { platform: env.platform })
      .map((dep) => `    ${dep}`)
      .join('\n');
    // lib_ldf_mode=off suppresses the dependency graph scan that causes
    // WiFiServer.cpp.o errors on the rp2040 primer (45.md §5.3.1 finding).
    const extraLines = env.platform === 'raspberrypi' ? 'lib_ldf_mode = off\n' : '';
    return `[env:${env.envName}]
platform = ${env.platform}
board = ${env.board}
framework = arduino
${extraLines}lib_deps =
${libDeps}
`;
  }).join('\n');
}

function main(): void {
  console.log('[warmup-pio] start — pre-DL frameworks + lib tarballs into the image');
  console.log(`[warmup-pio] primer envs: ${PRIMER_ENVS.map((e) => e.envName).join(', ')}`);
  console.log(`[warmup-pio] LIBS_DIR=${LIBS_DIR}, PIO_BIN=${PIO_BIN}`);

  rmSync(PRIMER_DIR, { recursive: true, force: true });
  mkdirSync(path.join(PRIMER_DIR, 'src'), { recursive: true });
  writeFileSync(path.join(PRIMER_DIR, 'platformio.ini'), buildPrimerIni(), 'utf-8');
  writeFileSync(path.join(PRIMER_DIR, 'src', 'main.cpp'), PRIMER_MAIN_CPP, 'utf-8');

  console.log('[warmup-pio] running `pio run` to populate ~/.platformio/...');
  try {
    execSync(`${PIO_BIN} run`, {
      cwd: PRIMER_DIR,
      stdio: 'inherit',
      env: { ...process.env, PLATFORMIO_DISABLE_PROGRESSBAR: 'true' },
    });
    console.log('[warmup-pio] pio run completed cleanly');
  } catch {
    // Compile failure tolerated — framework + lib tarball DL happen before the
    // build phase, so the cache should still be populated even if build fails.
    console.warn('[warmup-pio] pio run returned non-zero; framework/tarball cache should still be populated');
  }

  rmSync(PRIMER_DIR, { recursive: true, force: true });
  console.log('[warmup-pio] done — frameworks + lib tarballs are now in /root/.platformio/');
}

main();
