#!/usr/bin/env bash
# DigiMotion - platform abstraction guard (Phase A-γ commit 3)
#
# Enforces 59.md §1.0.1 + D-new-2 RP2040 future-portability discipline:
#
#   Layer 2 (src/actuator/) channels are allowed to use ESP32-specific HW
#     APIs (LEDC, ESP32Servo, FastAccelStepper, AccelStepper) inside their
#     own #ifdef ARDUINO_ARCH_ESP32 blocks — they are the platform-specific
#     leaves of the design — but MUST NOT spawn FreeRTOS tasks directly.
#     Task management lives in Layer 1 (src/pump/FreeRtosBackgroundPump_*).
#
#   Layer 3+ (src/motion/, src/sound/, src/trim/) MUST be platform-agnostic.
#     No FreeRTOS API, no LEDC, no ESP32Servo, no FastAccelStepper, no
#     AccelStepper, no Arduino-ESP32-specific headers. These layers depend
#     only on the abstract IActuatorChannel / IBackgroundPump / IBuzzer /
#     ITrimStore interfaces.
#
# A new platform (e.g. RP2040) is added by:
#   - Layer 1: dropping a SdkAlarmBackgroundPump_rp2040.cpp under src/pump/
#   - Layer 2: adding *_rp2040 .cpp/.h variants of channels under
#              src/actuator/, each guarded by its own #ifdef ARDUINO_ARCH_RP2040
#   - Layer 3+: NO changes required
#
# This script exits 0 if clean, 1 on any violation. Run from anywhere.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$LIB_DIR/src"

EXIT=0
TOTAL_VIOLATIONS=0

# --- helpers --------------------------------------------------------------

# Run a grep gate against the given directory. Args:
#   $1 = pattern (extended regex)
#   $2 = search directory (relative to lib root)
#   $3 = human label
check_gate() {
    local pattern="$1"
    local rel_dir="$2"
    local label="$3"
    local abs_dir="$LIB_DIR/$rel_dir"

    if [ ! -d "$abs_dir" ]; then
        printf "  [skip] %-60s (dir missing)\n" "$label"
        return
    fi

    local hits
    hits=$(grep -rnE "$pattern" "$abs_dir" --include="*.h" --include="*.cpp" --include="*.hpp" 2>/dev/null || true)
    local count=0
    if [ -n "$hits" ]; then
        count=$(printf '%s\n' "$hits" | wc -l | tr -d ' ')
    fi

    if [ "$count" -ne 0 ]; then
        printf "  [FAIL] %-60s %d hits\n" "$label" "$count"
        printf '%s\n' "$hits" | head -10 | sed 's/^/         /'
        EXIT=1
        TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + count))
    else
        printf "  [ OK ] %-60s 0 hits\n" "$label"
    fi
}

# --- gates -----------------------------------------------------------------

printf "DigiMotion platform abstraction guard (59.md §1.0.1, D-new-2)\n"
printf "Lib root: %s\n\n" "$LIB_DIR"

printf "Layer 2 (src/actuator/) — FreeRTOS task API forbidden:\n"
check_gate 'xTaskCreatePinnedToCore|xTaskCreate\b|vTaskDelay|vTaskDelete|TaskHandle_t|portTICK_PERIOD_MS' \
           "src/actuator" \
           "actuator/ free of FreeRTOS task API"

printf "\nLayer 3+ (src/motion, src/sound, src/trim) — all ESP32 HW API forbidden:\n"
for layer in motion sound trim; do
    check_gate 'xTaskCreatePinnedToCore|xTaskCreate\b|vTaskDelay|vTaskDelete|TaskHandle_t|portTICK_PERIOD_MS|ledcAttach|ledcWrite|ledcDetach|<ESP32Servo\.h>|<FastAccelStepper\.h>|<AccelStepper\.h>|<freertos/' \
               "src/$layer" \
               "${layer}/ free of ESP32-specific HW API"
done

printf "\n"
if [ "$EXIT" -eq 0 ]; then
    printf "✅ Platform abstraction clean. Layer 2+ can be re-targeted to a new\n"
    printf "   platform by adding sibling _<platform>.cpp files under src/pump/\n"
    printf "   and src/actuator/ only; src/motion / src/sound / src/trim require\n"
    printf "   no changes.\n"
else
    printf "❌ Platform abstraction violated: %d total hits across the gates above.\n" "$TOTAL_VIOLATIONS"
    printf "   Either move the offending include/API call into a Layer 1 pump impl\n"
    printf "   or a Layer 2 channel impl (whichever is appropriate), or replace it\n"
    printf "   with an abstract IF call (IBackgroundPump / IActuatorChannel etc).\n"
fi

exit $EXIT
