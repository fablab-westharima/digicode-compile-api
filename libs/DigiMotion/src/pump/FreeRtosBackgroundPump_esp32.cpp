/*
 * DigiMotion - FreeRTOS-backed BackgroundPump (ESP32 platform impl, Layer 1)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original ESP32 implementation. Generalises the inline
 * _servoBackgroundTask + _ServoState[40] pattern previously embedded in the
 * monorepo's servoBlocks.ts (Session 138). Source is new; not git-mv'd.
 *
 * Platform abstraction (59.md §1.0.1): FreeRTOS API usage is confined to
 * this single file. The entire body is guarded by ARDUINO_ARCH_ESP32 so
 * that the native (host) unit-test env compiles it to an empty translation
 * unit. Tests instantiate PortableBackgroundPump directly.
 */

#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "IBackgroundPump.h"

namespace {

// Concrete ESP32 pump. A single FreeRTOS task pinned to core 1 calls
// tick(millis()) every 1 ms, scanning all registered IPumpable slots.
class FreeRtosBackgroundPump : public PortableBackgroundPump {
public:
    FreeRtosBackgroundPump() : _taskHandle(nullptr) {}

    bool start() override {
        if (_taskHandle != nullptr) return true;
        BaseType_t ok = xTaskCreatePinnedToCore(
            _taskFn, "digiMotionPump", 2048, this, 1, &_taskHandle, 1);
        if (ok != pdPASS) {
            _taskHandle = nullptr;
            return false;
        }
        return true;
    }

    void stop() override {
        if (_taskHandle == nullptr) return;
        TaskHandle_t h = _taskHandle;
        _taskHandle = nullptr;
        vTaskDelete(h);
    }

private:
    static void _taskFn(void* arg) {
        FreeRtosBackgroundPump* self = static_cast<FreeRtosBackgroundPump*>(arg);
        for (;;) {
            if (self->_taskHandle == nullptr) {
                vTaskDelete(nullptr);
                return;
            }
            self->tick(millis());
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }

    TaskHandle_t _taskHandle;
};

FreeRtosBackgroundPump _esp32PumpInstance;

} // namespace

IBackgroundPump& getBackgroundPump() {
    return _esp32PumpInstance;
}

#endif // ARDUINO_ARCH_ESP32
