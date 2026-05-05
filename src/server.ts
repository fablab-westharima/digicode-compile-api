/**
 * server.ts — DigiCode Compile API (PlatformIO Core backed).
 *
 * Phase 2 PoC entry. Runs as a long-lived Node process on ML30 (or wherever
 * PlatformIO Core is installed). Drop-in replacement for legacy
 * `arduino-compile-server`'s POST /api/compile, with a planned cutover via
 * port swap (see 44.md §3.9).
 */

import { serve } from '@hono/node-server';
import { Hono } from 'hono';
import { cors } from 'hono/cors';
import { streamSSE } from 'hono/streaming';

import { compile, type CompileRequest } from './compile.js';

const app = new Hono();

// SSE chunk split (BUG-078 / 57.md §2.2): firmware base64 ~1.4-1.7 MB を 64 KB
// chunk に分割して `event: firmware-chunk` を連続送信。bootloader / partitions /
// boot_app0 は raw 19 / 3 / 8 KB → base64 26 / 4 / 11 KB で 64 KB 内に収まるため
// 単一 event。chunk size 64 KB の根拠は WHATWG SSE spec 上限なし + HTTP/2 frame
// 16 KB × multi-frame 安全圏 + base64 効率最適 (raw 48 KB binary → base64 64 KB)。
const FIRMWARE_CHUNK_SIZE = 65_536;

// Cross-origin support — the frontend lives on a different origin from
// this service in every deployment topology we ship:
//   • production CF Pages → ML30 via Cloudflare Tunnel (compile.digital-fab.jp)
//   • production CF Pages → user-local Docker container (localhost:<port>)
//   • Vite dev / preview → user-local Docker container
// compileService.ts sends every POST /api/compile with
// `Content-Type: application/json` + `Accept-Language`, which makes the
// request non-simple, so the browser issues an OPTIONS preflight first.
// Without this middleware the preflight returned 404 and the actual POST
// response had no Access-Control-Allow-Origin header — the browser then
// silently rejected the response, surfacing as 未接続 in the
// "接続テスト" button (cloud and local alike). Missing since the 44.md
// Phase 4 cutover from the legacy arduino-compile-server (Apr 2026);
// reported via the 2026-05-01 user smoke.
app.use(
  '*',
  cors({
    origin: [
      'https://code.fablab-westharima.jp', // production CF Pages
      'http://localhost:5173', // Vite dev
      'http://localhost:4173', // Vite preview
      'http://localhost:3000', // legacy / alt dev
    ],
    allowMethods: ['GET', 'POST', 'OPTIONS'],
    allowHeaders: ['Content-Type', 'Accept-Language'],
    maxAge: 86400, // cache preflight for 1 day
  }),
);

app.get('/health', (c) =>
  c.json({
    status: 'ok',
    service: 'digicode-compile-api',
    version: '0.1.0',
    timestamp: new Date().toISOString(),
  }),
);

app.post('/api/compile', async (c) => {
  const body = (await c.req.json().catch(() => null)) as CompileRequest | null;
  if (!body || typeof body.board !== 'string') {
    return c.json({ success: false, error: 'invalid request body' }, 400);
  }

  const fullPackage = c.req.query('fullPackage') === 'true';
  const result = await compile(body, { fullPackage });
  if (!result.success) {
    return c.json(result, 200); // legacy contract: compile failures return 200 + success:false
  }
  return c.json(result);
});

// SSE streaming endpoint (BUG-078 fix layer 1/3、57.md §2.2):
// Cloudflare Free/Pro/Business plan の edge `proxy_read_timeout = 100s + ±5s`
// に対し、long-running compile (cold 100-200s) は同期 POST /api/compile では
// HTTP 524 で fail する (production user 50-70% 初回 524、第83回 4 axis 実証済)。
//
// 対策 ε = SSE streaming 化 + 3-layer mitigation:
//   layer 1: TTFB 即時 (event:start を起動直後に書き出し、edge timer 満足)
//   layer 2: heartbeat 15s 間隔の SSE comment line で edge timer reset
//            (CF Free 100s に対し 6× safety margin)
//   layer 3: response header `X-Accel-Buffering: no` + `Cache-Control: no-cache,
//            no-transform` で edge / proxy buffering 抑止 (Mintlify postmortem
//            教訓直接適用)。加えて CF dashboard で当該 hostname の auto-
//            compression を Compression Rule で OFF (commit #5 deploy 直前に
//            user 操作、handover docs に明示 checkpoint)。
//
// Event 順序固定 (補足 3 / 57.md §2.2):
//   start → firmware-meta → firmware-chunk × N → bootloader → partitions →
//   boot_app0 → complete (or `error` 途中発火可)
//
// 既存 POST /api/compile (JSON 同期) は CI smoke / automated test / orchestrator
// backward compat 用に残置 (要請 5 確定、削除しない)。frontend / orchestrator は
// commit #3 / #4 で SSE only に切替予定 (要請 3 確定 = heuristic 廃止)。
app.post('/api/compile/sse', async (c) => {
  const body = (await c.req.json().catch(() => null)) as CompileRequest | null;
  if (!body || typeof body.board !== 'string') {
    return c.json({ success: false, error: 'invalid request body' }, 400);
  }
  const fullPackage = c.req.query('fullPackage') === 'true';

  // 3-layer mitigation header (layer 3):
  // - X-Accel-Buffering: no — proxy buffer 抑制 (nginx + 互換 proxy 全て理解)
  // - Cache-Control: no-cache, no-transform — Mintlify postmortem 教訓、
  //   no-transform は CF auto-compression を bypass する RFC 7234 directive。
  //
  // 注意: Hono v4.6 streamSSE は内部で c.header('Cache-Control', 'no-cache')
  // を呼ぶ (`node_modules/hono/dist/helper/streaming/sse.js:59`)、これは
  // 我々の値を上書きする (c.header() default = replace)。よって streamSSE()
  // 戻り Response の headers を post-override する。Hono v4.6 streamSSE は
  // c.newResponse() を sync 返却するため、戻り Response の Headers object
  // は依然 mutable で、return 前に set() で上書き可能。
  c.header('X-Accel-Buffering', 'no');

  const response = streamSSE(c, async (stream) => {
    // heartbeat: SSE comment line `:\n\n` を 15s 間隔で送信して CF edge timer
    // を idle 起点で reset。stream.writeSSE({ data: '' }) は WHATWG spec の
    // empty `data:` 行に展開されるが Hono v4.6 の sse.js 実装では一行 `:\n\n`
    // 形式と等価で、event listener には届かない (frontend の auto reconnect も
    // trigger しない)。`.catch(() => {})` で write fail を flow 中断させない。
    const heartbeat = setInterval(() => {
      stream.writeSSE({ data: '' }).catch(() => {});
    }, 15_000);

    try {
      // Step 1: start (TTFB 即時、edge timer 満足の起動条件)
      await stream.writeSSE({ event: 'start', data: JSON.stringify({ ts: Date.now() }) });

      // 既存 compile() を変更なしで invoke。内部 cache HIT は ~50ms で返却、
      // cold compile は 100-200s 程度。compile 中は heartbeat が edge timer
      // を keep-alive する。
      const result = await compile(body, { fullPackage });

      if (!result.success) {
        await stream.writeSSE({
          event: 'error',
          data: JSON.stringify({
            error: result.error,
            details: result.details,
            stderr: result.stderr,
          }),
        });
        return;
      }

      // Step 2: firmware-meta (totalChunks 通知、進捗 UI 早期駆動、補足 1)
      const firmwareB64 = result.firmware;
      const totalChunks = Math.ceil(firmwareB64.length / FIRMWARE_CHUNK_SIZE);
      await stream.writeSSE({
        event: 'firmware-meta',
        data: JSON.stringify({ totalChunks, totalBytes: firmwareB64.length }),
      });

      // Step 3: firmware-chunk × N (連番整数 id、in-order、補足 3 順序固定)
      for (let seq = 0; seq < totalChunks; seq++) {
        const offset = seq * FIRMWARE_CHUNK_SIZE;
        const chunk = firmwareB64.slice(offset, offset + FIRMWARE_CHUNK_SIZE);
        await stream.writeSSE({
          event: 'firmware-chunk',
          id: String(seq),
          data: JSON.stringify({ seq, total: totalChunks, data: chunk }),
        });
      }

      // Step 4-6: bootloader / partitions / boot_app0 (単一 event、ESP32
      // fullPackage のみ存在、raw 19 / 3 / 8 KB で 64 KB 制限内)
      if (result.bootloader) {
        await stream.writeSSE({
          event: 'bootloader',
          data: JSON.stringify({ data: result.bootloader }),
        });
      }
      if (result.partitions) {
        await stream.writeSSE({
          event: 'partitions',
          data: JSON.stringify({ data: result.partitions }),
        });
      }
      if (result.bootApp0) {
        await stream.writeSSE({
          event: 'boot_app0',
          data: JSON.stringify({ data: result.bootApp0 }),
        });
      }

      // Step 7: complete (metadata 集約)
      await stream.writeSSE({
        event: 'complete',
        data: JSON.stringify({
          success: true,
          durationMs: result.durationMs,
          template: result.template,
          pioBoard: result.pioBoard,
          cached: result.cached,
        }),
      });
    } catch (e) {
      // compile() 内部 throw / streamSSE 内例外を全捕捉、frontend に伝達
      await stream.writeSSE({
        event: 'error',
        data: JSON.stringify({ error: e instanceof Error ? e.message : String(e) }),
      });
    } finally {
      clearInterval(heartbeat);
    }
  });

  // streamSSE() が設定した Cache-Control: no-cache を no-transform 込みに
  // 上書き (RFC 7234 + Mintlify postmortem 教訓直接適用)。
  response.headers.set('Cache-Control', 'no-cache, no-transform');
  return response;
});

const port = Number(process.env.PORT ?? 3002);
const hostname = process.env.HOST ?? '0.0.0.0';

console.log(`[digicode-compile-api] listening on ${hostname}:${port}`);
serve({ fetch: app.fetch, port, hostname });
