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

import { isEsp32, pioTargetFor } from './boards.js';
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
  templatesDir: process.env.TEMPLATES_DIR ?? '/opt/digicode-compile/templates',
  libsDir: process.env.LIBS_DIR ?? '/opt/digicode-compile/libs',
  timeoutMs: Number(process.env.COMPILE_TIMEOUT_MS ?? 180_000),
  fullPackage: false,
};

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
  // Minimum lib_deps for Phase 2 PoC. Phase 3 will expand to cover every
  // block-generated #include via 44-A_library-compat-table.md inventory.
  const ini = `[env:${target.board}]
platform = ${target.platform}
board = ${target.board}
framework = arduino
lib_deps =
    file://${env.libsDir}/DigiCodeHumanoid
    file://${env.libsDir}/DigiCodeTransform
    file://${env.libsDir}/DigiCodeWheel
    madhephaestus/ESP32Servo
    dvarrel/ESPping
    bblanchon/ArduinoJson
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

    if (env.fullPackage && isEsp32(target.platform === 'espressif32' ? 'esp32:esp32:esp32' : '')) {
      // Note: real fullPackage logic also checks the original FQBN, not
      // platform alone; for Phase 2 we only return firmware.bin and add the
      // 4-file bundle in a follow-up commit.
      // TODO: bundle bootloader.bin / partitions.bin / boot_app0.bin
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
