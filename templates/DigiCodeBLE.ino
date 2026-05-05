/**
 * DigiCode BLE版ファームウェア
 *
 * BLE OTA書込み対応テンプレート（WiFi機能なし）
 *
 * 特徴:
 * - h2zero/NimBLEOtaライブラリ使用
 * - WiFi, mDNS, ArduinoOTA機能なし
 * - BLE経由でOTA更新対応
 * - デバイス名カスタマイズ対応（NVS保存）
 * - シリアルコマンドでデバイス名設定
 *
 * 依存ライブラリ:
 * - NimBLE-Arduino v2.4.0+
 * - h2zero/NimBLEOta
 */

#include <NimBLEDevice.h>
#include <NimBLEOta.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <nvs.h>

// BLE OTA
static NimBLEOta bleOta;

// Preferences (NVS) instance — post-Phase 4-4 commit 2-5 (case_0207-0212 fix):
// preferences_begin generator は redefinition 回避のため declare せず、template
// 側の global instance に依存する設計 (storageNvsBlocks.ts:42-51 参照)。OTA
// template (DigiCodeOTA.ino:56) では既に declare 済、本 template (BLE) と
// DigiCodeUSB.ino でも declare して 3 template 統一。Round 1 RCA cluster #5 の
// redefinition は OTA だけで declare していた非対称が原因、本修正で解消。
// See rules/digicode/03-block-workflow.md "Init block protocol".
Preferences preferences;

// UUID（デバイス識別子）
String deviceUUID = "";
const int UUID_LENGTH = 16;

// デバイス名（ユーザー設定可能、BLE広告名に使用）
String device_name = "";

// サーボ設定
// NOTE: ESP32Servo.h defines MAX_SERVOS as 16, so we use USER_MAX_SERVOS here
const int USER_MAX_SERVOS = 8;
Servo servos[USER_MAX_SERVOS];
int servoPins[USER_MAX_SERVOS] = {-1, -1, -1, -1, -1, -1, -1, -1};
int servoCount = 0;

// シリアル通信バッファ
String serialBuffer = "";

void setup() {
  // シリアル通信初期化
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  delay(1000);
  Serial.println("\n\n=== DigiCode BLE ===");
  Serial.println("Version: 2.1.0");
  Serial.println("Mode: BLE");

  // UUID生成（MACアドレスベース）
  uint64_t chipid = ESP.getEfuseMac();
  char uuidStr[17];
  snprintf(uuidStr, sizeof(uuidStr), "%016llX", chipid);
  deviceUUID = String(uuidStr);

  // NVSからデバイス設定を読み込み（NimBLE init前）
  // NVS直接APIを使用するためPreferencesとのmutex競合は発生しない
  loadDeviceConfigNvs();

  // BLE名: デバイス名が設定されていればそれを使用、なければUUIDの先頭8文字
  String nameBase = (device_name.length() > 0) ? device_name : deviceUUID.substring(0, 8);
  String bleName = "DigiCode-" + nameBase;
  // BLE広告名は29バイト制限があるため切り詰め
  if (bleName.length() > 29) {
    bleName = bleName.substring(0, 29);
  }
  Serial.print("BLE Name: ");
  Serial.println(bleName);

  // 正しいBLE名でNimBLEを初期化（init時の名前がGATTとアドバタイズに使用される）
  NimBLEDevice::init(bleName.c_str());

  // BLE OTAサービス開始
  NimBLEService* pOtaService = bleOta.start();

  // BLEアドバタイジング設定・開始
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

  // デバイス名を広告データに明示的に追加
  pAdvertising->setName(bleName.c_str());
  pAdvertising->addServiceUUID(pOtaService->getUUID());
  pAdvertising->enableScanResponse(true);
  pAdvertising->setPreferredParams(0x06, 0x12);  // 接続パラメータ設定

  // 広告開始
  bool advStarted = pAdvertising->start();

  Serial.print("BLE Advertising started: ");
  Serial.println(advStarted ? "SUCCESS" : "FAILED");
  Serial.println("BLE OTA Ready.");
  Serial.println("Ready.");

  // ユーザー定義のsetup処理を呼び出し
  userSetup();
}

void loop() {
  // シリアルコマンド処理
  handleSerialCommands();

  // ユーザー定義のloop処理を呼び出し
  userLoop();

  delay(10);
}

// ============================================
// NVS直接アクセス（Preferencesライブラリを使わない）
// NimBLEとPreferencesは内部NVS mutexが競合するため、
// ESP-IDFのNVS APIを直接使用する
// ============================================

/**
 * デバイス設定をNVSから読み込み
 * OTA版と同じ "digicode" ネームスペースを使用（設定引き継ぎ可能）
 */
void loadDeviceConfigNvs() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("digicode", NVS_READONLY, &handle);
  if (err != ESP_OK) {
    Serial.println("Device config: NVS not found, using defaults");
    Serial.println("  UUID: " + deviceUUID);
    Serial.println("  Name: (not set)");
    return;
  }

  // UUID読み込み
  char uuidBuf[64] = {0};
  size_t uuidLen = sizeof(uuidBuf);
  if (nvs_get_str(handle, "uuid", uuidBuf, &uuidLen) == ESP_OK && uuidLen > 1) {
    deviceUUID = String(uuidBuf);
  }

  // デバイス名読み込み
  char nameBuf[64] = {0};
  size_t nameLen = sizeof(nameBuf);
  if (nvs_get_str(handle, "name", nameBuf, &nameLen) == ESP_OK && nameLen > 1) {
    device_name = String(nameBuf);
  }

  nvs_close(handle);

  Serial.println("Device config loaded:");
  Serial.println("  UUID: " + deviceUUID);
  Serial.println("  Name: " + (device_name.length() > 0 ? device_name : "(not set)"));
}

/**
 * デバイス設定をNVSに保存
 */
void saveDeviceConfigNvs() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("digicode", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    Serial.println("ERROR: Failed to open NVS for writing");
    return;
  }

  nvs_set_str(handle, "uuid", deviceUUID.c_str());
  nvs_set_str(handle, "name", device_name.c_str());
  nvs_commit(handle);
  nvs_close(handle);

  Serial.println("Device config saved!");
  Serial.println("  UUID: " + deviceUUID);
  Serial.println("  Name: " + (device_name.length() > 0 ? device_name : "(not set)"));
}

// ============================================
// シリアルコマンド処理
// ============================================

void handleSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        processCommand(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
}

void processCommand(String command) {
  command.trim();

  if (command == "GET_CONFIG") {
    // デバイス設定取得
    Serial.println("OK:UUID=" + deviceUUID);
    Serial.println("OK:NAME=" + (device_name.length() > 0 ? device_name : ""));
    Serial.print("OK:SERVO_COUNT=");
    Serial.println(servoCount);
    Serial.println("OK:MODE=BLE");

  } else if (command.startsWith("SET_NAME:") || command.startsWith("SET_DEVICE_NAME:")) {
    // デバイス名設定（OTA版と互換）
    String newName = command.startsWith("SET_DEVICE_NAME:")
      ? command.substring(16)
      : command.substring(9);
    newName.trim();
    if (newName.length() > 0 && newName.length() <= 20) {
      device_name = newName;
      Serial.println("OK:NAME=" + device_name);
      Serial.println("INFO:Use SAVE_CONFIG to persist. Restart to apply new BLE name.");
    } else {
      Serial.println("ERROR:NAME_LENGTH (1-20 chars)");
    }

  } else if (command == "GET_DEVICE_NAME") {
    Serial.println("DEVICE_NAME:" + device_name);

  } else if (command == "SAVE_CONFIG") {
    // デバイス設定をNVSに保存
    saveDeviceConfigNvs();
    Serial.println("OK:CONFIG_SAVED");
    Serial.println("OK:UUID=" + deviceUUID);
    Serial.println("OK:NAME=" + device_name);

  } else if (command.startsWith("SET_SERVO_CONFIG:")) {
    // サーボ設定
    String params = command.substring(17);
    int commaIndex = params.indexOf(',');

    if (commaIndex > 0) {
      int count = params.substring(0, commaIndex).toInt();
      servoCount = min(count, USER_MAX_SERVOS);

      String pinsStr = params.substring(commaIndex + 1);
      int pinIndex = 0;
      int startPos = 0;

      for (int i = 0; i < servoCount && pinIndex < USER_MAX_SERVOS; i++) {
        int nextComma = pinsStr.indexOf(',', startPos);
        String pinStr;

        if (nextComma > 0) {
          pinStr = pinsStr.substring(startPos, nextComma);
          startPos = nextComma + 1;
        } else {
          pinStr = pinsStr.substring(startPos);
        }

        int pin = pinStr.toInt();
        servoPins[pinIndex] = pin;
        servos[pinIndex].attach(pin);
        pinIndex++;
      }

      Serial.println("OK:SERVO_CONFIG_SET");
    } else {
      Serial.println("ERROR:INVALID_SERVO_CONFIG");
    }

  } else if (command == "RESET") {
    // リセット
    Serial.println("OK:RESETTING");
    delay(100);
    ESP.restart();

  } else {
    // 未知のコマンド
    Serial.print("ERROR:UNKNOWN_COMMAND:");
    Serial.println(command);
  }
}

/**
 * ユーザー定義のsetup処理
 * このメソッドの中身はコンパイル時にBlocklyから生成されたコードで置き換えられます
 */
void userSetup() {
  // Blocklyから生成されたセットアップコードがここに挿入されます
}

/**
 * ユーザー定義のloop処理
 * このメソッドの中身はコンパイル時にBlocklyから生成されたコードで置き換えられます
 */
void userLoop() {
  // Blocklyから生成されたループコードがここに挿入されます
}
