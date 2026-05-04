/**
 * cache.ts — Filesystem blob cache for compile results.
 *
 * Layout:
 *   <cacheDir>/<key[0:2]>/<key>/
 *     ├── firmware.bin
 *     ├── bootloader.bin   (optional, ESP32 fullPackage only)
 *     ├── partitions.bin   (optional)
 *     ├── boot_app0.bin    (optional)
 *     └── meta.json        ({ template, pioBoard, durationMs, savedAt })
 *
 * The 2-char shard prefix avoids one giant directory; modern filesystems
 * (ext4) handle it fine but listing 1000+ entries gets slow. SHA-256 is
 * used so collisions are not a concern in any realistic horizon.
 *
 * No eviction is implemented in Phase 3 — disk is monitored at the OS
 * level (~1 MB/entry, 1000-case dataset = ~1 GB max). Phase 4+ may add
 * filesize-based LRU if needed.
 */

import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';

import type { CompileSuccess } from './compile.js';

export function computeCacheKey(
  injectedSource: string,
  pioBoard: string,
  templateName: string,
  platform: string,
  extraBuildFlags?: string[],
  libDepsHash?: string,
): string {
  const h = createHash('sha256');
  // v3 (55.md Phase 2 R4, 2026-05-04): include libDepsHash so cache entries
  // built with one lib_deps configuration cannot serve a different
  // configuration after a deploy. Without this, removing a lib (Heltec
  // pollution, 55.md R1) or adding a lib leaves the previous cache active,
  // making image cutovers invisible to the cache layer — the bug that
  // produced Round 3's 92.6% / Round 4's 79.7% cache-HIT illusions.
  //
  // v2 (BUG-059, 2026-04-29): added platform + extraBuildFlags so that
  //  - the C6 pioarduino fork doesn't share entries with the official
  //    espressif32 platform under the same `esp32-c6-devkitm-1` board id;
  //  - ATOMS3 Lite (USB-CDC build flags) doesn't share entries with the
  //    plain `esp32-s3-devkitc-1` board id from `esp32-s3-generic`.
  // Bumping the prefix invalidates pre-fix cache entries, which are stale
  // anyway after every in-scope change.
  h.update('v3\n');
  h.update(`platform=${platform}\n`);
  h.update(`board=${pioBoard}\n`);
  h.update(`template=${templateName}\n`);
  if (extraBuildFlags && extraBuildFlags.length > 0) {
    h.update(`flags=${extraBuildFlags.slice().sort().join(',')}\n`);
  }
  if (libDepsHash) {
    h.update(`libDeps=${libDepsHash}\n`);
  }
  h.update(injectedSource);
  return h.digest('hex');
}

interface CacheMeta {
  template: string;
  pioBoard: string;
  durationMs: number; // original cold compile duration
  savedAt: string; // ISO timestamp
}

function entryDir(cacheDir: string, key: string): string {
  return path.join(cacheDir, key.slice(0, 2), key);
}

/**
 * Load a cached result by key. Returns null on cache miss or any I/O error
 * (corrupted entries are treated as a miss; the next compile will overwrite).
 */
export function cacheGet(cacheDir: string, key: string): CompileSuccess | null {
  const dir = entryDir(cacheDir, key);
  const firmwarePath = path.join(dir, 'firmware.bin');
  const metaPath = path.join(dir, 'meta.json');
  if (!existsSync(firmwarePath) || !existsSync(metaPath)) return null;

  try {
    const meta = JSON.parse(readFileSync(metaPath, 'utf-8')) as CacheMeta;
    const firmware = readFileSync(firmwarePath).toString('base64');
    const result: CompileSuccess = {
      success: true,
      firmware,
      durationMs: meta.durationMs,
      template: meta.template,
      pioBoard: meta.pioBoard,
    };
    const bootloaderPath = path.join(dir, 'bootloader.bin');
    if (existsSync(bootloaderPath)) {
      result.bootloader = readFileSync(bootloaderPath).toString('base64');
    }
    const partitionsPath = path.join(dir, 'partitions.bin');
    if (existsSync(partitionsPath)) {
      result.partitions = readFileSync(partitionsPath).toString('base64');
    }
    const bootApp0Path = path.join(dir, 'boot_app0.bin');
    if (existsSync(bootApp0Path)) {
      result.bootApp0 = readFileSync(bootApp0Path).toString('base64');
    }
    return result;
  } catch {
    return null;
  }
}

/**
 * Store a successful compile result. Decodes the base64 fields back to raw
 * bytes so cache hits can re-encode (cheap) without bloating disk.
 */
export function cachePut(cacheDir: string, key: string, result: CompileSuccess): void {
  const dir = entryDir(cacheDir, key);
  mkdirSync(dir, { recursive: true });

  writeFileSync(path.join(dir, 'firmware.bin'), Buffer.from(result.firmware, 'base64'));
  if (result.bootloader) {
    writeFileSync(path.join(dir, 'bootloader.bin'), Buffer.from(result.bootloader, 'base64'));
  }
  if (result.partitions) {
    writeFileSync(path.join(dir, 'partitions.bin'), Buffer.from(result.partitions, 'base64'));
  }
  if (result.bootApp0) {
    writeFileSync(path.join(dir, 'boot_app0.bin'), Buffer.from(result.bootApp0, 'base64'));
  }
  const meta: CacheMeta = {
    template: result.template,
    pioBoard: result.pioBoard,
    durationMs: result.durationMs,
    savedAt: new Date().toISOString(),
  };
  writeFileSync(path.join(dir, 'meta.json'), JSON.stringify(meta));
}
