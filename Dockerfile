# DigiCode compile-api: PlatformIO Core-based Arduino compile service.
# Replaces legacy ghcr.io/fablab-westharima/digicode-compile-server (arduino-cli).
# Plan: prompt/maintenance/45_2026-04-28_ローカルコンパイルPIO統一計画.md Phase 1.

FROM node:20-slim

# System deps for PlatformIO Core + git URL lib pinning.
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 python3-pip python3-venv git curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# PlatformIO Core, system-wide. Debian bookworm sets PEP 668 EXTERNALLY-MANAGED;
# the container has no other Python user, so --break-system-packages is the
# documented escape valve. Pinned to match what ML30 host runtime uses today.
RUN pip3 install --no-cache-dir --break-system-packages "platformio==6.1.19"

# Pre-install PIO platforms used by compile-api/src/boards.ts.
#   espressif32: esp32 / esp32-s3 / esp32-c3 / esp32-c6 / m5stack family.
#   raspberrypi: Pico / Pico W / XIAO RP2040 / Nano RP2040 Connect (fallback).
# arduino-mbed is intentionally NOT installed — no FQBN in boards.ts maps to it
# (Nano RP2040 Connect uses raspberrypi/pico fallback per boards.ts:37-42).
RUN pio platform install espressif32 raspberrypi \
    && rm -rf /root/.platformio/.cache

# DigiCode custom + version-pinned vendored libs (LIBS_DIR=/opt/digicode-compile/libs).
# Source: arduino-compile-server/libraries/, minus NewPing v1.5
# (replaced by registry teckel12/NewPing@^1.9 in compile-api/src/compile.ts).
COPY libs/ /opt/digicode-compile/libs/

# .ino templates (TEMPLATES_DIR=/opt/digicode-compile/templates). 4 files unchanged from legacy.
COPY templates/ /opt/digicode-compile/templates/

# Hono server. tsx is a devDep but `npm start` invokes it at runtime, so
# we install all deps (npm ci, no --omit=dev). ~30 MB delta vs production-only
# install. Phase 2 polish may add a TS->JS build step to drop devDeps.
WORKDIR /opt/digicode-compile/api
COPY package.json package-lock.json ./
RUN npm ci
COPY tsconfig.json ./
COPY src/ ./src/
COPY scripts/ ./scripts/

# Primer pre-warm: DL frameworks + lib tarballs into the image so a fresh
# container's first compile starts warm. See compile-api/scripts/warmup-pio.ts.
# The primer is allowed to fail at the compile step — what we keep is the
# /root/.platformio/ cache populated before the build phase.
RUN npx tsx scripts/warmup-pio.ts

# Defaults match compile-api/src/compile.ts DEFAULT_ENV. Override via `docker run -e`.
# COMPILE_TIMEOUT_MS bumped 180s -> 300s in 45.md Phase 2 (Q1 = C):
# even with primer pre-warm, fresh-container first compile may still pay for
# lib_deps unpack + initial g++; 5 min is the safety net.
ENV PORT=3001 \
    PIO_BIN=/usr/local/bin/pio \
    PIO_HOME=/root/.platformio \
    TEMPLATES_DIR=/opt/digicode-compile/templates \
    LIBS_DIR=/opt/digicode-compile/libs \
    PROJECTS_DIR=/opt/digicode-compile/projects \
    CACHE_DIR=/opt/digicode-compile/cache \
    COMPILE_TIMEOUT_MS=300000

EXPOSE 3001

# Persistent state: Phase 3 incremental build cache + disk-blob result cache.
# libs/templates/api are image-bundled on purpose — image is the single source of truth.
VOLUME ["/opt/digicode-compile/projects", "/opt/digicode-compile/cache"]

HEALTHCHECK --interval=30s --timeout=10s --start-period=30s --retries=3 \
    CMD curl -fsS "http://localhost:${PORT}/health" || exit 1

CMD ["npx", "tsx", "src/server.ts"]
