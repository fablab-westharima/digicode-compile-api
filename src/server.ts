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

import { compile, type CompileRequest } from './compile.js';

const app = new Hono();

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

const port = Number(process.env.PORT ?? 3002);
const hostname = process.env.HOST ?? '0.0.0.0';

console.log(`[digicode-compile-api] listening on ${hostname}:${port}`);
serve({ fetch: app.fetch, port, hostname });
