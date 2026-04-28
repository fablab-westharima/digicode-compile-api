/*
 * DigiCode OTA Firmware
 * 競技ロボット教育プラットフォーム用ファームウェア
 *
 * 機能:
 * - WiFi Stationモード（複数WiFi保存、フォールバックでAPモード）
 * - OTA (Over-The-Air) アップデート（Arduino OTA + HTTP）
 * - HTTP Webサーバー（デバイス情報、OTA更新UI）
 * - シリアル通信
 * - デバイスUUID/名前の永続化（Preferences）
 * - WiFi設定の永続化（最大5個）
 * - Blocklyから生成されたコードの実行
 */

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <Update.h>
#include <ESPping.h>

// WiFi設定
const char* ap_password = "digicode2025";
const int MAX_WIFI_COUNT = 5;

// WiFi保存用構造体（APごとに固定IP設定を持つ）
struct WiFiCredential {
  String ssid;
  String password;
  String staticIp;      // 固定IP（空の場合はDHCP）
  String gateway;       // ゲートウェイ
  String subnet;        // サブネット（デフォルト: 255.255.255.0）
};

// WiFi設定リスト
WiFiCredential wifiList[MAX_WIFI_COUNT];
int wifiCount = 0;
String connectedSSID = "";  // 現在接続中のSSID
int connectedIndex = -1;    // 現在接続中のWiFiインデックス
bool isAPMode = false;      // APモードかどうか

// 後方互換性のための変数（現在接続中のAPの固定IP情報を保持）
String static_ip = "";
String static_gateway = "";
String static_subnet = "";
String static_dns = "";
bool useStaticIP = false;

// デバイス識別用UUID（Preferencesから読み込み）
String device_uuid = "robot001";  // デフォルト値（フロントエンドで自動置換される）
String device_name = "My Robot";   // デフォルト値

// Preferences（NVS）
Preferences preferences;

// シリアルコマンドバッファ
String serialBuffer = "";

// Webサーバー（Port 80）
WebServer server(80);

// ============================================
// サーボトリム機能（v1.6.0追加）
// ============================================
#define MAX_SERVO_TRIM 16
int servoTrims[MAX_SERVO_TRIM] = {0};
int servoTrimCount = 0;
Preferences trimPrefs;

// 内蔵LED設定（ESP32では通常GPIO2）
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif
const int LED_PIN = LED_BUILTIN;

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n\n================================");
  Serial.println("DigiCode OTA Firmware");
  Serial.println("Version: 1.8.0");
  Serial.println("================================\n");

  // 内蔵LED設定
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Preferencesから設定を読み込み
  loadDeviceConfig();
  loadWiFiList();
  loadServoTrims();  // v1.6.0: サーボトリム値を読み込み

  // WiFi接続試行（Stationモード）
  // mDNSホスト名: device_nameが設定されていればそれを使用、なければuuidをフォールバック
  String hostnameBase = (device_name.length() > 0 && device_name != "My Robot") ? device_name : device_uuid;
  String hostname = "digicode-" + hostnameBase;
  bool wifiConnected = connectToWiFi();

  if (!wifiConnected) {
    // WiFi接続失敗 → APモードで起動
    isAPMode = true;
    Serial.println("Starting Access Point mode...");
    WiFi.mode(WIFI_AP);

    String ssid = "DigiCode-" + device_uuid;
    WiFi.softAP(ssid.c_str(), ap_password);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP SSID: ");
    Serial.println(ssid);
    Serial.print("AP Password: ");
    Serial.println(ap_password);
    Serial.print("AP IP address: ");
    Serial.println(IP);
  } else {
    // WiFi接続成功
    isAPMode = false;
    Serial.println("Connected to WiFi: " + connectedSSID);
  }

  // mDNS設定
  if (MDNS.begin(hostname.c_str())) {
    Serial.print("mDNS responder started: ");
    Serial.print(hostname);
    Serial.println(".local");

    // mDNSサービス登録（デバイス探索用）
    // _digicode._tcp.local サービスとして登録
    MDNS.addService("digicode", "tcp", 80);
    MDNS.addServiceTxt("digicode", "tcp", "uuid", device_uuid.c_str());
    MDNS.addServiceTxt("digicode", "tcp", "name", device_name.c_str());
    MDNS.addServiceTxt("digicode", "tcp", "version", "1.8.0");
    Serial.println("mDNS service registered: _digicode._tcp");
  }

  // OTA設定
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword(ap_password);

  ArduinoOTA.onStart([&]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
    digitalWrite(LED_PIN, HIGH);
  });

  ArduinoOTA.onEnd([&]() {
    Serial.println("\nUpdate complete!");
    digitalWrite(LED_PIN, LOW);
  });

  ArduinoOTA.onProgress([&](unsigned int progress, unsigned int total) {
    static unsigned int lastPercent = 0;
    unsigned int percent = (progress / (total / 100));
    if (percent != lastPercent && percent % 10 == 0) {
      Serial.printf("Progress: %u%%\n", percent);
      lastPercent = percent;
    }
    // LED点滅でOTA進行を表示
    digitalWrite(LED_PIN, (millis() / 100) % 2);
  });

  ArduinoOTA.onError([&](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
    digitalWrite(LED_PIN, LOW);
  });

  ArduinoOTA.begin();

  // 起動完了サマリー（モードに応じて表示内容を変更）
  Serial.println("\n========== BOOT SUMMARY ==========");
  Serial.println("System Ready!");
  Serial.println("Device UUID: " + device_uuid);
  Serial.println("Device Name: " + device_name);

  if (isAPMode) {
    Serial.println("Mode: AP Mode (WiFi connection failed)");
    Serial.println("WARNING: No saved WiFi or connection failed!");
    Serial.println("AP SSID: DigiCode-" + device_uuid);
    Serial.println("AP Password: " + String(ap_password));
    Serial.println("AP IP: 192.168.4.1");
    Serial.println("Action: Connect to AP and configure WiFi settings.");
  } else {
    Serial.println("Mode: Station Mode (Connected)");
    Serial.println("Connected to: " + connectedSSID);
    if (useStaticIP) {
      Serial.println("IP Mode: Static");
      Serial.println("IP Address: " + static_ip);
    } else {
      Serial.println("IP Mode: DHCP");
      Serial.println("IP Address: " + WiFi.localIP().toString());
    }
    Serial.println("OTA Ready: http://" + WiFi.localIP().toString());
  }
  Serial.println("===================================\n");

  // Webサーバー設定
  setupWebServer();

  // 起動完了を示すLED点滅
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }

  // ユーザー初期化コード
  userSetup();
}

void loop() {
  // OTAハンドラ（必須）
  ArduinoOTA.handle();

  // Webサーバーハンドラ
  server.handleClient();

  // シリアルコマンド処理
  handleSerialCommand();

  // ユーザーループコード
  userLoop();

  // 接続クライアント数表示（APモード時のみ、デバッグ用）
  static unsigned long lastClientCheck = 0;
  if (isAPMode && millis() - lastClientCheck > 10000) {  // 10秒ごと
    int clients = WiFi.softAPgetStationNum();
    if (clients > 0) {
      Serial.print("Connected clients: ");
      Serial.println(clients);
    }
    lastClientCheck = millis();
  }
}

// ============================================
// 設定管理（Preferences / NVS）
// ============================================

void loadDeviceConfig() {
  preferences.begin("digicode", false);  // Read-Write mode

  // UUIDを読み込み（保存されていなければデフォルト値）
  device_uuid = preferences.getString("uuid", "robot001");
  device_name = preferences.getString("name", "My Robot");

  preferences.end();

  Serial.println("Device config loaded:");
  Serial.println("  UUID: " + device_uuid);
  Serial.println("  Name: " + device_name);
}

// WiFi接続後に呼び出して、グローバル変数を更新
void updateStaticIpInfo() {
  if (connectedIndex >= 0 && wifiList[connectedIndex].staticIp.length() > 0) {
    static_ip = wifiList[connectedIndex].staticIp;
    static_gateway = wifiList[connectedIndex].gateway;
    static_subnet = wifiList[connectedIndex].subnet;
    static_dns = wifiList[connectedIndex].gateway;  // DNSはゲートウェイと同じ
    useStaticIP = true;
  } else {
    static_ip = "";
    static_gateway = "";
    static_subnet = "";
    static_dns = "";
    useStaticIP = false;
  }
}

void saveDeviceConfig() {
  preferences.begin("digicode", false);

  preferences.putString("uuid", device_uuid);
  preferences.putString("name", device_name);

  preferences.end();

  Serial.println("Device config saved!");
  Serial.println("  UUID: " + device_uuid);
  Serial.println("  Name: " + device_name);
}

// ============================================
// WiFi管理（複数WiFi保存・接続）
// ============================================

void loadWiFiList() {
  preferences.begin("digicode", false);

  wifiCount = preferences.getInt("wifi_count", 0);
  if (wifiCount > MAX_WIFI_COUNT) wifiCount = MAX_WIFI_COUNT;

  Serial.printf("Loading %d WiFi credentials...\n", wifiCount);

  for (int i = 0; i < wifiCount; i++) {
    String ssidKey = "wifi_" + String(i) + "_ssid";
    String passKey = "wifi_" + String(i) + "_pass";
    String ipKey = "wifi_" + String(i) + "_ip";
    String gwKey = "wifi_" + String(i) + "_gw";
    String snKey = "wifi_" + String(i) + "_sn";

    wifiList[i].ssid = preferences.getString(ssidKey.c_str(), "");
    wifiList[i].password = preferences.getString(passKey.c_str(), "");
    wifiList[i].staticIp = preferences.getString(ipKey.c_str(), "");
    wifiList[i].gateway = preferences.getString(gwKey.c_str(), "");
    wifiList[i].subnet = preferences.getString(snKey.c_str(), "255.255.255.0");

    if (wifiList[i].staticIp.length() > 0) {
      Serial.printf("  [%d] %s (Static: %s)\n", i, wifiList[i].ssid.c_str(), wifiList[i].staticIp.c_str());
    } else {
      Serial.printf("  [%d] %s (DHCP)\n", i, wifiList[i].ssid.c_str());
    }
  }

  preferences.end();
}

void saveWiFiList() {
  preferences.begin("digicode", false);

  preferences.putInt("wifi_count", wifiCount);

  for (int i = 0; i < wifiCount; i++) {
    String ssidKey = "wifi_" + String(i) + "_ssid";
    String passKey = "wifi_" + String(i) + "_pass";
    String ipKey = "wifi_" + String(i) + "_ip";
    String gwKey = "wifi_" + String(i) + "_gw";
    String snKey = "wifi_" + String(i) + "_sn";

    preferences.putString(ssidKey.c_str(), wifiList[i].ssid);
    preferences.putString(passKey.c_str(), wifiList[i].password);
    preferences.putString(ipKey.c_str(), wifiList[i].staticIp);
    preferences.putString(gwKey.c_str(), wifiList[i].gateway);
    preferences.putString(snKey.c_str(), wifiList[i].subnet);
  }

  preferences.end();

  Serial.println("WiFi list saved!");
}

bool addWiFi(String ssid, String password) {
  // 既に存在するかチェック
  for (int i = 0; i < wifiCount; i++) {
    if (wifiList[i].ssid == ssid) {
      // 既存のパスワードを更新
      wifiList[i].password = password;
      Serial.println("WiFi updated: " + ssid);
      return true;
    }
  }

  // 新規追加
  if (wifiCount >= MAX_WIFI_COUNT) {
    Serial.println("ERROR: WiFi list full (max 5)");
    return false;
  }

  wifiList[wifiCount].ssid = ssid;
  wifiList[wifiCount].password = password;
  wifiCount++;

  Serial.println("WiFi added: " + ssid);
  return true;
}

bool removeWiFi(String ssid) {
  int foundIndex = -1;

  for (int i = 0; i < wifiCount; i++) {
    if (wifiList[i].ssid == ssid) {
      foundIndex = i;
      break;
    }
  }

  if (foundIndex == -1) {
    Serial.println("ERROR: WiFi not found: " + ssid);
    return false;
  }

  // リストを詰める
  for (int i = foundIndex; i < wifiCount - 1; i++) {
    wifiList[i] = wifiList[i + 1];
  }

  wifiCount--;
  Serial.println("WiFi removed: " + ssid);
  return true;
}

void listWiFi() {
  Serial.println("=== Saved WiFi List ===");
  Serial.printf("Count: %d / %d\n", wifiCount, MAX_WIFI_COUNT);

  if (wifiCount == 0) {
    Serial.println("  (No WiFi saved)");
  } else {
    for (int i = 0; i < wifiCount; i++) {
      String mask = "";
      for (int j = 0; j < wifiList[i].password.length(); j++) {
        mask += "*";
      }

      String status = "";
      if (wifiList[i].ssid == connectedSSID) {
        status = " [CONNECTED]";
      }

      // 固定IP情報を追加
      String ipInfo = "";
      if (wifiList[i].staticIp.length() > 0) {
        ipInfo = " [Static: " + wifiList[i].staticIp + "]";
      } else {
        ipInfo = " [DHCP]";
      }

      Serial.printf("  [%d] %s (password: %s)%s%s\n",
                    i,
                    wifiList[i].ssid.c_str(),
                    mask.c_str(),
                    ipInfo.c_str(),
                    status.c_str());
    }
  }
  Serial.println("=======================");
}

void clearAllWiFi() {
  wifiCount = 0;
  preferences.begin("digicode", false);
  preferences.putInt("wifi_count", 0);
  preferences.end();
  Serial.println("All WiFi credentials cleared!");
}

// ============================================
// サーボトリム機能（v1.6.0追加）
// ============================================

// トリム値をNVSから読み込み
void loadServoTrims() {
  trimPrefs.begin("servo_trim", true);  // Read-only
  servoTrimCount = trimPrefs.getInt("count", 0);
  if (servoTrimCount > MAX_SERVO_TRIM) servoTrimCount = MAX_SERVO_TRIM;

  for (int i = 0; i < servoTrimCount && i < MAX_SERVO_TRIM; i++) {
    String key = "s" + String(i);
    servoTrims[i] = trimPrefs.getInt(key.c_str(), 0);
  }
  trimPrefs.end();

  Serial.printf("[TRIM] Loaded %d servo trims: ", servoTrimCount);
  for (int i = 0; i < servoTrimCount; i++) {
    Serial.printf("%d ", servoTrims[i]);
  }
  Serial.println();
}

// トリム値をNVSに保存
void saveServoTrims() {
  trimPrefs.begin("servo_trim", false);  // Read-Write
  trimPrefs.putInt("count", servoTrimCount);

  for (int i = 0; i < servoTrimCount && i < MAX_SERVO_TRIM; i++) {
    String key = "s" + String(i);
    trimPrefs.putInt(key.c_str(), servoTrims[i]);
  }
  trimPrefs.end();

  Serial.println("[TRIM] Saved to NVS");
}

// トリム値を設定（範囲: -30〜+30）
void setServoTrim(int index, int value) {
  if (index >= 0 && index < MAX_SERVO_TRIM) {
    servoTrims[index] = constrain(value, -30, 30);
    Serial.printf("[TRIM] Set servo %d trim to %d\n", index, servoTrims[index]);
  }
}

// トリム値を取得
int getServoTrim(int index) {
  if (index >= 0 && index < MAX_SERVO_TRIM) {
    return servoTrims[index];
  }
  return 0;
}

// サーボ数を設定
void setServoTrimCount(int count) {
  servoTrimCount = constrain(count, 0, MAX_SERVO_TRIM);
  Serial.printf("[TRIM] Servo count set to %d\n", servoTrimCount);
}

// 簡易JSON数値パース（"key":value形式）
int parseJsonInt(String json, String key) {
  String searchKey = "\"" + key + "\":";
  int keyIndex = json.indexOf(searchKey);
  if (keyIndex < 0) return -9999;  // Not found marker

  int valueStart = keyIndex + searchKey.length();
  int valueEnd = valueStart;

  // 数値の終端を探す（,か}か]）
  while (valueEnd < json.length()) {
    char c = json.charAt(valueEnd);
    if (c == ',' || c == '}' || c == ']') break;
    valueEnd++;
  }

  String valueStr = json.substring(valueStart, valueEnd);
  valueStr.trim();
  return valueStr.toInt();
}

// 簡易JSON配列パース（"key":[n,n,n]形式）
int parseJsonArray(String json, String key, int* arr, int maxLen) {
  String searchKey = "\"" + key + "\":[";
  int keyIndex = json.indexOf(searchKey);
  if (keyIndex < 0) return 0;

  int arrayStart = keyIndex + searchKey.length();
  int arrayEnd = json.indexOf("]", arrayStart);
  if (arrayEnd < 0) return 0;

  String arrayStr = json.substring(arrayStart, arrayEnd);
  int count = 0;
  int lastComma = -1;

  for (int i = 0; i <= arrayStr.length() && count < maxLen; i++) {
    if (i == arrayStr.length() || arrayStr.charAt(i) == ',') {
      String numStr = arrayStr.substring(lastComma + 1, i);
      numStr.trim();
      if (numStr.length() > 0) {
        arr[count++] = numStr.toInt();
      }
      lastComma = i;
    }
  }

  return count;
}

// IPアドレス文字列をIPAddressに変換するヘルパー関数
IPAddress stringToIP(String ipStr) {
  int parts[4];
  int partIndex = 0;
  int lastDot = 0;

  for (int i = 0; i <= ipStr.length() && partIndex < 4; i++) {
    if (i == ipStr.length() || ipStr.charAt(i) == '.') {
      parts[partIndex++] = ipStr.substring(lastDot, i).toInt();
      lastDot = i + 1;
    }
  }

  return IPAddress(parts[0], parts[1], parts[2], parts[3]);
}

bool connectToWiFi() {
  if (wifiCount == 0) {
    Serial.println("No WiFi credentials saved. Starting AP mode...");
    return false;
  }

  Serial.println("Trying to connect to saved WiFi networks...");

  WiFi.mode(WIFI_STA);

  // Phase 1: 利用可能なSSIDをスキャン
  Serial.println("\n=== Scanning available networks ===");
  int networkCount = WiFi.scanNetworks();
  Serial.printf("Found %d networks:\n", networkCount);
  for (int j = 0; j < networkCount; j++) {
    Serial.printf("  [%d] %s (RSSI: %d, Ch: %d)\n",
                  j, WiFi.SSID(j).c_str(), WiFi.RSSI(j), WiFi.channel(j));
  }
  Serial.println("===================================\n");

  for (int i = 0; i < wifiCount; i++) {
    Serial.printf("[%d/%d] Trying: %s\n", i + 1, wifiCount, wifiList[i].ssid.c_str());

    // Phase 2-3: SSIDが利用可能か確認
    bool ssidFound = false;
    int8_t rssi = 0;
    for (int j = 0; j < networkCount; j++) {
      if (WiFi.SSID(j) == wifiList[i].ssid) {
        ssidFound = true;
        rssi = WiFi.RSSI(j);
        break;
      }
    }

    if (!ssidFound) {
      Serial.println("  SSID not found in scan. Skipping...\n");
      continue;  // 次のAPへ
    } else {
      Serial.printf("  SSID found! (RSSI: %d)\n", rssi);
    }

    // Phase 2-1: 前回の接続をクリア
    Serial.println("  Disconnecting previous connection...");
    WiFi.disconnect(true);
    delay(100);

    // IP設定をクリア
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

    // Phase 1: 詳細な設定情報を表示
    Serial.println("=== Connection attempt details ===");
    Serial.printf("  Target SSID: %s\n", wifiList[i].ssid.c_str());

    // APごとの固定IP設定がある場合は適用
    bool staticIpConfigured = false;
    if (wifiList[i].staticIp.length() > 0) {
      IPAddress ip = stringToIP(wifiList[i].staticIp);
      IPAddress gateway = stringToIP(wifiList[i].gateway);
      IPAddress subnet = stringToIP(wifiList[i].subnet);
      IPAddress dns = gateway;  // DNSはゲートウェイと同じ

      Serial.println("  Configuring static IP:");
      Serial.printf("    IP: %s\n", wifiList[i].staticIp.c_str());
      Serial.printf("    Gateway: %s\n", wifiList[i].gateway.c_str());
      Serial.printf("    Subnet: %s\n", wifiList[i].subnet.c_str());

      if (!WiFi.config(ip, gateway, subnet, dns)) {
        // Phase 2-2: 固定IP設定失敗時はDHCPで試行
        Serial.println("  ERROR: Static IP configuration failed!");
        Serial.println("  Falling back to DHCP mode...");
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        staticIpConfigured = false;
      } else {
        staticIpConfigured = true;
      }
    } else {
      Serial.println("  Using DHCP mode");
    }
    Serial.println("==================================");

    Serial.print("  Connecting");
    WiFi.begin(wifiList[i].ssid.c_str(), wifiList[i].password.c_str());

    // 10秒待機
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();

    // Phase 1: 接続結果の詳細を表示
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("  ✓ Connection SUCCESS!");
      connectedSSID = wifiList[i].ssid;
      connectedIndex = i;
      updateStaticIpInfo();  // グローバル変数を更新
      Serial.println("\n=== Connected WiFi Info ===");
      Serial.printf("  SSID: %s\n", connectedSSID.c_str());
      Serial.printf("  IP address: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
      Serial.printf("  Subnet: %s\n", WiFi.subnetMask().toString().c_str());
      Serial.printf("  DNS: %s\n", WiFi.dnsIP().toString().c_str());
      Serial.printf("  IP Mode: %s\n", staticIpConfigured ? "Static" : "DHCP");
      Serial.println("============================\n");
      return true;
    } else {
      // Phase 1: 失敗理由を詳細に表示
      Serial.println("  ✗ Connection FAILED");
      Serial.printf("  WiFi Status Code: %d\n", WiFi.status());

      // ステータスコードの意味を表示
      switch(WiFi.status()) {
        case WL_NO_SSID_AVAIL:
          Serial.println("  Reason: SSID not available (AP out of range or not broadcasting)");
          break;
        case WL_CONNECT_FAILED:
          Serial.println("  Reason: Connection failed (wrong password or AP rejected)");
          break;
        case WL_CONNECTION_LOST:
          Serial.println("  Reason: Connection lost");
          break;
        case WL_DISCONNECTED:
          Serial.println("  Reason: Disconnected");
          break;
        case WL_IDLE_STATUS:
          Serial.println("  Reason: Idle (WiFi.begin() not called or in progress)");
          break;
        default:
          Serial.println("  Reason: Unknown error");
          break;
      }
      Serial.println();
    }
  }

  connectedIndex = -1;
  Serial.println("All WiFi attempts failed. Starting AP mode...");
  return false;
}

// ============================================
// シリアルコマンド処理
// ============================================

void handleSerialCommand() {
  while (Serial.available() > 0) {
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

  if (command.startsWith("SET_UUID:")) {
    String newUuid = command.substring(9);
    newUuid.trim();

    if (newUuid.length() >= 3 && newUuid.length() <= 20) {
      device_uuid = newUuid;
      Serial.println("OK:UUID=" + device_uuid);
    } else {
      Serial.println("ERROR:Invalid UUID length (3-20 chars required)");
    }
  }
  else if (command.startsWith("SET_NAME:") || command.startsWith("SET_DEVICE_NAME:")) {
    // SET_NAME と SET_DEVICE_NAME の両方をサポート（互換性のため）
    String newName = command.startsWith("SET_DEVICE_NAME:")
      ? command.substring(16)
      : command.substring(9);
    newName.trim();

    if (newName.length() > 0 && newName.length() <= 32) {
      device_name = newName;
      Serial.println("OK:DEVICE_NAME_SET");
      Serial.println("OK:NAME=" + device_name);
    } else if (newName.length() > 32) {
      Serial.println("ERROR:Device name too long (max 32 chars)");
    } else {
      Serial.println("ERROR:Invalid device name");
    }
  }
  else if (command == "GET_DEVICE_NAME") {
    Serial.println("DEVICE_NAME:" + device_name);
  }
  else if (command == "SAVE_CONFIG") {
    saveDeviceConfig();
    Serial.println("OK:CONFIG_SAVED");
    Serial.println("INFO:Please restart ESP32 to apply new settings");
  }
  else if (command == "GET_CONFIG") {
    Serial.println("OK:UUID=" + device_uuid);
    Serial.println("OK:NAME=" + device_name);
    Serial.println("OK:SSID=DigiCode-" + device_uuid);
    if (connectedIndex >= 0 && wifiList[connectedIndex].staticIp.length() > 0) {
      Serial.println("OK:CONNECTED_SSID=" + connectedSSID);
      Serial.println("OK:IP_MODE=STATIC");
      Serial.println("OK:STATIC_IP=" + wifiList[connectedIndex].staticIp);
      Serial.println("OK:GATEWAY=" + wifiList[connectedIndex].gateway);
      Serial.println("OK:SUBNET=" + wifiList[connectedIndex].subnet);
    } else if (connectedIndex >= 0) {
      Serial.println("OK:CONNECTED_SSID=" + connectedSSID);
      Serial.println("OK:IP_MODE=DHCP");
      Serial.println("OK:CURRENT_IP=" + WiFi.localIP().toString());
    } else {
      Serial.println("OK:IP_MODE=NOT_CONNECTED");
    }
  }
  else if (command == "USE_CURRENT_IP") {
    // 現在のDHCP IPをそのまま固定IPとして設定
    if (WiFi.status() != WL_CONNECTED || connectedIndex < 0) {
      Serial.println("ERROR:WiFi not connected. Connect to WiFi first.");
    } else {
      String currentIP = WiFi.localIP().toString();
      String currentGateway = WiFi.gatewayIP().toString();
      String currentSubnet = WiFi.subnetMask().toString();

      wifiList[connectedIndex].staticIp = currentIP;
      wifiList[connectedIndex].gateway = currentGateway;
      wifiList[connectedIndex].subnet = currentSubnet;

      saveWiFiList();

      Serial.println("OK:USE_CURRENT_IP_SET");
      Serial.println("OK:SSID=" + connectedSSID);
      Serial.println("OK:STATIC_IP=" + currentIP);
      Serial.println("OK:GATEWAY=" + currentGateway);
      Serial.println("OK:SUBNET=" + currentSubnet);
      Serial.println("INFO:Static IP set for " + connectedSSID + ". Will be used on next connection.");
    }
  }
  else if (command.startsWith("SET_STATIC_IP:")) {
    // フォーマット: SET_STATIC_IP:192.168.1.100,192.168.1.1,255.255.255.0
    // 現在接続中のAPに固定IPを設定
    if (connectedIndex < 0) {
      Serial.println("ERROR:WiFi not connected. Connect to WiFi first.");
    } else {
      String params = command.substring(14);
      int comma1 = params.indexOf(',');
      int comma2 = params.indexOf(',', comma1 + 1);

      if (comma1 > 0 && comma2 > comma1) {
        String ip = params.substring(0, comma1);
        String gw = params.substring(comma1 + 1, comma2);
        String sn = params.substring(comma2 + 1);

        ip.trim();
        gw.trim();
        sn.trim();

        wifiList[connectedIndex].staticIp = ip;
        wifiList[connectedIndex].gateway = gw;
        wifiList[connectedIndex].subnet = sn;

        saveWiFiList();

        Serial.println("OK:STATIC_IP_SET");
        Serial.println("OK:SSID=" + connectedSSID);
        Serial.println("OK:STATIC_IP=" + ip);
        Serial.println("OK:GATEWAY=" + gw);
        Serial.println("OK:SUBNET=" + sn);
        Serial.println("INFO:Restart to apply static IP.");
      } else {
        Serial.println("ERROR:Invalid format. Use: SET_STATIC_IP:ip,gateway,subnet");
      }
    }
  }
  else if (command == "CLEAR_STATIC_IP") {
    // 現在接続中のAPの固定IP設定をクリア
    if (connectedIndex < 0) {
      Serial.println("ERROR:WiFi not connected.");
    } else {
      wifiList[connectedIndex].staticIp = "";
      wifiList[connectedIndex].gateway = "";
      wifiList[connectedIndex].subnet = "255.255.255.0";

      saveWiFiList();

      Serial.println("OK:STATIC_IP_CLEARED");
      Serial.println("OK:SSID=" + connectedSSID);
      Serial.println("INFO:IP mode set to DHCP for " + connectedSSID + ". Restart to apply.");
    }
  }
  else if (command == "AUTO_STATIC_IP") {
    // WiFi接続中のみ有効 - 現在接続中のAPに自動で空きIPを設定
    if (WiFi.status() != WL_CONNECTED || connectedIndex < 0) {
      Serial.println("ERROR:WiFi not connected. Connect to WiFi first.");
    } else {
      Serial.println("INFO:Scanning for available IP address...");
      IPAddress gatewayIP = WiFi.gatewayIP();
      IPAddress subnetIP = WiFi.subnetMask();
      String foundIP = "";

      // 100-200の範囲でスキャン
      for (int i = 100; i <= 200; i++) {
        IPAddress testIP(gatewayIP[0], gatewayIP[1], gatewayIP[2], i);
        if (testIP == WiFi.localIP()) continue;

        if (!Ping.ping(testIP, 1)) {
          delay(10);
          if (!Ping.ping(testIP, 1)) {
            foundIP = testIP.toString();
            Serial.print("INFO:Found available IP: ");
            Serial.println(foundIP);
            break;
          }
        }
      }

      if (foundIP.length() > 0) {
        wifiList[connectedIndex].staticIp = foundIP;
        wifiList[connectedIndex].gateway = gatewayIP.toString();
        wifiList[connectedIndex].subnet = subnetIP.toString();

        saveWiFiList();

        Serial.println("OK:AUTO_STATIC_IP_SET");
        Serial.println("OK:SSID=" + connectedSSID);
        Serial.println("OK:IP=" + foundIP);
        Serial.println("OK:GATEWAY=" + wifiList[connectedIndex].gateway);
        Serial.println("OK:SUBNET=" + wifiList[connectedIndex].subnet);
        Serial.println("INFO:Restart to apply static IP.");
      } else {
        Serial.println("ERROR:No available IP found in range 100-200");
      }
    }
  }
  else if (command == "RESTART") {
    Serial.println("OK:RESTARTING");
    delay(1000);
    ESP.restart();
  }
  else if (command.startsWith("ADD_WIFI:")) {
    // フォーマット: ADD_WIFI:ssid,password
    // 接続テストを行い、成功した場合のみ保存する
    String params = command.substring(9);
    int commaIndex = params.indexOf(',');

    if (commaIndex > 0) {
      String ssid = params.substring(0, commaIndex);
      String password = params.substring(commaIndex + 1);

      ssid.trim();
      password.trim();

      Serial.println("INFO:Testing WiFi connection...");
      Serial.println("INFO:SSID=" + ssid);

      // 現在の接続を切断
      WiFi.disconnect(true);
      delay(100);
      WiFi.mode(WIFI_STA);
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

      // 接続試行
      Serial.print("INFO:Connecting");
      WiFi.begin(ssid.c_str(), password.c_str());

      // 10秒待機（20回 × 500ms）
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
      }
      Serial.println();

      if (WiFi.status() == WL_CONNECTED) {
        // 接続成功 → 保存
        Serial.println("INFO:Connection test SUCCESS!");
        Serial.println("INFO:IP=" + WiFi.localIP().toString());

        if (addWiFi(ssid, password)) {
          saveWiFiList();
          // 接続状態を更新
          connectedSSID = ssid;
          for (int i = 0; i < wifiCount; i++) {
            if (wifiList[i].ssid == ssid) {
              connectedIndex = i;
              break;
            }
          }
          isAPMode = false;
          Serial.println("OK:WIFI_ADDED:CONNECTED");
        } else {
          Serial.println("ERROR:WIFI_ADD_FAILED");
        }
      } else {
        // 接続失敗 → 保存しない
        int statusCode = WiFi.status();
        Serial.println("ERROR:WIFI_TEST_FAILED:" + String(statusCode));

        // 失敗理由を詳細に表示
        switch(statusCode) {
          case WL_NO_SSID_AVAIL:
            Serial.println("ERROR:Reason=SSID not found");
            break;
          case WL_CONNECT_FAILED:
            Serial.println("ERROR:Reason=Wrong password or AP rejected");
            break;
          case WL_CONNECTION_LOST:
            Serial.println("ERROR:Reason=Connection lost");
            break;
          case WL_DISCONNECTED:
            Serial.println("ERROR:Reason=Disconnected");
            break;
          default:
            Serial.println("ERROR:Reason=Unknown error");
            break;
        }

        // APモードに戻す
        Serial.println("INFO:Reverting to AP mode...");
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_AP);
        String apSsid = "DigiCode-" + device_uuid;
        WiFi.softAP(apSsid.c_str(), ap_password);
        isAPMode = true;
        connectedSSID = "";
        connectedIndex = -1;
        Serial.println("INFO:AP mode started: " + apSsid);
      }
    } else {
      Serial.println("ERROR:Invalid format. Use: ADD_WIFI:ssid,password");
    }
  }
  else if (command.startsWith("REMOVE_WIFI:")) {
    String ssid = command.substring(12);
    ssid.trim();

    if (removeWiFi(ssid)) {
      saveWiFiList();
      Serial.println("OK:WIFI_REMOVED");
    } else {
      Serial.println("ERROR:WIFI_REMOVE_FAILED");
    }
  }
  else if (command == "LIST_WIFI") {
    listWiFi();
    Serial.println("OK:WIFI_LIST_SHOWN");
  }
  else if (command == "GET_CURRENT_WIFI") {
    if (isAPMode) {
      Serial.println("OK:MODE=AP");
      Serial.println("OK:SSID=DigiCode-" + device_uuid);
    } else {
      Serial.println("OK:MODE=STATION");
      Serial.println("OK:CONNECTED_SSID=" + connectedSSID);
      Serial.println("OK:IP=" + WiFi.localIP().toString());
    }
  }
  else if (command == "CLEAR_WIFI") {
    clearAllWiFi();
    Serial.println("OK:WIFI_CLEARED");
    Serial.println("INFO:Please restart ESP32 to apply changes");
  }
  else {
    // 未知のコマンドは無視（ユーザーコード実行など）
  }
}

// ============================================
// Webサーバー設定
// ============================================

// CORSヘッダーを追加するヘルパー関数
void sendCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void setupWebServer() {
  // CORS Preflight対応
  server.on("/", HTTP_OPTIONS, [](){
    sendCORSHeaders();
    server.send(204);
  });

  // ルート: デバイス情報ページ
  server.on("/", [](){
    sendCORSHeaders();
    String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>DigiCode Device Info</title>
  <style>
    body { font-family: Arial; margin: 40px; background: #f0f0f0; }
    .container { max-width: 600px; margin: auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }
    .info { margin: 20px 0; }
    .info-item { margin: 10px 0; padding: 10px; background: #f9f9f9; border-left: 3px solid #4CAF50; }
    .info-label { font-weight: bold; color: #666; }
    .info-value { color: #333; }
    .button { display: inline-block; padding: 12px 24px; background: #4CAF50; color: white; text-decoration: none; border-radius: 5px; margin-top: 20px; }
    .button:hover { background: #45a049; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🤖 DigiCode Device</h1>
    <div class="info">
      <div class="info-item">
        <span class="info-label">Device Name:</span>
        <span class="info-value">)" + device_name + R"(</span>
      </div>
      <div class="info-item">
        <span class="info-label">Device UUID:</span>
        <span class="info-value">)" + device_uuid + R"(</span>
      </div>
      <div class="info-item">
        <span class="info-label">WiFi Mode:</span>
        <span class="info-value">)" + String(isAPMode ? "AP Mode" : "Station Mode") + R"(</span>
      </div>
      <div class="info-item">
        <span class="info-label">)" + String(isAPMode ? "AP SSID:" : "Connected to:") + R"(</span>
        <span class="info-value">)" + String(isAPMode ? "DigiCode-" + device_uuid : connectedSSID) + R"(</span>
      </div>
      <div class="info-item">
        <span class="info-label">IP Address:</span>
        <span class="info-value">)" + String(isAPMode ? "192.168.4.1" : WiFi.localIP().toString()) + R"(</span>
      </div>
      <div class="info-item">
        <span class="info-label">mDNS:</span>
        <span class="info-value">digicode-)" + device_uuid + R"(.local</span>
      </div>
      <div class="info-item">
        <span class="info-label">Firmware Version:</span>
        <span class="info-value">1.7.0</span>
      </div>
    </div>
    <a href="/update" class="button">🔄 OTA Update</a>
  </div>
</body>
</html>
    )";
    server.send(200, "text/html", html);
  });

  // API: デバイス情報（JSON）- mDNS検索用
  server.on("/api/info", HTTP_OPTIONS, [](){
    sendCORSHeaders();
    server.send(204);
  });

  server.on("/api/info", HTTP_GET, [](){
    sendCORSHeaders();
    String json = "{";
    json += "\"name\":\"" + device_name + "\",";
    json += "\"uuid\":\"" + device_uuid + "\",";
    json += "\"ip\":\"" + (isAPMode ? String("192.168.4.1") : WiFi.localIP().toString()) + "\",";
    json += "\"mdns\":\"digicode-" + device_uuid + ".local\",";
    json += "\"mode\":\"" + String(isAPMode ? "ap" : "sta") + "\",";
    json += "\"ssid\":\"" + (isAPMode ? String("DigiCode-" + device_uuid) : connectedSSID) + "\",";
    json += "\"ipMode\":\"" + String(useStaticIP ? "static" : "dhcp") + "\",";
    if (useStaticIP) {
      json += "\"staticIp\":\"" + static_ip + "\",";
      json += "\"gateway\":\"" + static_gateway + "\",";
      json += "\"subnet\":\"" + static_subnet + "\",";
    }
    json += "\"version\":\"1.7.0\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  // ルート: OTA更新フォーム
  server.on("/update", HTTP_GET, [](){
    String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>DigiCode OTA Update</title>
  <style>
    body { font-family: Arial; margin: 40px; background: #f0f0f0; }
    .container { max-width: 600px; margin: auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; border-bottom: 2px solid #FF5722; padding-bottom: 10px; }
    .warning { background: #FFF3CD; border: 1px solid #FFE69C; padding: 15px; border-radius: 5px; margin: 20px 0; }
    .form-group { margin: 20px 0; }
    label { display: block; margin-bottom: 5px; font-weight: bold; color: #666; }
    input[type="file"] { width: 100%; padding: 10px; border: 2px dashed #ccc; border-radius: 5px; }
    button { width: 100%; padding: 15px; background: #FF5722; color: white; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; }
    button:hover { background: #E64A19; }
    .progress { display: none; margin-top: 20px; }
    .progress-bar { width: 100%; height: 30px; background: #f0f0f0; border-radius: 5px; overflow: hidden; }
    .progress-fill { height: 100%; background: #4CAF50; transition: width 0.3s; }
    .progress-text { text-align: center; margin-top: 10px; font-weight: bold; }
    .success { background: #D4EDDA; border: 2px solid #4CAF50; padding: 20px; border-radius: 5px; color: #155724; font-size: 18px; }
    .error { background: #F8D7DA; border: 2px solid #DC3545; padding: 20px; border-radius: 5px; color: #721C24; font-size: 18px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🔄 ファームウェアOTA更新</h1>
    <div class="warning">
      ⚠️ <strong>警告:</strong> 更新中は絶対に電源を切らないでください！
    </div>
    <form id="uploadForm" enctype="multipart/form-data">
      <div class="form-group">
        <label for="firmware">ファームウェアファイル (.bin):</label>
        <input type="file" id="firmware" name="firmware" accept=".bin" required>
      </div>
      <button type="submit">アップロード＆更新</button>
    </form>
    <div class="progress" id="progress">
      <div class="progress-bar"><div class="progress-fill" id="progressFill"></div></div>
      <div class="progress-text" id="progressText">アップロード中: 0%</div>
    </div>
  </div>
  <script>
    document.getElementById('uploadForm').addEventListener('submit', async (e) => {
      e.preventDefault();
      const fileInput = document.getElementById('firmware');
      const file = fileInput.files[0];
      if (!file) return;

      const progress = document.getElementById('progress');
      const progressFill = document.getElementById('progressFill');
      const progressText = document.getElementById('progressText');

      progress.style.display = 'block';
      document.querySelector('button').disabled = true;

      const xhr = new XMLHttpRequest();
      xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
          const percent = (e.loaded / e.total) * 100;
          progressFill.style.width = percent + '%';
          progressText.textContent = 'アップロード中: ' + Math.round(percent) + '%';
        }
      });

      xhr.addEventListener('load', () => {
        if (xhr.status === 200) {
          progressText.textContent = '✅ 更新成功！再起動しています...';
          progressText.className = 'progress-text success';
          progressFill.style.width = '100%';
          setTimeout(() => {
            progressText.textContent = '✅ 更新完了！デバイスが再起動しました。5秒後にトップページに戻ります...';
          }, 1000);
          setTimeout(() => {
            window.location.href = '/';
          }, 5000);
        } else {
          progressText.textContent = '❌ 更新失敗: ' + xhr.responseText;
          progressText.className = 'progress-text error';
          document.querySelector('button').disabled = false;
        }
      });

      xhr.addEventListener('error', () => {
        progressText.textContent = '❌ アップロードエラー';
        progressText.className = 'progress-text error';
        document.querySelector('button').disabled = false;
      });

      xhr.open('POST', '/doUpdate');
      const formData = new FormData();
      formData.append('firmware', file);
      xhr.send(formData);
    });
  </script>
</body>
</html>
    )";
    server.send(200, "text/html", html);
  });

  // CORS Preflight対応: /doUpdate
  server.on("/doUpdate", HTTP_OPTIONS, [](){
    sendCORSHeaders();
    server.send(204);
  });

  // ルート: OTA更新実行（POST）
  server.on("/doUpdate", HTTP_POST, [](){
    sendCORSHeaders();
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    // HTTPレスポンスが完全に送信されるまで待機してからリスタート
    // これがないとクライアントがレスポンスを受信する前に接続が切れる可能性がある
    delay(500);
    ESP.restart();
  }, [](){
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.println("OTA Update Start");
      Serial.printf("Filename: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("Update Success: %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  // ============================================
  // サーボトリムAPIエンドポイント（v1.6.0追加）
  // ============================================

  // CORS Preflight: /trim
  server.on("/trim", HTTP_OPTIONS, [](){
    sendCORSHeaders();
    server.send(204);
  });

  // GET /trim - トリム値取得
  server.on("/trim", HTTP_GET, [](){
    sendCORSHeaders();

    String json = "{\"count\":" + String(servoTrimCount) + ",\"trims\":[";
    for (int i = 0; i < servoTrimCount; i++) {
      if (i > 0) json += ",";
      json += String(servoTrims[i]);
    }
    json += "]}";

    server.send(200, "application/json", json);
  });

  // POST /trim - トリム値設定（一時的）
  // Body: {"index":0,"value":5} または {"trims":[0,5,-3,0]}
  server.on("/trim", HTTP_POST, [](){
    sendCORSHeaders();

    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No body\"}");
      return;
    }

    String body = server.arg("plain");
    Serial.println("[TRIM] POST /trim: " + body);

    // 配列形式: {"trims":[0,5,-3,0]}
    int trimsArr[MAX_SERVO_TRIM];
    int arrCount = parseJsonArray(body, "trims", trimsArr, MAX_SERVO_TRIM);

    if (arrCount > 0) {
      servoTrimCount = arrCount;
      for (int i = 0; i < arrCount; i++) {
        servoTrims[i] = constrain(trimsArr[i], -30, 30);
      }
      Serial.printf("[TRIM] Set %d trims\n", arrCount);
      server.send(200, "application/json", "{\"success\":true}");
      return;
    }

    // 単一形式: {"index":0,"value":5}
    int index = parseJsonInt(body, "index");
    int value = parseJsonInt(body, "value");

    if (index != -9999 && value != -9999) {
      setServoTrim(index, value);
      // サーボ数を自動更新
      if (index >= servoTrimCount) {
        servoTrimCount = index + 1;
      }
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON format\"}");
    }
  });

  // CORS Preflight: /trim/save
  server.on("/trim/save", HTTP_OPTIONS, [](){
    sendCORSHeaders();
    server.send(204);
  });

  // POST /trim/save - NVSに永続保存
  server.on("/trim/save", HTTP_POST, [](){
    sendCORSHeaders();
    saveServoTrims();
    server.send(200, "application/json", "{\"success\":true}");
  });

  // CORS Preflight: /trim/test
  server.on("/trim/test", HTTP_OPTIONS, [](){
    sendCORSHeaders();
    server.send(204);
  });

  // POST /trim/test - テスト動作
  // Body: {"action":"home"} or {"action":"sweep","index":0}
  // 注: 実際のサーボ制御はユーザーコード側で実装
  //     ここではフラグを立てて、ユーザーコードから読み取る
  server.on("/trim/test", HTTP_POST, [](){
    sendCORSHeaders();

    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No body\"}");
      return;
    }

    String body = server.arg("plain");
    Serial.println("[TRIM] POST /trim/test: " + body);

    // action の検出（簡易実装）
    String action = "unknown";
    if (body.indexOf("\"home\"") >= 0) {
      action = "home";
    } else if (body.indexOf("\"sweep\"") >= 0) {
      action = "sweep";
    } else if (body.indexOf("\"walk\"") >= 0) {
      action = "walk";
    }

    int index = parseJsonInt(body, "index");

    Serial.printf("[TRIM] Test action: %s, index: %d\n", action.c_str(), index);

    // テスト動作の実際の実装は、サーボが接続されている
    // ユーザーコード側で行う必要がある
    // ファームウェアレベルではログ出力のみ
    server.send(200, "application/json", "{\"success\":true,\"action\":\"" + action + "\"}");
  });

  // CORS Preflight: /trim/config
  server.on("/trim/config", HTTP_OPTIONS, [](){
    sendCORSHeaders();
    server.send(204);
  });

  // POST /trim/config - サーボ構成設定
  // Body: {"count":4}
  server.on("/trim/config", HTTP_POST, [](){
    sendCORSHeaders();

    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No body\"}");
      return;
    }

    String body = server.arg("plain");
    Serial.println("[TRIM] POST /trim/config: " + body);

    int count = parseJsonInt(body, "count");
    if (count != -9999 && count >= 0 && count <= MAX_SERVO_TRIM) {
      setServoTrimCount(count);
      // 新しいサーボのトリム値を0に初期化
      for (int i = servoTrimCount; i < count; i++) {
        servoTrims[i] = 0;
      }
      server.send(200, "application/json", "{\"success\":true,\"count\":" + String(servoTrimCount) + "}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid count\"}");
    }
  });

  server.begin();
  if (isAPMode) {
    Serial.println("HTTP server started on http://192.168.4.1 (AP Mode)");
  } else {
    Serial.println("HTTP server started on http://" + WiFi.localIP().toString() + " (Station Mode)");
  }
}

// ============================================
// ユーザーコード（Blocklyから生成される部分）
// ============================================

void userSetup() {
  // Blocklyから生成されたセットアップコードがここに挿入される
  // 例: モーター初期化、センサー初期化など

  Serial.println("User setup completed.");
}

void userLoop() {
  // Blocklyから生成されたループコードがここに挿入される
  // 例: センサー読み取り、モーター制御など

  // デフォルト動作：LED点滅（動作確認用）
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
  }
}
