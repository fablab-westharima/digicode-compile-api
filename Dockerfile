# DigiCode compile-api: PlatformIO Core-based Arduino compile service.
# Replaces legacy ghcr.io/fablab-westharima/digicode-compile-server (arduino-cli).
# Plan: prompt/maintenance/45_2026-04-28_ローカルコンパイルPIO統一計画.md Phase 1.

# BUG-059 X2 triage round 6 (2026-04-30): switch from node:20-slim to
# the full node:20 (Debian bookworm). The slim variant is missing
# build-essential / git / curl in many cases, and we already pip-install
# multiple Python deps to satisfy pioarduino's tool chain (round 1-4
# whack-a-mole). The full image lands closer to a real-world dev
# environment, which removes a class of "package not present" surprises
# without changing pioarduino's actual behaviour. ~+800 MB image size is
# acceptable on ML30 (~334 GB free disk).
FROM node:20

# node:20 (full Debian bookworm) ships python3 + git + curl, but does NOT
# ship pip3 by default — round 6 first attempt tripped over `exit code 127
# (command not found)` for pip3. Install python3-pip via apt before any
# pip-driven step.
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3-pip \
    && rm -rf /var/lib/apt/lists/*

# PlatformIO Core + pioarduino's runtime Python deps (BUG-059 X2 triage,
# 2026-04-30). pioarduino's espressif32 builder + tool-esptoolpy depend on
# Python packages that are absent from node:20-slim's default environment:
#   - yaml          → builder/frameworks/component_manager.py:14
#   - jsonschema    → a couple of build-time helpers
#   - rich_click    → tool-esptoolpy/esptool/__init__.py:41
#   - intelhex      → tool-esptoolpy/esptool/cmds.py:15
#   - (likely more) → cryptography, pyserial, ecdsa, ...
# Rather than playing whack-a-mole with each round's missing-module surface
# (rich_click in round 1, intelhex in round 2, ...), pull the full upstream
# `esptool` distribution — its setup.cfg dependencies (intelhex,
# cryptography, ecdsa, pyserial, reedsolo, bitstring, rich_click, ...) all
# install into /usr/local/lib/python3.11/dist-packages where pioarduino's
# bundled tool-esptoolpy/esptool.py resolves them. The pip-installed
# esptool wheel sits alongside but tool-esptoolpy continues to win on
# PATH; the duplication adds <10 MB and removes an entire class of
# missing-dep bug. pyyaml / jsonschema stay explicit because they're not
# pulled by esptool.
#
# `click<8.2` (BUG-059 X2 triage round 4): click 8.2 removed the optional
# `ctx` parameter on `ParamType.get_metavar()` (pioarduino's
# tool-esptoolpy/esptool/cli_util.py:295 calls the no-arg form). Pinning
# below 8.2 keeps the older signature so the bundled CLI initialises.
# Debian bookworm sets PEP 668 EXTERNALLY-MANAGED; the container has no
# other Python user, so --break-system-packages is the documented escape.
RUN pip3 install --no-cache-dir --break-system-packages \
        "platformio==6.1.19" \
        "pyyaml" \
        "jsonschema" \
        "click<8.2" \
        "esptool"

# raspberrypi platform install removed (amendment 9 v3++, 2026-05-07):
# 56.md (2026-05-05) deleted every RP2040 board mapping from
# compile-api/src/boards.ts (DigiCode is ESP32-only), so the
# `pio platform install raspberrypi` step that previously installed
# toolchain-gccarmnoneeabi (~458 MB virtual) was carrying dead weight.
# Saves ~150 MB transport size for end-user image distribution. The
# pioarduino fork drives every ESP32 target via the platformio.ini
# `platform = <url>` route (lazy-install at warmup-pio.ts time).

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
