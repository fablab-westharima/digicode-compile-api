/**
 * DigiCode USB版ファームウェア
 *
 * USB書込み専用テンプレート（WiFi/OTA機能なし）
 *
 * 特徴:
 * - WiFi, mDNS, OTA, WebServerを全て削除
 * - シリアルコマンド最小限
 * - サイズ: 200-300KB（OTAの1/4）
 */

#include <ESP32Servo.h>
#include <Preferences.h>

// UUID（デバイス識別子）
String deviceUUID = "";
const int UUID_LENGTH = 16;

// Preferences (NVS) instance — post-Phase 4-4 commit 2-5 (case_0207-0212 fix):
// preferences_begin generator は redefinition 回避のため declare せず、template
// 側の global instance に依存する設計 (storageNvsBlocks.ts:42-51 参照)。OTA
// template (DigiCodeOTA.ino:56) では既に declare 済、本 template (USB) と
// DigiCodeBLE.ino でも declare して 3 template 統一。Round 1 RCA cluster #5 の
// redefinition は OTA だけで declare していた非対称が原因、本修正で解消。
// See rules/digicode/03-block-workflow.md "Init block protocol".
Preferences preferences;

// サーボ設定
// NOTE: ESP32Servo.h defines MAX_SERVOS as 16, so we use USER_MAX_SERVOS here
const int USER_MAX_SERVOS = 8;
Servo servos[USER_MAX_SERVOS];
int servoPins[USER_MAX_SERVOS] = {-1, -1, -1, -1, -1, -1, -1, -1};
int servoCount = 0;

// ============================================
// PID gain runtime (Phase D-3、Session 148、case 23 incident F transport-side 解消)
// ============================================
// PID gain は SET_PID:name,kp,ki,kd command で runtime 上書き、 user code が getPidGain で読出。
// (Phase 3-F Session 156: トリム経路 A 廃止 = ServoTrimDialog pinPresetStore 直書き compile-time
//  反映に統一、 SET_TRIM/SET_TRIMS/GET_TRIMS/SAVE_TRIMS/TEST_TRIM commands + servoTrims[] +
//  applyTrimsToActuators + Preferences "servo_trim" namespace 全削除。)
#define MAX_PID_INSTANCES 4
struct PidGainEntry { String name; float kp; float ki; float kd; };
PidGainEntry pidGains[MAX_PID_INSTANCES];
int pidGainCount = 0;

// シリアル通信バッファ
String serialBuffer = "";

void setup() {
  // シリアル通信初期化
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  delay(1000);
  Serial.println("\n\n=== DigiCode USB ===");
  Serial.println("Version: 1.0.0");
  Serial.println("Mode: USB");

  // UUID生成または読み込み
  generateOrLoadUUID();
  Serial.print("UUID: ");
  Serial.println(deviceUUID);

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
// PID gain runtime accessor (Phase D-3、case 23 incident F transport-side 解消)
// ============================================

void setPidGain(String name, float kp, float ki, float kd) {
  for (int i = 0; i < pidGainCount; i++) {
    if (pidGains[i].name == name) {
      pidGains[i].kp = kp; pidGains[i].ki = ki; pidGains[i].kd = kd;
      Serial.printf("[PID] Updated %s: kp=%.4f ki=%.4f kd=%.4f\n", name.c_str(), kp, ki, kd);
      return;
    }
  }
  if (pidGainCount < MAX_PID_INSTANCES) {
    pidGains[pidGainCount].name = name;
    pidGains[pidGainCount].kp = kp;
    pidGains[pidGainCount].ki = ki;
    pidGains[pidGainCount].kd = kd;
    pidGainCount++;
    Serial.printf("[PID] Added %s: kp=%.4f ki=%.4f kd=%.4f\n", name.c_str(), kp, ki, kd);
  } else {
    Serial.println("[PID] ERROR: MAX_PID_INSTANCES reached");
  }
}

bool getPidGain(String name, float& kp, float& ki, float& kd) {
  for (int i = 0; i < pidGainCount; i++) {
    if (pidGains[i].name == name) {
      kp = pidGains[i].kp; ki = pidGains[i].ki; kd = pidGains[i].kd;
      return true;
    }
  }
  return false;
}

/**
 * UUID生成または読み込み
 */
void generateOrLoadUUID() {
  // 簡易UUID生成（ESP32のMACアドレスベース）
  uint64_t chipid = ESP.getEfuseMac();
  char uuidStr[17];
  snprintf(uuidStr, sizeof(uuidStr), "%016llX", chipid);
  deviceUUID = String(uuidStr);
}

/**
 * シリアルコマンド処理
 */
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

/**
 * コマンド実行
 */
void processCommand(String command) {
  command.trim();

  if (command == "GET_CONFIG") {
    // デバイス設定取得
    Serial.println("OK:UUID=" + deviceUUID);
    Serial.print("OK:SERVO_COUNT=");
    Serial.println(servoCount);
    Serial.println("OK:MODE=USB");

  } else if (command.startsWith("SET_SERVO_CONFIG:")) {
    // サーボ設定
    // 形式: SET_SERVO_CONFIG:count,pin1,pin2,...
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

  // ============================================
  // Phase D-3 (Session 148、 case 23 incident F transport-side 解消):
  // SET_PID line command (SerialPidTransport から送信、 59.md §1-4.3 verbatim)
  // (Phase 3-F Session 156: SET_TRIM / SET_TRIMS / GET_TRIMS / SAVE_TRIMS / TEST_TRIM 全削除、
  //  経路 A 廃止 = ServoTrimDialog pinPresetStore 直書き compile-time 反映に統一。)
  // ============================================
  } else if (command.startsWith("SET_PID:")) {
    // 形式: SET_PID:name,kp,ki,kd
    String params = command.substring(8);
    int c1 = params.indexOf(',');
    int c2 = (c1 >= 0) ? params.indexOf(',', c1 + 1) : -1;
    int c3 = (c2 >= 0) ? params.indexOf(',', c2 + 1) : -1;
    if (c1 > 0 && c2 > c1 && c3 > c2) {
      String name = params.substring(0, c1);
      float kp = params.substring(c1 + 1, c2).toFloat();
      float ki = params.substring(c2 + 1, c3).toFloat();
      float kd = params.substring(c3 + 1).toFloat();
      setPidGain(name, kp, ki, kd);
      Serial.println("OK:PID_SET");
    } else {
      Serial.println("ERROR:INVALID_PID");
    }

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
