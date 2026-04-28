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

import { compile, type CompileRequest } from './compile.js';

const app = new Hono();

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
