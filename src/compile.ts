/**
 * compile.ts — Core compile flow.
 *
 * Phase 3 architecture (44.md §5.4):
 *   1. Resolve FQBN → PlatformIO target (boards.ts)
 *   2. Load .ino template + inject user code (inject.ts)
 *   3. Compute SHA-256 cache key (cache.ts)
 *   4. Cache lookup → HIT returns immediately (~50 ms)
 *   5. Cache MISS: serialize per-(board × template) via withLock (projectLock.ts)
 *   6. Inside lock:
 *      a. Ensure persistent project dir (projectStore.ts) with current
 *         platformio.ini and write src/main.ino
 *      b. Run `pio run` — `.pio/build/<env>/` is preserved across requests so
 *         source-only edits go through PIO's incremental build path (~2-3 s)
 *      c. Read firmware.bin (+ bootloader/partitions/boot_app0 for ESP32
 *         fullPackage)
 *      d. Cache the result (cache.ts)
 *   7. Return result. The persistent project is never deleted.
 */

import { exec } from 'node:child_process';
import { promisify } from 'node:util';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';

import { isEsp32FamilyPlatform, pioTargetFor } from './boards.js';
import { cacheGet, cachePut, computeCacheKey } from './cache.js';
import { injectUserCode, templateNameFor, type ConnectionType } from './inject.js';
import { ensurePersistentProject, projectKey, writeMainIno } from './projectStore.js';
import { withLock } from './projectLock.js';

const execP = promisify(exec);

export interface CompileRequest {
  includes?: string;
  globals?: string;
  setupCode?: string;
  loopCode?: string;
  board: string;
  connectionType?: ConnectionType;
}

export interface CompileSuccess {
  success: true;
  firmware: string; // base64
  bootloader?: string; // base64, ESP32 fullPackage only
  partitions?: string; // base64, ESP32 fullPackage only
  bootApp0?: string; // base64, ESP32 fullPackage only
  /** Wall time of this request. For cache hits this is the lookup latency. */
  durationMs: number;
  template: string;
  pioBoard: string;
  /** True when the result was served from disk cache. Optional, off by default. */
  cached?: boolean;
}

export interface CompileFailure {
  success: false;
  error: string;
  details?: string;
  stderr?: string;
  durationMs: number;
}

export type CompileResult = CompileSuccess | CompileFailure;

export interface CompileEnv {
  /** Path to `pio` binary. Default: `pio` (in PATH). */
  pioBin: string;
  /** PlatformIO core home (for boot_app0.bin path resolution). */
  pioHome: string;
  /** Where .ino templates live (volume-mounted, see 44.md §3.7). */
  templatesDir: string;
  /** Where DigiCode custom + bundled libs live (volume-mounted, see 44.md §3.6). */
  libsDir: string;
  /** Where persistent per-(board × template) PIO projects live (Phase 3). */
  projectsDir: string;
  /** Where the compile-result blob cache lives (Phase 3). */
  cacheDir: string;
  /** Per-attempt compile timeout in ms. */
  timeoutMs: number;
  /** Whether the request asked for ESP32 fullPackage (4-file bundle). */
  fullPackage: boolean;
}

const DEFAULT_ENV: CompileEnv = {
  pioBin: process.env.PIO_BIN ?? 'pio',
  pioHome: process.env.PIO_HOME ?? path.join(process.env.HOME ?? '', '.platformio'),
  templatesDir: process.env.TEMPLATES_DIR ?? '/opt/digicode-compile/templates',
  libsDir: process.env.LIBS_DIR ?? '/opt/digicode-compile/libs',
  projectsDir: process.env.PROJECTS_DIR ?? '/opt/digicode-compile/projects',
  cacheDir: process.env.CACHE_DIR ?? '/opt/digicode-compile/cache',
  timeoutMs: Number(process.env.COMPILE_TIMEOUT_MS ?? 180_000),
  fullPackage: false,
};

/**
 * Platform-aware lib_deps for the PlatformIO project. Mirrors the legacy
 * `arduino-compile-server/Dockerfile` install list plus 2 latent-bug fixes
 * (QTRSensors, MFRC522_I2C — referenced by frontend block generators but
 * never installed in the legacy Dockerfile, so those blocks failed to build
 * on the legacy backend).
 *
 * The split is necessary because some libs declare `architectures=*` in
 * library.properties despite being platform-specific (e.g. WiFiNINA shadows
 * ESP32 native WiFi.h, breaking ESP32 builds). Listing them only in matching
 * envs avoids that.
 *
 * Categories:
 *   - file://${libsDir}/<name>  : volume-mounted (custom + version-pinned bundles)
 *   - <owner>/<name>@<semver>   : PIO registry
 *   - https://github.com/...    : git URL pin (registry-absent libs)
 *
 * See 44-A_library-compat-table.md for full sourcing decisions.
 */

/** Truly portable libs (compile on every framework=arduino platform). */
const COMMON_REGISTRY_LIBS: readonly string[] = [
  'bblanchon/ArduinoJson@^6.21.6', // v6 pin: source uses StaticJsonDocument syntax
  'adafruit/Adafruit GFX Library@^1.11',
  'adafruit/Adafruit SSD1306@^2.5',
  'adafruit/Adafruit BusIO@^1.16',
  'adafruit/Adafruit Unified Sensor@^1.1',
  'adafruit/DHT sensor library@^1.4',
  'adafruit/RTClib@^2.1',
  'adafruit/Adafruit MPU6050@^2.2',
  'adafruit/Adafruit BME280 Library@^2.2',
  'adafruit/Adafruit BMP280 Library@^2.6',
  'adafruit/Adafruit_VL53L0X@^1.2',
  'adafruit/Adafruit ILI9341@^1.6',
  'adafruit/Adafruit ST7735 and ST7789 Library@^1.10',
  'arduino-libraries/NTPClient@^3.2',
  'knolleary/PubSubClient@^2.8',
  'gilmaimon/ArduinoWebsockets@^0.5.4',
  'dfrobot/DFRobotDFPlayerMini@^1.0.6',
  'robtillaart/AS5600@^0.6',
  'marcoschwartz/LiquidCrystal_I2C@^1.1.4',
  'pololu/QTRSensors@^4', // latent fix: legacy Dockerfile missed this. Latest is v4.0.0 (2019).
  'teckel12/NewPing@^1.9', // bundled libs/NewPing is v1.5 AVR-only; registry has ESP32 support
  // BUG-059 closure (2026-04-30): 技適制約により I2C 版 RFID lib 一択。
  // miguelbalboa/MFRC522 (SPI) と MakerSpaceLeiden/rfid (SPI/I2C/UART) は
  // 両方とも `class MFRC522` を提供し arozcan と link collision するため drop。
  // M5Stack RFID 2 Unit (WS1850S, I2C 0x28) が DigiCode 公式 reader、
  // SPI 版 MFRC522 は永久にサポートしない (rfid_init_generic block 削除済)。
  'https://github.com/arozcan/MFRC522-I2C-Library.git', // MFRC522 (I2C only — 技適対応)
];

/** ESP32-only libs (platform = espressif32 / pioarduino). */
const ESP32_REGISTRY_LIBS: readonly string[] = [
  'dawidchyrzynski/home-assistant-integration@^2.1', // depends on ESP32 WiFi
  'dvarrel/ESPping@^1.0.5',
  'plerup/EspSoftwareSerial@^8.2',
  'crankyoldgit/IRremoteESP8266@^2.8',
  'handmade0octopus/ESP32-TWAI-CAN@^1.0.1',
  // ESP32Servo360 dropped (BUG-059 X2 triage 2026-04-30):
  // `ecal-mid/ESP32Servo360@^0.1.0` ships smart-quote characters in its
  // `#error` directive that arduino-esp32 v3.x's gcc rejects with
  // "extended character " is not valid in an identifier", which fails
  // every ESP32 build on the pioarduino image. No DigiCode block
  // generator emits `#include <ESP32Servo360.h>` (verified by grep), so
  // the dependency was unused dead weight from BP-era exploration.
];

/**
 * Build lib_deps for a given target. Order: common volume-mount, then
 * common registry/git, then ESP32-only volume-mount + registry, then
 * Nano RP2040 Connect specifics.
 *
 * Note: Nano RP2040 Connect is currently mapped to the rp2040 (Earle
 * Philhower) core in boards.ts as a fallback. WiFiNINA support is a
 * follow-up task (44.md Phase 4+).
 */
export function buildLibDeps(libsDir: string, target: { platform: string }): string[] {
  const deps: string[] = [
    `file://${libsDir}/Adafruit_NeoPixel`,
    ...COMMON_REGISTRY_LIBS,
  ];
  if (isEsp32FamilyPlatform(target.platform)) {
    deps.push(
      `file://${libsDir}/DigiCodeHumanoid`,
      `file://${libsDir}/DigiCodeTransform`,
      `file://${libsDir}/DigiCodeWheel`,
      `file://${libsDir}/ESP32Servo`,
      `file://${libsDir}/NimBLE-Arduino`,
      `file://${libsDir}/NimBLEOta`,
      ...ESP32_REGISTRY_LIBS,
    );
  }
  return deps;
}

function buildPlatformioIni(
  target: { platform: string; board: string; extraBuildFlags?: string[] },
  libsDir: string,
): string {
  const libDeps = buildLibDeps(libsDir, target)
    .map((dep) => `    ${dep}`)
    .join('\n');
  // BUG-059 X2 triage (2026-04-30): arduino-esp32 v3.x's gcc promotes a
  // wider set of warnings into errors than v2.x did. Several registry libs
  // (Adafruit_VL53L0X, ...) emit benign warnings that worked under v2.x
  // and now fail the build. Quiet these defensively at the env level —
  // legitimate user-source warnings still surface in the stderr we return.
  const baseFlags = [
    '-DDIGICODE_COMPILE_API',
    '-Wno-error',
    '-Wno-deprecated-declarations',
    '-Wno-unused-but-set-variable',
    '-Wno-unused-variable',
  ];
  const buildFlags = [...baseFlags, ...(target.extraBuildFlags ?? [])]
    .map((flag) => `    ${flag}`)
    .join('\n');
  // BUG-059 X2 triage round 7 (2026-04-30): NimBLE-Arduino + the OTA
  // template's WiFi/HTTP/Update stack push the firmware past the default
  // ESP32 partition's 1.25 MB app slot (`Error: program size (1400874
  // bytes) is greater than maximum allowed (1310720 bytes)`).
  // `min_spiffs.csv` is an arduino-esp32-shipped layout with two 1.9 MB
  // OTA slots + a small SPIFFS region — enough headroom for NimBLE,
  // ArduinoHA + WebServer + Update, and any reasonable user code, while
  // still preserving OTA dual-slot behaviour. RP2040 builds use the
  // raspberrypi platform's own partition handling (no override).
  const partitionLine = isEsp32FamilyPlatform(target.platform)
    ? 'board_build.partitions = min_spiffs.csv\n'
    : '';
  return `[env:${target.board}]
platform = ${target.platform}
board = ${target.board}
framework = arduino
${partitionLine}; BUG-059 X2 triage round 5 (2026-04-30): pioarduino's default LDF
; behaviour links every lib in lib_deps unconditionally, so
; \`miguelbalboa/MFRC522\` and \`arozcan/MFRC522-I2C-Library\` (two
; different libraries that both ship a class named MFRC522) collided at
; the linker stage on every ESP32 build with "multiple definition of
; _ZN7MFRC522..." even when the user source did not #include either
; one. \`chain\` (one of PIO's stricter LDF modes) only pulls libs that
; the user source actually #includes, so unused lib_deps stop fighting
; over symbols. Default in PIO docs is "chain", but pioarduino appears to
; override with a deeper mode — this line forces it back.
lib_ldf_mode = chain
lib_deps =
${libDeps}
build_flags =
${buildFlags}
`;
}

export async function compile(
  req: CompileRequest,
  envOverride: Partial<CompileEnv> = {},
): Promise<CompileResult> {
  const env = { ...DEFAULT_ENV, ...envOverride };
  const start = Date.now();

  const target = pioTargetFor(req.board);
  const templateName = templateNameFor(req.connectionType);
  const templatePath = path.join(env.templatesDir, `${templateName}.ino`);

  if (!existsSync(templatePath)) {
    return {
      success: false,
      error: `template not found: ${templatePath}`,
      durationMs: Date.now() - start,
    };
  }

  const template = readFileSync(templatePath, 'utf-8');
  const injected = injectUserCode(template, req);

  // Cache key includes board + template, so different fullPackage flags
  // landing on the same source still share the firmware.bin half — the
  // optional 4-file bundle is rebuilt from the same cached binaries.
  const cacheKey = computeCacheKey(
    injected,
    target.board,
    templateName,
    target.platform,
    target.extraBuildFlags,
  );

  // 1. Cache lookup (no lock — different keys must not block each other).
  const cached = cacheGet(env.cacheDir, cacheKey);
  if (cached) {
    // Strip optional fullPackage fields if the caller did not ask for them,
    // to keep response shape consistent with a fresh non-fullPackage compile.
    const result: CompileSuccess = {
      ...cached,
      durationMs: Date.now() - start,
      cached: true,
    };
    if (!env.fullPackage) {
      delete result.bootloader;
      delete result.partitions;
      delete result.bootApp0;
    }
    return result;
  }

  // 2. Cache miss: compile under a per-(board × template) lock. Different
  //    envs run in parallel; identical ones serialize through PIO.
  const lockKey = projectKey(target.board, templateName);
  return withLock(lockKey, async () => {
    // Re-check the cache after acquiring the lock — a parallel request for
    // the same key may have populated it while we were waiting.
    const racewinner = cacheGet(env.cacheDir, cacheKey);
    if (racewinner) {
      const result: CompileSuccess = {
        ...racewinner,
        durationMs: Date.now() - start,
        cached: true,
      };
      if (!env.fullPackage) {
        delete result.bootloader;
        delete result.partitions;
        delete result.bootApp0;
      }
      return result;
    }

    const ini = buildPlatformioIni(target, env.libsDir);
    const { projectDir } = ensurePersistentProject(
      env.projectsDir,
      target.board,
      templateName,
      ini,
    );
    writeMainIno(projectDir, injected);
    const result = await runPio(projectDir, target, templateName, env, start);
    if (result.success) {
      // Always store the full ESP32 4-file bundle when present, regardless
      // of `fullPackage` — future requests asking for it can hit cache.
      cachePut(env.cacheDir, cacheKey, ensureFullEsp32Bundle(result, target, env, projectDir));
    }
    return result;
  });
}

/**
 * If a successful ESP32 build did not include the optional 4-file bundle
 * (because the caller did not request fullPackage), grab it from the build
 * dir + framework package anyway so the cached entry is complete. Cheap —
 * just three small file reads.
 */
function ensureFullEsp32Bundle(
  result: CompileSuccess,
  target: { platform: string; board: string },
  env: CompileEnv,
  projectDir: string,
): CompileSuccess {
  if (!isEsp32FamilyPlatform(target.platform)) return result;
  if (result.bootloader && result.partitions && result.bootApp0) return result;

  const buildDir = path.join(projectDir, '.pio', 'build', target.board);
  const enriched = { ...result };
  const bootloaderPath = path.join(buildDir, 'bootloader.bin');
  const partitionsPath = path.join(buildDir, 'partitions.bin');
  const bootApp0Path = path.join(
    env.pioHome,
    'packages',
    'framework-arduinoespressif32',
    'tools',
    'partitions',
    'boot_app0.bin',
  );
  if (!enriched.bootloader && existsSync(bootloaderPath)) {
    enriched.bootloader = readFileSync(bootloaderPath).toString('base64');
  }
  if (!enriched.partitions && existsSync(partitionsPath)) {
    enriched.partitions = readFileSync(partitionsPath).toString('base64');
  }
  if (!enriched.bootApp0 && existsSync(bootApp0Path)) {
    enriched.bootApp0 = readFileSync(bootApp0Path).toString('base64');
  }
  return enriched;
}

async function runPio(
  projectDir: string,
  target: ReturnType<typeof pioTargetFor>,
  templateName: string,
  env: CompileEnv,
  start: number,
): Promise<CompileResult> {
  try {
    const { stderr } = await execP(`${env.pioBin} run`, {
      cwd: projectDir,
      timeout: env.timeoutMs,
      maxBuffer: 16 * 1024 * 1024,
    });
    const durationMs = Date.now() - start;

    const buildDir = path.join(projectDir, '.pio', 'build', target.board);
    const firmwarePath = path.join(buildDir, 'firmware.bin');
    if (!existsSync(firmwarePath)) {
      return {
        success: false,
        error: 'firmware.bin not produced',
        details: stderr?.slice(-2000),
        durationMs,
      };
    }
    const firmware = readFileSync(firmwarePath).toString('base64');

    const result: CompileSuccess = {
      success: true,
      firmware,
      durationMs,
      template: templateName,
      pioBoard: target.board,
    };

    if (env.fullPackage && isEsp32FamilyPlatform(target.platform)) {
      const bootloaderPath = path.join(buildDir, 'bootloader.bin');
      const partitionsPath = path.join(buildDir, 'partitions.bin');
      const bootApp0Path = path.join(
        env.pioHome,
        'packages',
        'framework-arduinoespressif32',
        'tools',
        'partitions',
        'boot_app0.bin',
      );
      if (existsSync(bootloaderPath)) {
        result.bootloader = readFileSync(bootloaderPath).toString('base64');
      }
      if (existsSync(partitionsPath)) {
        result.partitions = readFileSync(partitionsPath).toString('base64');
      }
      if (existsSync(bootApp0Path)) {
        result.bootApp0 = readFileSync(bootApp0Path).toString('base64');
      }
    }

    return result;
  } catch (e) {
    const err = e as Error & { stderr?: string; stdout?: string; signal?: string };
    const durationMs = Date.now() - start;
    if (err.signal === 'SIGTERM') {
      return {
        success: false,
        error: `timeout after ${env.timeoutMs}ms`,
        stderr: err.stderr?.slice(-2000),
        durationMs,
      };
    }
    return {
      success: false,
      error: err.message,
      stderr: err.stderr?.slice(-4000),
      details: err.stdout?.slice(-2000),
      durationMs,
    };
  }
}
