/**
 * warmup-pio.ts — Build-time primer that pre-DLs frameworks + lib tarballs
 * AND pre-compiles lib archives into the persistent project dirs that the
 * runtime uses, so a fresh container's first compile per (board × template)
 * does not pay for ~120s of framework download + lib_deps fetch + ~3-5 min
 * of lib `.a` archive generation.
 *
 * amendment 9 (2026-05-07, release 前最善状態): the original primer (45.md
 * §3 Q1=C, primer scope A2) only filled `/root/.platformio/.cache` (framework
 * + lib tarballs). The persistent project dirs (`/opt/digicode-compile/
 * projects/<board>_<template>/`) under PROJECTS_DIR remained empty until the
 * first runtime compile per env, which then took 360-509s (Stage D v2 max
 * 509s heavy lib lottery, BUG-078 第84回 — NimBLE-Arduino + ArduinoHA + a
 * deep registry chain compiling from source on a cold project). Amendment 9
 * pushes those `.pio/libdeps/` + per-lib archives under `.pio/build/<env>/` into the
 * image at build time so post-cutover the first compile per env is ~30s.
 *
 * Why VOLUME-baked content survives: Docker copies image content into a
 * named volume on first mount when the volume is empty. So as long as
 * `docker volume rm digicode-compile-api_digicode-projects` is run before
 * `up -d` after a cutover (already documented in
 * `prompt/maintenance/rules/digicode/05-deploy.md` PIO cache invalidation
 * procedure), the freshly-evicted volume picks up image-baked archives.
 *
 * Scope decision (Q1, user 確定 2026-05-07): 7 PIO targets × DigiCodeOTA
 * template only. Image growth ~3-4 GB. Other templates (DigiCodeUSB /
 * DigiCodeBLE) share the same lib_deps + ~9 割 of compiled archives, so
 * their first compile is fast even without per-template bake (only main.cpp
 * recompiles + relink). Future scope expansion = trivial config change here.
 *
 * Compile failures are tolerated — the framework + tarball DL happens before
 * the build phase, so the cache is populated even on build fail. Lib
 * archives are an additional bonus when build succeeds. We log warnings but
 * do not fail the image build.
 *
 * Post-bake step: src/main.ino is overwritten with a minimal stub (no user
 * code, just empty userSetup/userLoop), so the baked image does not carry
 * stale credentials / user identifiers from the warmup compile (defense in
 * depth alongside the runtime cleanup in compile.ts withLock finally).
 */

import { execSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';

import { buildPlatformioIni } from '../src/compile.js';
import { injectUserCode, templateNameFor, type ConnectionType } from '../src/inject.js';
import type { PioTarget } from '../src/boards.js';

const PROJECTS_DIR = process.env.PROJECTS_DIR ?? '/opt/digicode-compile/projects';
const TEMPLATES_DIR = process.env.TEMPLATES_DIR ?? '/opt/digicode-compile/templates';
const LIBS_DIR = process.env.LIBS_DIR ?? '/opt/digicode-compile/libs';
const PIO_BIN = process.env.PIO_BIN ?? 'pio';

/**
 * BUG-059 X2 (2026-04-30) + amendment 9 (2026-05-07): pioarduino is the
 * canonical ESP32 platform. Mirror compile-api/src/boards.ts — same URL pin,
 * same arduino-esp32 release. `pio platform install <url>` rejects this URL
 * form, so the only reliable install path is letting `pio run` resolve
 * `platform = <url>` from a primer's platformio.ini at image build time.
 * Updating the URL here without updating boards.ts (or vice versa) leaves
 * the runtime resolving a different platform than the image preloaded,
 * defeating the primer.
 */
const PIOARDUINO_PLATFORM =
  'https://github.com/pioarduino/platform-espressif32/releases/download/54.03.21/platform-espressif32.zip';

/**
 * 7 unique PIO targets in compile-api/src/boards.ts FQBN_TO_PIO (after
 * dedup by board id — m5stack-atom is shared by atom_lite + atom_matrix +
 * m5stick_c_plus; esp32-s3-devkitc-1 is shared by esp32s3 + ATOMS3 Lite,
 * the ATOMS3 Lite USB-CDC build flags are NOT baked here so the
 * persistent project dir matches the more-common plain s3 path; ATOMS3
 * Lite first compile after cutover triggers a partial rebuild only —
 * `.pio/libdeps/` is already warm).
 *
 * Mirroring boards.ts changes here (e.g. adding a new SoC) is required for
 * the bake to cover the new target on the next image build.
 */
const PRIMER_TARGETS: ReadonlyArray<PioTarget> = [
  { platform: PIOARDUINO_PLATFORM, board: 'esp32dev' },
  { platform: PIOARDUINO_PLATFORM, board: 'esp32-s3-devkitc-1' },
  { platform: PIOARDUINO_PLATFORM, board: 'esp32-c3-devkitm-1' },
  { platform: PIOARDUINO_PLATFORM, board: 'esp32-c6-devkitm-1' },
  { platform: PIOARDUINO_PLATFORM, board: 'm5stack-fire' },
  { platform: PIOARDUINO_PLATFORM, board: 'm5stack-atom' },
  { platform: PIOARDUINO_PLATFORM, board: 'm5stamp-pico' },
];

const PRIMER_CONNECTION: ConnectionType = 'ota';
const PRIMER_TEMPLATE_NAME = templateNameFor(PRIMER_CONNECTION); // 'DigiCodeOTA'

/** Empty user-code fragments — the baked compile only needs the template's
 * structural lib references, not user logic. injectUserCode then fills the
 * userSetup/userLoop bodies with the documented `// No setup/loop code`
 * placeholders, which is a valid C++ program. */
const EMPTY_FRAGMENTS = {
  includes: '',
  globals: '',
  setupCode: '',
  loopCode: '',
};

function projectKey(board: string, templateName: string): string {
  return `${board}_${templateName}`;
}

function bakeOneTarget(target: PioTarget): { board: string; ok: boolean; ms: number } {
  const start = Date.now();
  const key = projectKey(target.board, PRIMER_TEMPLATE_NAME);
  const projectDir = path.join(PROJECTS_DIR, key);
  const srcDir = path.join(projectDir, 'src');
  mkdirSync(srcDir, { recursive: true });

  const templatePath = path.join(TEMPLATES_DIR, `${PRIMER_TEMPLATE_NAME}.ino`);
  const template = readFileSync(templatePath, 'utf-8');
  const injected = injectUserCode(template, EMPTY_FRAGMENTS);

  const ini = buildPlatformioIni(target, LIBS_DIR);
  writeFileSync(path.join(projectDir, 'platformio.ini'), ini, 'utf-8');
  writeFileSync(path.join(srcDir, 'main.ino'), injected, 'utf-8');

  console.log(`[warmup-pio] [${target.board}] running pio run...`);
  let ok = false;
  try {
    execSync(`${PIO_BIN} run`, {
      cwd: projectDir,
      stdio: 'inherit',
      env: { ...process.env, PLATFORMIO_DISABLE_PROGRESSBAR: 'true' },
    });
    ok = true;
    console.log(`[warmup-pio] [${target.board}] OK in ${Date.now() - start}ms`);
  } catch {
    // Compile failure tolerated — framework + lib tarball DL still happens
    // before the build phase, so /root/.platformio/.cache is populated even
    // on build fail. The runtime first-compile for this board will then
    // do the lib-archive generation work; that is the same path AS-IS, so
    // a partial bake is never worse than no bake.
    console.warn(
      `[warmup-pio] [${target.board}] pio run returned non-zero — framework/tarball cache still populated, lib archive bake may be incomplete`,
    );
  }

  // Post-bake cleanup: clear main.ino so the baked image carries no
  // identifiable user code (defense in depth alongside compile.ts runtime
  // cleanup; release-gate audit #3, 2026-05-07).
  try {
    writeFileSync(path.join(srcDir, 'main.ino'), '', 'utf-8');
  } catch (e) {
    console.warn(`[warmup-pio] [${target.board}] main.ino cleanup failed (non-fatal):`, e);
  }

  return { board: target.board, ok, ms: Date.now() - start };
}

function main(): void {
  console.log('[warmup-pio] start — pre-DL frameworks + lib tarballs + bake lib archives into image');
  console.log(`[warmup-pio] PROJECTS_DIR=${PROJECTS_DIR}, TEMPLATES_DIR=${TEMPLATES_DIR}, LIBS_DIR=${LIBS_DIR}, PIO_BIN=${PIO_BIN}`);
  console.log(`[warmup-pio] targets: ${PRIMER_TARGETS.map((t) => t.board).join(', ')} × template=${PRIMER_TEMPLATE_NAME}`);

  // Clean the projects dir before baking (idempotent re-runs in dev), but
  // leave PROJECTS_DIR itself in place so subsequent mkdirSync is a no-op.
  // In the Dockerfile build phase the dir is fresh anyway, so this is mostly
  // a developer ergonomic.
  for (const target of PRIMER_TARGETS) {
    const dir = path.join(PROJECTS_DIR, projectKey(target.board, PRIMER_TEMPLATE_NAME));
    rmSync(dir, { recursive: true, force: true });
  }

  const results: Array<{ board: string; ok: boolean; ms: number }> = [];
  for (const target of PRIMER_TARGETS) {
    results.push(bakeOneTarget(target));
  }

  const okCount = results.filter((r) => r.ok).length;
  const totalMs = results.reduce((acc, r) => acc + r.ms, 0);
  console.log(
    `[warmup-pio] done — ${okCount}/${PRIMER_TARGETS.length} targets baked successfully, ${totalMs}ms total`,
  );
  for (const r of results) {
    console.log(`[warmup-pio]   ${r.board}: ${r.ok ? 'OK' : 'PARTIAL'} (${r.ms}ms)`);
  }
}

main();
