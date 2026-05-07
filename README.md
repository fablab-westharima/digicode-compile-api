# digicode-compile-api

PlatformIO Core-based Arduino compile API for ESP32, used by [DigiCode](https://code.fablab-westharima.jp).

**License:** MIT
**Distributed as:** `ghcr.io/fablab-westharima/digicode-compile-api:latest` (multi-arch Docker image)

---

## What this does

Receives a compile request:

```jsonc
POST /api/compile
{
  "fqbn": "esp32:esp32:esp32",
  "template": "DigiCodeOTA",
  "fragments": {
    "includes": "...",
    "globals": "...",
    "setupCode": "...",
    "loopCode": "..."
  }
}
```

Returns the compiled `firmware.bin` (base64), with `bootloader.bin` / `partitions.bin` / `boot_app0.bin` for ESP32 fullPackage targets.

`POST /api/compile/sse` streams progress events (Server-Sent Events) for long compiles.

---

## Quick run (consume the public image)

End-users should use [digicode-installer](https://github.com/fablab-westharima/digicode-installer) for a one-command install on macOS / Windows / Linux.

For docker-compose users:

```bash
curl -fsSL https://raw.githubusercontent.com/fablab-westharima/digicode-compile-api/main/docker-compose.local.yml -o docker-compose.yml
docker compose up -d
curl http://localhost:3001/health
```

Then point DigiCode at `http://localhost:3001` (Compile Settings → Local Server).

---

## Build from source

```bash
git clone https://github.com/fablab-westharima/digicode-compile-api.git
cd digicode-compile-api
docker build -t digicode-compile-api:dev .
docker run -d -p 3001:3001 digicode-compile-api:dev
```

The image bakes PlatformIO Core 6.1.19, the `pioarduino` ESP32 toolchain, and pre-warmed `build_cache_dir` for `esp32dev` / `esp32-s3-devkitc-1` / `esp32-c3-devkitm-1`. First-time cold compiles for these targets land in ~50–70 s; non-warmup ESP32 targets (c6, m5stack-fire, atom, m5stamp-pico) land in ~180–190 s.

---

## Architecture

```
[client] → POST /api/compile        (JSON, sync)
       or POST /api/compile/sse     (Server-Sent Events for progress)
            ↓
[Hono / Node 22]
   1. resolve FQBN → PlatformIO target           (src/boards.ts)
   2. load .ino template (baked in image)        (templates/)
   3. inject user fragments
      (includes / globals / setupCode / loopCode) (src/inject.ts)
   4. materialize project under PROJECTS_DIR     (src/projectStore.ts)
   5. run `pio run` with shared build_cache_dir  (src/compile.ts)
   6. read firmware.bin (+ bootloader for ESP32 fullPackage)
   7. respond
[volume: /opt/digicode-compile/projects]  ← per-(board × template) persistent
[volume: /opt/digicode-compile/cache]     ← disk-blob result cache
```

Persistent volumes mean:

| Scenario                             | Wall time |
| ------------------------------------ | --------- |
| First compile after install (cold)   | 50–190 s  |
| Source-only change (warm rebuild)    | ~9.6 s    |
| Identical source compile (cache HIT) | ~1 ms     |

---

## Status

- **Production:** ML30 + Cloudflare Tunnel `compile.digital-fab.jp`
- **Latest tag:** `ghcr.io/fablab-westharima/digicode-compile-api:latest`
- **Replaces:** legacy `arduino-compile-server` (arduino-cli; decommissioned 2026-04-28)

---

## Repository layout

```
.
├── src/                     # Hono server (TypeScript)
│   ├── server.ts            # HTTP endpoints (/health, /api/compile, /api/compile/sse)
│   ├── compile.ts           # PlatformIO invocation + SSE event flow
│   ├── inject.ts            # template + fragment merge
│   ├── boards.ts            # FQBN → PlatformIO target map
│   ├── projectStore.ts      # per-(board × template) persistent project
│   ├── projectLock.ts       # in-process serialization per project key
│   └── cache.ts             # SHA-256 keyed disk-blob result cache
├── scripts/
│   └── warmup-pio.ts        # build-time primer (3-target precompile bake)
├── libs/                    # vendored Arduino libraries
│   ├── DigiCodeHumanoid/    # robotics libraries (Digi Co LLC, MIT)
│   ├── DigiCodeTransform/
│   ├── DigiCodeWheel/
│   ├── Adafruit_NeoPixel/   # LGPL-3.0
│   ├── NimBLE-Arduino/      # Apache-2.0
│   ├── NimBLEOta/           # MIT
│   └── ESP32Servo/
├── templates/               # .ino templates injected into compiled projects
│   ├── BasicArduino.ino
│   ├── DigiCodeUSB.ino
│   ├── DigiCodeBLE.ino
│   └── DigiCodeOTA.ino
├── Dockerfile
├── docker-compose.local.yml
└── package.json
```

---

## License

MIT, including the vendored `libs/DigiCodeHumanoid/`, `libs/DigiCodeTransform/`, `libs/DigiCodeWheel/` (Digi Co LLC custom robotics libraries).

Other vendored libraries under `libs/` retain their original licenses (NimBLE-Arduino: Apache-2.0; Adafruit_NeoPixel: LGPL-3.0; NimBLEOta: MIT; ESP32Servo: see directory).

See `LICENSE` for the full text.

---

## Related projects

- [DigiCode](https://code.fablab-westharima.jp) — Blockly-based ESP32 firmware builder (frontend, not in this repo)
- [digicode-installer](https://github.com/fablab-westharima/digicode-installer) — one-command local install for end-users
- [DigiCode-Finder](https://github.com/fablab-westharima/DigiCode-Finder) — mDNS device discovery helper for WiFi OTA

---

## Contact

Digi Co LLC (合同会社デジコ) — contact@digi-co.jp
