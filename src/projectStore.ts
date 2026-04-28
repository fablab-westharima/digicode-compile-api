/**
 * projectStore.ts — Persistent PlatformIO projects per (board × template).
 *
 * Phase 3 replaces the per-request tmp dir of Phase 2 with one durable
 * project per (pioBoard, templateName). PlatformIO keeps `.pio/build/<env>/`
 * around between runs, so source-only edits go through the incremental
 * build path (~2-3s) instead of a full re-link (~60s).
 *
 * The platformio.ini is rewritten on every ensure() call. PIO detects the
 * file mtime and only rebuilds when content actually differs, so this is
 * cheap — and it keeps the project config in sync with code-side changes
 * to lib_deps without manual flush.
 *
 * Concurrency: callers must wrap edits + `pio run` in `withLock(projectKey)`
 * (see projectLock.ts). PIO does not handle two concurrent builds in the
 * same project dir.
 */

import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';

export interface ProjectInit {
  /** Stable project key, used for the dir name + lock key. */
  key: string;
  /** Absolute path to the persistent project dir. */
  projectDir: string;
}

export function projectKey(pioBoard: string, templateName: string): string {
  return `${pioBoard}_${templateName}`;
}

export function ensurePersistentProject(
  projectsRoot: string,
  pioBoard: string,
  templateName: string,
  iniContent: string,
): ProjectInit {
  const key = projectKey(pioBoard, templateName);
  const projectDir = path.join(projectsRoot, key);
  const srcDir = path.join(projectDir, 'src');
  if (!existsSync(srcDir)) {
    mkdirSync(srcDir, { recursive: true });
  }
  const iniPath = path.join(projectDir, 'platformio.ini');
  // Only rewrite when content differs — avoids touching the file (and the
  // mtime PIO uses for cache invalidation) on every request.
  let needWrite = true;
  if (existsSync(iniPath)) {
    try {
      needWrite = readFileSync(iniPath, 'utf-8') !== iniContent;
    } catch {
      needWrite = true;
    }
  }
  if (needWrite) {
    writeFileSync(iniPath, iniContent);
  }
  return { key, projectDir };
}

export function writeMainIno(projectDir: string, source: string): void {
  // PIO reads `src/main.ino` for arduino framework. Always overwrite —
  // PIO's incremental build hashes the file, so an identical write is a no-op.
  writeFileSync(path.join(projectDir, 'src', 'main.ino'), source);
}
