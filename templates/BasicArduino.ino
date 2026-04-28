/*
 * DigiCode Basic Arduino Firmware
 * RP2040/Arduino AVR用の基本テンプレート
 *
 * 機能:
 * - Blocklyから生成されたコードの実行
 * - シリアル通信
 * - USB書き込み専用
 */

// ユーザー定義グローバル変数・関数
// ここにBlocklyのグローバルコードが挿入されます

void userSetup() {
  // ここにBlocklyのセットアップコードが挿入されます
}

void userLoop() {
  // ここにBlocklyのループコードが挿入されます
}

void setup() {
  // シリアル通信初期化
  Serial.begin(115200);
  delay(100);

  Serial.println("\n\n================================");
  Serial.println("DigiCode Basic Firmware");
  Serial.println("Version: 1.0.0");
  Serial.println("================================\n");

  // 内蔵LED設定（存在する場合）
  #ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  #endif

  // ユーザー定義のセットアップ
  userSetup();

  Serial.println("\n[Setup] Completed");
}

void loop() {
  // ユーザー定義のループ
  userLoop();
}
