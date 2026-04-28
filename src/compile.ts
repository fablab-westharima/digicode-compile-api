/**
 * compile.ts — Core compile flow.
 *
 *   1. Resolve FQBN → PlatformIO target (boards.ts)
 *   2. Load .ino template + inject user code (inject.ts)
 *   3. Materialize a tmp PlatformIO project (platformio.ini + src/main.ino)
 *   4. Spawn `pio run`
 *   5. Read firmware.bin (+ bootloader/partitions/boot_app0 for ESP32 fullPackage)
 *   6. Cleanup tmp dir
 */

import { exec } from 'node:child_process';
import { promisify } from 'node:util';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';

import { pioTargetFor } from './boards.js';
import { injectUserCode, templateNameFor, type ConnectionType } from './inject.js';

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
  durationMs: number;
  template: string;
  pioBoard: string;
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
  'miguelbalboa/MFRC522@^1.4.12',
  'pololu/QTRSensors@^4', // latent fix: legacy Dockerfile missed this. Latest is v4.0.0 (2019).
  'teckel12/NewPing@^1.9', // bundled libs/NewPing is v1.5 AVR-only; registry has ESP32 support
  'https://github.com/MakerSpaceLeiden/rfid#1.5.1', // MFRC522-spi-i2c-uart-async
  'https://github.com/arozcan/MFRC522-I2C-Library.git', // MFRC522_I2C: latent fix
];

/** ESP32-only libs (platform = espressif32). */
const ESP32_REGISTRY_LIBS: readonly string[] = [
  'dawidchyrzynski/home-assistant-integration@^2.1', // depends on ESP32 WiFi
  'dvarrel/ESPping@^1.0.5',
  'plerup/EspSoftwareSerial@^8.2',
  'crankyoldgit/IRremoteESP8266@^2.8',
  'handmade0octopus/ESP32-TWAI-CAN@^1.0.1',
  'ecal-mid/ESP32Servo360@^0.1.0',
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
function buildLibDeps(libsDir: string, target: { platform: string }): string[] {
  const deps: string[] = [
    `file://${libsDir}/Adafruit_NeoPixel`,
    ...COMMON_REGISTRY_LIBS,
  ];
  if (target.platform === 'espressif32') {
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

  const projectDir = mkdtempSync(path.join(tmpdir(), 'digicode-compile-'));
  try {
    materializeProject(projectDir, target, injected, env);
    return await runPio(projectDir, target, templateName, env, start);
  } catch (e) {
    return {
      success: false,
      error: (e as Error).message,
      durationMs: Date.now() - start,
    };
  } finally {
    rmSync(projectDir, { recursive: true, force: true });
  }
}

function materializeProject(
  projectDir: string,
  target: ReturnType<typeof pioTargetFor>,
  sourceInoContent: string,
  env: CompileEnv,
): void {
  const libDeps = buildLibDeps(env.libsDir, target)
    .map((dep) => `    ${dep}`)
    .join('\n');
  const ini = `[env:${target.board}]
platform = ${target.platform}
board = ${target.board}
framework = arduino
lib_deps =
${libDeps}
build_flags =
    -DDIGICODE_COMPILE_API
`;
  writeFileSync(path.join(projectDir, 'platformio.ini'), ini);
  mkdirSync(path.join(projectDir, 'src'));
  writeFileSync(path.join(projectDir, 'src', 'main.ino'), sourceInoContent);
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

    if (env.fullPackage && target.platform === 'espressif32') {
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
