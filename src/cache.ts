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
): string {
  const h = createHash('sha256');
  h.update('v1\n'); // bump if cache schema changes
  h.update(`board=${pioBoard}\n`);
  h.update(`template=${templateName}\n`);
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
