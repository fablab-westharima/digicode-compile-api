/**
 * warmup-pio.ts — Build-time primer that pre-DLs frameworks + lib tarballs
 * AND populates a cross-project build cache (`build_cache_dir` in
 * compile.ts), so a fresh container's first compile per (board × template)
 * does not pay for ~120s of framework download + ~3-5 min of lib source
 * compilation.
 *
 * amendment 9 (2026-05-07, release 前最善状態): the original primer (45.md
 * §3 Q1=C, primer scope A2) only filled `/root/.platformio/.cache` (framework
 * + lib tarballs). The persistent project dirs (`/opt/digicode-compile/
 * projects/<board>_<template>/`) under PROJECTS_DIR remained empty until the
 * first runtime compile per env, which then took 360-509s (Stage D v2 max
 * 509s heavy lib lottery, BUG-078 第84回 — NimBLE-Arduino + ArduinoHA + a
 * deep registry chain compiling from source on a cold project).
 *
 * Strategy decision (build_cache_dir pivot, user 確定 2026-05-07 mid-build):
 * the first amendment 9 attempt baked the per-target `.pio/build/<env>/`
 * archives directly into the image at PROJECTS_DIR. That worked
 * functionally (run #36 warmup-pio reported 7/7 OK in 57.3 min) but blew
 * past the GH Actions runner's ~14 GB free disk during export and pushed
 * the user-distributed image to ~12 GB (8.83 GB AS-IS + ~3.25 GB bake).
 *
 * Pivot: PIO `build_cache_dir` (set in compile.ts buildPlatformioIni) is a
 * cross-project SCons-level cache of compiled `.o` files keyed by
 * (source content + flags + arch). warmup-pio still runs `pio run` per
 * target so SCons populates the cache, but we DELETE the per-target
 * persistent project dirs after bake — only the cache survives in
 * `/root/.platformio/build-cache/`. Effect: image growth ~700 MB (vs
 * ~3.25 GB), runtime first compile per env ~30-60s (lib resolve + cache
 * HITs + link, vs ~5 min cold). 4-5x smaller image, same UX win.
 *
 * Why /root/.platformio/build-cache/ (not a VOLUME path): the cache lives
 * in the image filesystem so it survives `docker volume rm` cycles
 * (the standard PIO cache invalidation procedure documented in
 * `prompt/maintenance/rules/digicode/05-deploy.md`). New cache entries
 * written at runtime end up in the container's COW upper layer and are
 * lost on container restart — acceptable because the baked-in cache
 * covers the lib_deps universe and main.cpp is unique per request anyway.
 *
 * Scope decision (Q1, user 確定 2026-05-07 morning): 7 PIO targets ×
 * DigiCodeOTA template. Same lib_deps for all templates, so DigiCodeUSB /
 * DigiCodeBLE first compile reuses the cache for ~9 割 of work.
 *
 * Compile failures are tolerated — the framework + tarball DL happens
 * before the build phase, so /root/.platformio/.cache is populated even
 * on build fail. The build cache requires successful compile for entries,
 * so a partial bake means partial cache (still better than no cache).
 *
 * Post-bake cleanup: per-target project dir is removed entirely at the
 * end of bakeOneTarget (cache content survives in build_cache_dir).
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
 * 3 representative PIO targets covering both ESP32 toolchain families
 * (xtensa + riscv32) and ensuring all runtime SoC variants have their
 * toolchains + framework-libs SoC-variant subpackages pre-installed.
 *
 * v3++ target reduction (2026-05-07, post-v2 verification):
 *   v2 (7 targets) image was 16.9 GB virtual / 4.06 GB transport. The
 *   dominant cost is toolchain + framework-libs install (unavoidable for
 *   c3/c6/s3 runtime support), NOT the build_cache_dir itself (1.2 GB /
 *   ~400 MB transport). Going 7 → 3 targets saves ~700 MB virtual /
 *   ~200 MB transport on build_cache_dir but does not reduce toolchain
 *   or framework-libs (1.3 GB single package shipping all 7 SoC variants
 *   under packages/framework-arduinoespressif32-libs/{esp32, s3, c3, c6,
 *   s2, h2, p4}/).
 *
 * Coverage rationale for the 3 chosen targets:
 *   - esp32dev          → installs xtensa-esp-elf toolchain (1.2 GB);
 *                          full cache for esp32 SoC. m5stack-fire /
 *                          m5stack-atom / m5stamp-pico (also esp32 SoC)
 *                          reuse the toolchain at runtime; their first
 *                          compile pays only for user-lib compilation
 *                          (~3-4 min) since SCons cache key includes
 *                          board-specific defines (-DARDUINO_<BOARD>) so
 *                          user-lib .o files are not cross-board cached.
 *   - esp32-s3-devkitc-1 → triggers s3 framework-libs subpackage install
 *                          (or any equivalent xtensa-s3 component
 *                          differences). Full cache for s3 SoC builds.
 *                          ATOMS3 Lite (mapped to same board id with
 *                          different build flags) hits cache MISS due to
 *                          different flags but reuses toolchain.
 *   - esp32-c3-devkitm-1 → installs riscv32-esp toolchain (2.4 GB) +
 *                          c3 framework-libs subpackage. esp32-c6-devkitm-1
 *                          (also riscv32 SoC) reuses the toolchain; first
 *                          compile cost = c6 framework-libs sub-package
 *                          DL (~30s) + user-lib compile (~3-4 min).
 *
 * Non-warmup boards' first runtime compile = ~3-4 min (toolchain HIT,
 * framework-libs HIT, user-lib MISS). Better than AS-IS ~5-7 min cold
 * (which had no toolchain pre-install for s3/c3/c6).
 *
 * Mirroring boards.ts changes here (e.g. adding a new SoC family like
 * h2 or p4) is required for the bake to cover the new target on the
 * next image build. Adding boards within an existing SoC family is
 * cache MISS for first compile but otherwise functional.
 */
const PRIMER_TARGETS: ReadonlyArray<PioTarget> = [
  { platform: PIOARDUINO_PLATFORM, board: 'esp32dev' },
  { platform: PIOARDUINO_PLATFORM, board: 'esp32-s3-devkitc-1' },
  { platform: PIOARDUINO_PLATFORM, board: 'esp32-c3-devkitm-1' },
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

  // Post-bake cleanup: drop the entire per-target project dir. The
  // build_cache_dir at /root/.platformio/build-cache/ holds the compiled
  // `.o` content keyed by (source + flags + arch) and is the only piece
  // that needs to survive into the image. Keeping the project dir would
  // 4-5x the image growth (~3.25 GB vs ~700 MB) without speeding up the
  // runtime first compile beyond what the cache already provides.
  // Also doubles as the main.ino credential cleanup (release-gate audit
  // #3, 2026-05-07) — the file is removed entirely, not just emptied.
  try {
    rmSync(projectDir, { recursive: true, force: true });
  } catch (e) {
    console.warn(`[warmup-pio] [${target.board}] project cleanup failed (non-fatal):`, e);
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

  // v3++ image-size cleanup (2026-05-07): drop /root/.platformio/dist/
  // (PIO download tarball staging area) — once toolchain + framework
  // packages are extracted to /root/.platformio/packages/, the staged
  // tarballs are not needed at runtime. PIO would only re-fetch them on
  // an explicit `pio platform install` reinvocation, which is not a
  // runtime path. Saves ~450 MB virtual / ~200 MB transport.
  //
  // /root/.platformio/.cache/ (download cache for re-installs of registry
  // libs) is intentionally NOT removed — runtime first compile per
  // (board, template) post-cutover may re-resolve `lib_deps` and benefit
  // from cached tarballs there. Keeping .cache/ avoids ~30-60s of network
  // DL per first compile.
  console.log('[warmup-pio] cleanup — removing /root/.platformio/dist/ (PIO staging area, not needed at runtime)');
  try {
    rmSync('/root/.platformio/dist', { recursive: true, force: true });
  } catch (e) {
    console.warn('[warmup-pio] dist cleanup failed (non-fatal):', e);
  }
}

main();
