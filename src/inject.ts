/**
 * inject.ts — port of legacy `insertUserCode` from
 * `arduino-compile-server/src/index.js:119-165`.
 *
 * The browser side splits Blockly-generated C++ into 4 fragments and POSTs
 * `{includes, globals, setupCode, loopCode}`. The compile-server side
 * substitutes those into a stock template (.ino, kept as-is for now —
 * PlatformIO accepts .ino under `framework = arduino`).
 *
 * Substitution rules (kept identical to legacy for byte-stability across the
 * PIO migration):
 *   1. `includes` lines: appended after the LAST existing `#include` in the
 *      template.
 *   2. `globals`: inserted just before `void userSetup()`.
 *   3. `setupCode`: replaces the body of `void userSetup() { ... }`.
 *   4. `loopCode`:  replaces the body of `void userLoop() { ... }`.
 *
 * The legacy regex patterns are mirrored verbatim; see UAT 1 in 44.md §7
 * for byte-stability checks vs. the legacy server output.
 */

import { isEsp32FamilyPlatform } from './boards.js';

export interface UserFragments {
  includes?: string;
  globals?: string;
  setupCode?: string;
  loopCode?: string;
}

export function injectUserCode(template: string, fragments: UserFragments): string {
  const { includes, globals, setupCode, loopCode } = fragments;
  let result = template;

  // 1) #include block — append after the last existing #include line.
  if (includes && includes.trim()) {
    const lastIncludeMatch = template.match(/#include\s+[<"].*[>"]\s*$/m);
    if (lastIncludeMatch?.index !== undefined) {
      const insertPos = lastIncludeMatch.index + lastIncludeMatch[0].length;
      result = result.slice(0, insertPos) + '\n' + includes + result.slice(insertPos);
    } else {
      result = includes + '\n\n' + result;
    }
  }

  // 2) globals — insert just before `void userSetup()`.
  if (globals && globals.trim()) {
    const userSetupMatch = result.match(/void userSetup\(\)/);
    if (userSetupMatch?.index !== undefined) {
      const insertPos = userSetupMatch.index;
      result = result.slice(0, insertPos) + globals + '\n\n' + result.slice(insertPos);
    }
  }

  // 3) userSetup() body replacement.
  const setupReplacement = `void userSetup() {
  // Blockly から生成されたセットアップコード
${setupCode || '  // No setup code'}
}`;
  result = result.replace(/void userSetup\(\) \{[\s\S]*?\n\}/m, setupReplacement);

  // 4) userLoop() body replacement.
  const loopReplacement = `void userLoop() {
  // Blockly から生成されたループコード
${loopCode || '  // No loop code'}
}`;
  result = result.replace(/void userLoop\(\) \{[\s\S]*?\n\}/m, loopReplacement);

  return result;
}

export type ConnectionType = 'ota' | 'usb' | 'ble';

/**
 * Pick the .ino template for a given (connectionType, platform).
 *
 * The DigiCode{OTA,USB,BLE} templates all rely on ESP32-only includes
 * (WiFi.h / ArduinoOTA / NimBLEDevice / ESP32Servo / nvs_flash) that won't
 * resolve on non-ESP32 platforms (raspberrypi, atmelavr, ststm32, ...).
 * For non-ESP32 targets we always fall back to BasicArduino, which has no
 * includes and just exposes the userSetup/userLoop hooks — safe baseline
 * for any framework=arduino board.
 *
 * Root cause D of 55.md §0.2 (Round 4 passRate recovery): orchestrator +
 * production both used to send connectionType='ota' for every board,
 * pulling DigiCodeOTA's <ESPmDNS.h> / <WiFi.h> into RP2040 builds and
 * failing 4/20 boards' fresh compiles. Routing by platform here makes the
 * server authoritative: even if a future caller sends an ESP32-only
 * connectionType for an RP2040 board, the build still succeeds.
 *
 * For ESP32 family boards, the existing connectionType→template mapping
 * is preserved unchanged.
 */
export function templateNameFor(
  connectionType: ConnectionType | undefined,
  platform: string,
): string {
  if (!isEsp32FamilyPlatform(platform)) {
    return 'BasicArduino';
  }
  switch (connectionType) {
    case 'usb':
      return 'DigiCodeUSB';
    case 'ble':
      return 'DigiCodeBLE';
    case 'ota':
    default:
      return 'DigiCodeOTA';
  }
}
