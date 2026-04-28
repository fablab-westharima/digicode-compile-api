# digicode-compile-api

PlatformIO Core-backed compile API for DigiCode.

**Status:** 🟡 Phase 2 PoC (44.md §5.3)
**Replaces:** legacy `arduino-compile-server` (Node + arduino-cli) — see 44.md
**Plan doc:** `prompt/maintenance/44_2026-04-28_compile-infra再設計計画.md`

## Architecture (single-server, ML30)

```
[browser] → POST /api/compile (legacy contract preserved)
            ↓
[Hono / Node 22]
   1. resolve FQBN → PlatformIO target
   2. load .ino template (volume mount, /opt/digicode-compile/templates)
   3. inject user fragments (includes/globals/setupCode/loopCode)
   4. materialize tmp project → run `pio run`
   5. read firmware.bin (+ bootloader/partitions/boot_app0 for ESP32 fullPackage)
   6. respond
[volume mount: /opt/digicode-compile/libs/]  ← DigiCode custom libs (CRUD via SSH)
```

## Local dev

```bash
cd compile-api
npm install
PIO_BIN=$HOME/.local/bin/pio \
TEMPLATES_DIR=/opt/digicode-compile/templates \
LIBS_DIR=/opt/digicode-compile/libs \
npm run dev
```

## ML30 deploy (Phase 3, port 3004 to coexist with legacy on 3002)

```bash
# Sync
rsync -avz --exclude node_modules --exclude .pio compile-api/ ml30:/opt/digicode-compile/api/

# On ML30
cd /opt/digicode-compile/api && npm install && npm start
```

## Cutover plan (44.md §3.9)

1. New API server runs on port 3004 alongside legacy on port 3002
2. UAT against the 60-case baseline preserved at
   `variants/ota/frontend/scripts/probabilistic-debug-results/2026-04-28_12-37_p1_partial-60_pre-jobs-test/`
3. Bin-equality check: legacy vs PIO firmware.bin SHA-256 for representative cases
4. Cloudflare Tunnel reroute `compile.digital-fab.jp` from `:3002` → `:3004`
5. Decommission legacy `arduino-compile-server` container, free ML30 resources
