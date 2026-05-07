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
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';

import { pioTargetFor } from './boards.js';
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
  /**
   * Bypass the result-blob cache (cache.ts) for this request: skip both
   * cacheGet and cachePut. Used by the orchestrator's --no-cache flag and
   * the API's `?no-cache=true` query param so a test session does not pollute
   * the production cache and cannot accidentally serve a previously-cached
   * result. The lib precompile cache (image-baked .pio/build/<env>/lib<N>
   * archives) remains active — that is exactly what amendment 9 is supposed
   * to deliver in test mode too. Default false (production cache active).
   */
  noCache: boolean;
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
  noCache: false,
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
  // post-Phase 4-4 commit 3 (case_0197-0202 fix, 2026-05-06): stepperDriverBlocks.ts
  // (52.md commit 2e50876, 第80回) emits `#include <AccelStepper.h>` for both
  // a4988_* (5 blocks) and uln2003_* (2 blocks) generators, but AccelStepper
  // was never registered in lib_deps. Source comment already pinned `@^1.64`,
  // the registration was the only gap. PIO registry: waspinator/AccelStepper
  // (latest 1.64, Nov 2022). Same systematic root cause as commits 2-4/2-7/2-10
  // (block added without lib_deps registration in compile.ts).
  'waspinator/AccelStepper@^1.64', // a4988 + uln2003 stepper drivers (52.md commit #5)
  // BUG-059 closure (2026-04-30): 技適制約により I2C 版 RFID lib 一択。
  // miguelbalboa/MFRC522 (SPI) と MakerSpaceLeiden/rfid (SPI/I2C/UART) は
  // 両方とも `class MFRC522` を提供し arozcan と link collision するため drop。
  // M5Stack RFID 2 Unit (WS1850S, I2C 0x28) が DigiCode 公式 reader、
  // SPI 版 MFRC522 は永久にサポートしない (rfid_init_generic block 削除済)。
  // post-Phase 4-4 commit 16 (2026-05-06): release 前に commit hash pin で
  // upstream drift による silent breakage を防ぐ。`c0e3c5d` は Phase 4-4
  // で動作実証済の master HEAD (ML30 cache `.piopm` で `sha.c0e3c5d` を
  // 確証、PIO smoke 70/77 で build 通過確認)。registry 公式 pin を持たない
  // git URL lib は PIO が毎回 master HEAD を fetch するため、upstream の
  // breaking change が release 後に user 全員の compile を突然 fail させる
  // 重大 risk があった。
  'https://github.com/arozcan/MFRC522-I2C-Library.git#c0e3c5d1e54745928a787a9d4e70f21fcaf59943', // MFRC522 (I2C only — 技適対応、commit hash pin 2026-05-06)
  // 51.md Phase A+B 追加 (2026-05-04 第78回、FS 講座 + Fab Academy 対応):
  'm5stack/M5Unit-ENV@^1.3.2',              // ENV III (SHT30+QMP6988) + ENV IV (SHT40+BMP280) 統合 lib (D-5 案 β)
  'sensirion/Sensirion I2C SHT3x@^1.0.1',   // stand-alone SHT30 (Fab Academy 自作回路、D-5 案 β)
  'adafruit/Adafruit SHT4x Library@^1.0.5', // stand-alone SHT40 (Adafruit 既存 lib 整合、D-5 案 β)
  'robtillaart/HX711@^0.6.3',               // HX711 ロードセル (calibrate / median 内蔵、AS5600 同 owner 一貫性、D-6)
  // 52.md Phase C+D + 新規発見追加 (2026-05-04 第80回、Fab Academy + FS 講座 + 一般 ESP32 教育対応):
  'akj7/TM1637 Driver@^2.2.1',                                              // TM1637 7-seg (Phase C、教育標準、blink/scroll/animation 内蔵)
  'codewitch-honey-crisis/htcw_max7219@^2.0.0',                             // MAX7219 LED matrix (Phase C、LGPL wayoda/LedControl 回避)
  'zinggjm/GxEPD2@^1.6.9',                                                  // E-paper 標準 lib (Phase D、Adafruit_GFX 互換、M5Paper 対応)
  'robtillaart/INA219@^0.4.2',                                              // INA219 高精度電流/電圧/電力 (Phase D、I2C、AS5600/HX711 同 owner 一貫性)
  'robtillaart/ACS712@^0.4.0',                                              // ACS712 ホール電流 (Phase D、Q-D 確定、RMS 計算内蔵)
  'mikalhart/TinyGPSPlus@^1.1.0',                                           // GPS NEO-6M/8M NMEA parser (強推奨、業界標準)
  '4-20ma/ModbusMaster@^2.0.1',                                             // Modbus RTU master (強推奨、Industrial IoT、RS485 transceiver 併用)
  'sensirion/Sensirion I2C SCD30@^1.1.1',                                   // CO2 SCD30 (中推奨、Q-E 確定、I2C + temp/humidity 内蔵)
  'avaldebe/PMSerial@^1.2.0',                                               // PM2.5 PMS5003 (中推奨、UART)
  'adafruit/Adafruit NeoMatrix@^1.3.3',                                     // NeoPixel 2D matrix (中推奨、Q-H 確定、Adafruit_GFX 互換)
  'adafruit/Adafruit APDS9960 Library@^1.3.1',                              // APDS9960 ジェスチャー/色/近接 (低推奨採用、HCI プロジェクト)
  'sparkfun/SparkFun MAX3010x Pulse and Proximity Sensor Library@^1.1.2',   // MAX30102/30105 心拍/SpO2 (低推奨採用、ヘルスケア教材)
];

/**
 * ESP32-only libs (platform = espressif32 / pioarduino).
 *
 * ⚠️ lib_deps entries are build-active: every entry here is compiled for
 * every ESP32 build under `lib_ldf_mode = chain`, regardless of which
 * boards use it. Do NOT add libs as placeholders for future boards —
 * `heltecautomation/Heltec ESP32 Dev-Boards` was a placeholder here until
 * 55.md Phase 2 (R1, 2026-05-04) and its headers/macros polluted 16/20
 * boards' fresh compiles (root cause A of 54.md §1.2). Re-add a lib only
 * when a board entry, board-conditional dispatch, build flags, and linker
 * flag are introduced in the same commit.
 */
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
  // 51.md Phase A+B 追加 (2026-05-04 第78回、FS 講座 + Fab Academy 対応):
  'azure/Azure SDK for C@^1.1.8',  // Azure IoT Hub + Central + DPS (Microsoft 公式・MIT、D-3。失敗時 D-19 fallback = MQTT 薄ラッパー)
  'm5stack/M5Unified@^0.2.14',     // M5Stack 全機種統合 (Core/Core2/Tough/CoreS3/StickC/StickC-Plus/AtomS3/Stamp/Capsule/Cardputer/etc.、D-4)
  // 52.md Phase C+D + 新規発見追加 (2026-05-04 第80回):
  'sandeepmistry/LoRa@^0.8.0',                          // LoRa SX1276/77/78/79 generic SPI (強推奨、Q-B 確定、ESP32 SPI、教育標準)
];

/**
 * Build lib_deps for a given target. Order: common volume-mount + registry,
 * then DigiCode bundled libs, then ESP32 registry libs.
 *
 * 56.md (2026-05-05): DigiCode is ESP32-only after the RP2040 board removal,
 * so every target gets the ESP32 lib_deps unconditionally. The signature
 * still takes a `target` object to keep the call sites simple; future
 * board-family branching (if it ever comes back) can rehydrate the predicate
 * without touching every caller.
 */
export function buildLibDeps(libsDir: string, _target: { platform: string }): string[] {
  return [
    `file://${libsDir}/Adafruit_NeoPixel`,
    ...COMMON_REGISTRY_LIBS,
    `file://${libsDir}/DigiCodeHumanoid`,
    `file://${libsDir}/DigiCodeTransform`,
    `file://${libsDir}/DigiCodeWheel`,
    `file://${libsDir}/ESP32Servo`,
    `file://${libsDir}/NimBLE-Arduino`,
    `file://${libsDir}/NimBLEOta`,
    ...ESP32_REGISTRY_LIBS,
    // BUG-068 (2026-04-30): user code emitting #include <SD.h> causes PIO
    // LDF to transitively pull arduino-libraries/SD@1.x from the registry,
    // which ships utility/Sd2Card.h / SdFat.h / SdFile.cpp and fails ESP32
    // with "#error Architecture or board not supported." The pioarduino
    // framework already ships a compatible SD lib (name=SD, v3.2.1,
    // architectures=esp32) at packages/framework-arduinoespressif32/
    // libraries/SD with a matching SD.begin / SD.open / File / fs::FS API.
    // Pinning that path with a `symlink://` lib_deps entry tells PIO LDF
    // we already have SD; the registry version is no longer auto-installed.
    // (Round 1 used `lib_ignore = SD` but that filtered both the registry
    // and framework copies by name. Round 2 = explicit symlink wins.)
    `symlink:///root/.platformio/packages/framework-arduinoespressif32/libraries/SD`,
  ];
}

/**
 * Path to the cross-project compile object cache populated by warmup-pio.ts
 * at image build time and reused by every runtime compile. Lives in the
 * image filesystem (NOT a VOLUME mount), so the cache survives
 * `docker volume rm` cycles required for proper image cutovers
 * (`rules/digicode/05-deploy.md` PIO cache invalidation procedure). New
 * cache entries written at runtime end up in the container's COW upper
 * layer and are lost on container restart — acceptable because the
 * baked-in cache covers the lib_deps universe and main.cpp is unique
 * per request anyway.
 */
const BUILD_CACHE_DIR = '/root/.platformio/build-cache';

/**
 * Build the platformio.ini contents for a (board, template-equivalent) target.
 * Exported so warmup-pio.ts can bake the same lib_deps + build_flags + lib_ignore
 * configuration into the image as runtime uses — DRY-ing this prevents an
 * image-build precompile (amendment 9) from being silently invalidated by
 * a runtime config drift.
 */
export function buildPlatformioIni(
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
  // still preserving OTA dual-slot behaviour.
  const partitionLine = 'board_build.partitions = min_spiffs.csv\n';
  // BUG-077 (2026-05-05): the M5Stack ecosystem libs depend on each other
  // through M5UnitComponent.hpp (the M5UnitUnified base class) whose HAL
  // adapters target ESP32 original / S2 / S3 only. On esp32-c3-devkitm-1
  // the adapters reference symbols that don't exist on the C3 port:
  //   - adapter_gpio.cpp:389  ADC_RTC_CLK_SRC_DEFAULT    (C3 has only ADC_DIGI_CLK_SRC_DEFAULT)
  //   - adapter_i2c.cpp:44    gpio_dev_s::func_sel       (C3 GPIO matrix layout differs)
  //   - adapter_i2c.cpp:55    I2CEXT1_SDA_IN_IDX / SCL   (C3 has only one I2C peripheral)
  // → compile fails for every esp32c3 build (xiao-esp32c3, esp32-c3-generic,
  // m5stamp-c3 all map to esp32-c3-devkitm-1).
  //
  // The initial fix (`lib_ignore = M5Unified, M5UnitUnified`) was incomplete:
  // M5Unit-ENV is a separate top-level lib in COMMON_REGISTRY_LIBS that
  // depends on M5UnitComponent.hpp (verified by full-recursive grep in
  // libdeps/esp32-c3-devkitm-1: every M5Unit-ENV/src/unit/*.hpp includes
  // <M5UnitComponent.hpp>). When M5UnitUnified is ignored, M5Unit-ENV's
  // own compile fails at unit_BME688.cpp → fatal "M5UnitComponent.hpp:
  // No such file or directory". Adding M5Unit-ENV to the ignore list closes
  // the gap. M5HAL / M5Utility / M5GFX are not in the list because grep
  // confirmed they do not reference M5UnitComponent (M5HAL self-references
  // M5Utility only; M5Utility is self-contained; M5GFX is a transitive dep
  // of M5Unified that PIO chain mode auto-skips when M5Unified is ignored).
  //
  // M5Stamp C3 is a breakout-only product with no on-board LCD/buttons,
  // so M5Unified / M5Unit-ENV APIs are not actually needed for any C3 board.
  // Ignoring all three libs at the env level is the minimum-blast-radius fix;
  // ESP32 / S2 / S3 builds keep the full M5 stack. ESP32-C6 was verified to
  // not have the same issue (smoke esp32c6 × usb PASSed before this followup
  // fix), so no BUG-078 needed.
  //
  // 教訓: lib_ignore 範囲決定は transitive header reference grep 全件で実証必須。
  // See `prompt/maintenance/rules/common/judgment-mistakes-history.md` (パターン B
  // scope の自己確証) — initial fix で M5UnitUnified ignore の transitive
  // 依存先 (M5Unit-ENV) を grep せず scope 確定した判断ミスを記録。
  const libIgnoreLine = target.board === 'esp32-c3-devkitm-1'
    ? 'lib_ignore = M5Unified, M5UnitUnified, M5Unit-ENV\n'
    : '';
  // amendment 9 build_cache_dir pivot (2026-05-07): a cross-project compile
  // object cache lets warmup-pio.ts populate a single shared cache and
  // discard the per-target persistent project dirs after bake, instead of
  // baking ~3.25 GB of project state into the image. Image size growth
  // drops from ~3.25 GB → ~700 MB. The runtime persistent project hits
  // the cache via SCons MD5 dedupe — first compile per (board, template)
  // post-cutover is ~30-60s (lib resolve + cache HITs + link) instead of
  // ~5 min cold (BUG-078 第84回 Stage D v2 max 509s). Cache lives outside
  // VOLUME mounts so it survives `docker volume rm` cycles, baked into
  // the image filesystem.
  return `[platformio]
build_cache_dir = ${BUILD_CACHE_DIR}

[env:${target.board}]
platform = ${target.platform}
board = ${target.board}
framework = arduino
${partitionLine}${libIgnoreLine}; BUG-059 X2 triage round 5 (2026-04-30): pioarduino's default LDF
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

  // 55.md Phase 2 R4 (2026-05-04): hash lib_deps into the cache key so
  // adding/removing a lib in this file invalidates entries built with the
  // previous configuration. Without this, image cutovers are invisible to
  // the cache layer (Round 3's 92.6% / Round 4's 79.7% were cache-HIT
  // illusions surviving the Heltec/bootstrap fixes).
  // 16-char prefix of sha256 = 64-bit hash space, ample for collision-free
  // operation at any realistic build volume.
  const libDeps = buildLibDeps(env.libsDir, target);
  const libDepsHash = createHash('sha256').update(libDeps.join('\n')).digest('hex').slice(0, 16);

  // Cache key includes board + template, so different fullPackage flags
  // landing on the same source still share the firmware.bin half — the
  // optional 4-file bundle is rebuilt from the same cached binaries.
  const cacheKey = computeCacheKey(
    injected,
    target.board,
    templateName,
    target.platform,
    target.extraBuildFlags,
    libDepsHash,
  );

  // 1. Cache lookup (no lock — different keys must not block each other).
  //    amendment 9: env.noCache (set by `?no-cache=true` query param /
  //    orchestrator --no-cache flag) bypasses both cacheGet here and cachePut
  //    inside the lock, so test sessions cannot read or pollute the
  //    production cache.
  if (!env.noCache) {
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
  }

  // 2. Cache miss: compile under a per-(board × template) lock. Different
  //    envs run in parallel; identical ones serialize through PIO.
  const lockKey = projectKey(target.board, templateName);
  return withLock(lockKey, async () => {
    // Re-check the cache after acquiring the lock — a parallel request for
    // the same key may have populated it while we were waiting.
    if (!env.noCache) {
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
    }

    const ini = buildPlatformioIni(target, env.libsDir);
    const { projectDir } = ensurePersistentProject(
      env.projectsDir,
      target.board,
      templateName,
      ini,
    );
    writeMainIno(projectDir, injected);
    try {
      const result = await runPio(projectDir, target, templateName, env, start);
      if (result.success && !env.noCache) {
        // Always store the full ESP32 4-file bundle when present, regardless
        // of `fullPackage` — future requests asking for it can hit cache.
        cachePut(env.cacheDir, cacheKey, ensureFullEsp32Bundle(result, target, env, projectDir));
      }
      return result;
    } finally {
      // amendment 9 周辺 (#3 WiFi credential cleanup, release-gate audit
      // 2026-05-07): user code may inline WiFi SSID/password (mqttBlocks /
      // wifi_connect generators), and main.ino persists in the projectDir
      // until the next compile for that (board × template). Overwrite with
      // an empty file post-compile so a paused / idle ML30 does not retain
      // plaintext credentials. The cache blob (firmware.bin, binary) is
      // intentionally not touched — recovering credentials from there
      // requires decompilation, well outside the threat model. Cleanup is
      // best-effort: a write failure must not fail the compile result.
      try {
        writeFileSync(path.join(projectDir, 'src', 'main.ino'), '');
      } catch (cleanupErr) {
        console.warn(
          '[compile] main.ino cleanup failed (best-effort, compile result unaffected):',
          cleanupErr,
        );
      }
    }
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

    if (env.fullPackage) {
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
