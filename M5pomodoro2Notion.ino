#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>
#include <esp_system.h>
#include <time.h>
#include <math.h>

// 設定ファイルを読み込む（存在する場合）
#ifdef __has_include
  #if __has_include("secrets.h")
    #include "secrets.h"
    #define HAS_SECRETS_FILE
  #endif
#endif

namespace {

constexpr unsigned long WORK_DURATION_MS = 25UL * 60UL * 1000UL;
constexpr unsigned long BREAK_DURATION_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long UI_REFRESH_INTERVAL_MS = 200UL;
constexpr unsigned long BUTTON_LONG_PRESS_MS = 200UL;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 15000UL;
constexpr unsigned long QUEUE_RETRY_INTERVAL_MS = 10000UL;
constexpr unsigned long STATUS_MESSAGE_DURATION_MS = 4000UL;
constexpr size_t MAX_QUEUE_SIZE = 10;  // キューの最大保存数

const char *NOTION_API_URL = "https://api.notion.com/v1/pages";
const char *NOTION_VERSION = "2022-06-28";
const char *PRIMARY_NTP = "ntp.nict.jp";
const char *SECONDARY_NTP = "time.google.com";
const char *TERTIARY_NTP = "pool.ntp.org";

enum class TimerState {
  Waiting,
  Work,
  Break,
  Paused
};

enum class TimerPhase {
  Work,
  Break
};

struct WifiCredential {
  String ssid;
  String password;
};

struct Credentials {
  std::vector<WifiCredential> wifiNetworks;
  String notionToken;
  String notionDatabaseId;

  bool isValid() const {
    return !wifiNetworks.empty() && notionToken.length() > 0 && notionDatabaseId.length() > 0;
  }
};

struct NotionPendingItem {
  time_t sessionStart = 0;
  time_t logTime = 0;
  float roundedHours = 0.0f;
  String isoDate;
  String displayDate;
  String notes;
  String weekdayId;
};

Credentials g_credentials;
std::vector<NotionPendingItem> g_pendingQueue;
Preferences g_prefs;  // 不揮発性メモリへのアクセス用

TimerState g_timerState = TimerState::Waiting;
TimerPhase g_currentPhase = TimerPhase::Work;
TimerPhase g_nextPhase = TimerPhase::Work;

unsigned long g_stateStartMillis = 0;
unsigned long g_pauseStartMillis = 0;
unsigned long g_totalAccumulatedMillis = 0;
unsigned long g_lastUiRefresh = 0;
unsigned long g_lastQueueAttempt = 0;
unsigned long g_lastWifiAttempt = 0;
size_t g_currentWifiIndex = 0;  // 現在試行中のWiFi設定のインデックス

time_t g_sessionStartEpoch = 0;
bool g_sessionActive = false;

bool g_timeSynced = false;
bool g_queueDirty = false;

String g_sessionId;
String g_statusMessage;
unsigned long g_statusMessageExpireAt = 0;
String g_connectedSsid;  // 接続中のWiFi SSID
bool g_wifiConnecting = false;  // WiFi接続試行中フラグ

// ボタン長押し検知用
unsigned long g_buttonAPressStart = 0;
unsigned long g_buttonCPressStart = 0;
bool g_buttonAHandled = false;
bool g_buttonCHandled = false;

TFT_eSprite g_canvas = TFT_eSprite(&M5.Lcd);

TimerState g_lastUiState = TimerState::Waiting;
TimerPhase g_lastUiPhase = TimerPhase::Work;
unsigned long g_lastUiTotalSeconds = 0;
unsigned long g_lastUiTimeLeftSeconds = 0;
bool g_lastUiPaused = false;
size_t g_lastUiQueueSize = 0;
bool g_lastUiWifiStatus = false;

// Forward declarations
bool connectWiFiAndWait(bool force = false);
void connectWiFi(bool force = false);
void syncTime();
void generateSessionId();
void ensureQueueLoaded();
void saveQueue();
bool postToNotion(const NotionPendingItem &item);
void setStatusMessage(const String &message, unsigned long duration = STATUS_MESSAGE_DURATION_MS);
void startPhase(TimerPhase phase);
void enterWaitingState(TimerPhase upcomingPhase, bool fromCompletion);
unsigned long currentPhaseDuration();
unsigned long currentPhaseElapsed();
unsigned long computeTotalElapsedMillis();
void updateTimer();
void updateUi(bool force = false);
void drawWaitingScreen(unsigned long totalMillis);
void drawActiveScreen(bool paused, unsigned long timeLeftMillis, unsigned long totalMillis);
void drawStatusFooter();
String phaseLabel(TimerPhase phase);
String stateLabel(TimerState state);
String formatDuration(unsigned long millisValue);
String formatTime(time_t epoch, const char *format);
String isoDateFromTime(const tm &timeinfo);
String displayDateFromTime(const tm &timeinfo);
String weekdayIdFromTime(const tm &timeinfo);
float toRoundedQuarterHours(unsigned long millisValue);
void buzzStart();
void buzzComplete();
void buzzShortBeep();
void resetSession(bool regenerateSessionId);
void handleStartPauseButton();
void handleSendButton();
void attemptImmediateQueueFlush();
void loadCredentialsFromSecrets();

template <typename T>
T clampValue(T value, T minValue, T maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

}  // namespace

void setup() {
  M5.begin();
  M5.Power.begin();
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(TFT_BLACK);
  
  g_canvas.setColorDepth(8);
  if (!g_canvas.createSprite(M5.Lcd.width(), M5.Lcd.height())) {
    M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.print("Sprite Error!");
    while(1) delay(1000);
  }

  // secrets.hファイルから設定を読み込む
  #ifdef HAS_SECRETS_FILE
    loadCredentialsFromSecrets();
  #else
    M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.print("secrets.h not found");
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.print("WiFi unavailable");
    while(1) delay(1000);  // WiFi接続できないため停止
  #endif

  // WiFi接続をバックグラウンドで開始（接続待機しない）
  g_wifiConnecting = true;
  connectWiFi(true);

  // 不揮発性メモリからキューを復元
  ensureQueueLoaded();

  generateSessionId();
  resetSession(false);

  // 時刻同期はWiFi接続後にバックグラウンドで実行される
  updateUi(true);
}

void loop() {
  M5.update();

  // Aボタン長押し検知
  if (M5.BtnA.isPressed()) {
    if (g_buttonAPressStart == 0) {
      // 押下開始
      g_buttonAPressStart = millis();
      g_buttonAHandled = false;
    } else if (!g_buttonAHandled && (millis() - g_buttonAPressStart >= BUTTON_LONG_PRESS_MS)) {
      // 200ms経過、まだ処理していない
      buzzShortBeep();
      handleStartPauseButton();
      g_buttonAHandled = true;
      // ボタンが離されるまで待機（重複実行を防ぐ）
      while (M5.BtnA.isPressed()) {
        M5.update();
        delay(10);
      }
      // ボタンが離されたのでリセット
      g_buttonAPressStart = 0;
      g_buttonAHandled = false;
    }
  } else {
    // ボタンが離された
    g_buttonAPressStart = 0;
    g_buttonAHandled = false;
  }
  
  // Cボタン長押し検知
  if (M5.BtnC.isPressed()) {
    if (g_buttonCPressStart == 0) {
      // 押下開始
      g_buttonCPressStart = millis();
      g_buttonCHandled = false;
    } else if (!g_buttonCHandled && (millis() - g_buttonCPressStart >= BUTTON_LONG_PRESS_MS)) {
      // 200ms経過、まだ処理していない
      buzzShortBeep();
      handleSendButton();
      g_buttonCHandled = true;
      // ボタンが離されるまで待機（重複実行を防ぐ）
      while (M5.BtnC.isPressed()) {
        M5.update();
        delay(10);
      }
      // ボタンが離されたのでリセット
      g_buttonCPressStart = 0;
      g_buttonCHandled = false;
    }
  } else {
    // ボタンが離された
    g_buttonCPressStart = 0;
    g_buttonCHandled = false;
  }

  updateTimer();
  attemptImmediateQueueFlush();

  // WiFi接続をバックグラウンドで継続的に試行
  unsigned long now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    // 接続成功時
    if (g_wifiConnecting || g_connectedSsid.length() == 0) {
      g_wifiConnecting = false;
      g_connectedSsid = WiFi.SSID();
      g_currentWifiIndex = 0;  // 接続成功したらインデックスをリセット
      if (!g_timeSynced) {
        syncTime();
      }
      // WiFi接続成功時にキューを処理
      attemptImmediateQueueFlush();
      updateUi(true);
    }
  } else {
    // 未接続時
    if (!g_wifiConnecting) {
      g_connectedSsid = "";  // 接続試行中でない場合のみSSIDをクリア
      // 接続試行中でない場合、次の試行タイミングをチェック
      if (now - g_lastWifiAttempt > WIFI_RETRY_INTERVAL_MS) {
        // 次のWiFi設定を試行
        g_wifiConnecting = true;
        connectWiFi(true);
        g_lastWifiAttempt = now;
      }
    } else {
      // 接続試行中：タイムアウトチェック
      if (now - g_lastWifiAttempt > WIFI_CONNECT_TIMEOUT_MS) {
        // タイムアウト：次のWiFi設定を試行
        g_wifiConnecting = false;
        g_currentWifiIndex++;
        if (g_currentWifiIndex >= g_credentials.wifiNetworks.size()) {
          g_currentWifiIndex = 0;  // すべて試行したら最初に戻る
        }
        g_lastWifiAttempt = now;
      }
    }
  }

  delay(20);
}

namespace {

bool connectWiFiAndWait(bool force) {
  if (!g_credentials.isValid()) {
    return false;
  }
  if (WiFi.status() == WL_CONNECTED && !force) {
    return true;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

  for (const auto &cred : g_credentials.wifiNetworks) {
    M5.Lcd.setCursor(10, 70);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setTextSize(1);
    M5.Lcd.printf("Trying: %s", cred.ssid.c_str());
    M5.Lcd.fillRect(0, 90, 320, 20, TFT_BLACK);
    
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
    unsigned long start = millis();
    int dotCount = 0;
    
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
      delay(200);
      // 接続中の表示を更新
      if ((millis() - start) % 1000 < 200) {
        M5.Lcd.setCursor(10, 90);
        M5.Lcd.print(".");
        dotCount++;
        if (dotCount > 3) {
          M5.Lcd.fillRect(10, 90, 20, 20, TFT_BLACK);
          dotCount = 0;
        }
      }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      M5.Lcd.fillRect(0, 70, 320, 40, TFT_BLACK);
      M5.Lcd.setCursor(10, 70);
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
      M5.Lcd.printf("Connected: %s", WiFi.localIP().toString().c_str());
      return true;
    }
  }

  M5.Lcd.fillRect(0, 70, 320, 40, TFT_BLACK);
  return false;
}

void connectWiFi(bool force) {
  if (!g_credentials.isValid()) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED && !force) {
    return;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

  // バックグラウンド接続：現在のインデックスのWiFi設定を試行開始
  // 実際の接続確認はloop()で行う
  if (!g_credentials.wifiNetworks.empty() && g_currentWifiIndex < g_credentials.wifiNetworks.size()) {
    const auto &cred = g_credentials.wifiNetworks[g_currentWifiIndex];
    g_connectedSsid = cred.ssid;  // 接続試行中のSSID名を保存
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
    g_lastWifiAttempt = millis();
  }
}

void loadCredentialsFromSecrets() {
  #ifdef HAS_SECRETS_FILE
    g_credentials.wifiNetworks.clear();
    
    // WiFi設定を自動検出して追加（最大10個まで対応）
    // WIFI_SSID_1, WIFI_SSID_2, ... の形式で定義されているものを自動検出
    // secrets.hに新しいWiFi設定を追加するだけで自動的に検出されます
    
    // WiFi設定1
    #ifdef WIFI_SSID_1
    {
      String ssid = String(WIFI_SSID_1);
      String password = String(WIFI_PASSWORD_1);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_1") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // WiFi設定2
    #ifdef WIFI_SSID_2
    {
      String ssid = String(WIFI_SSID_2);
      String password = String(WIFI_PASSWORD_2);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_2") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // WiFi設定3
    #ifdef WIFI_SSID_3
    {
      String ssid = String(WIFI_SSID_3);
      String password = String(WIFI_PASSWORD_3);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_3") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // WiFi設定4
    #ifdef WIFI_SSID_4
    {
      String ssid = String(WIFI_SSID_4);
      String password = String(WIFI_PASSWORD_4);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_4") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // WiFi設定5
    #ifdef WIFI_SSID_5
    {
      String ssid = String(WIFI_SSID_5);
      String password = String(WIFI_PASSWORD_5);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_5") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // WiFi設定6
    #ifdef WIFI_SSID_6
    {
      String ssid = String(WIFI_SSID_6);
      String password = String(WIFI_PASSWORD_6);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_6") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // WiFi設定7
    #ifdef WIFI_SSID_7
    {
      String ssid = String(WIFI_SSID_7);
      String password = String(WIFI_PASSWORD_7);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_7") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // WiFi設定8
    #ifdef WIFI_SSID_8
    {
      String ssid = String(WIFI_SSID_8);
      String password = String(WIFI_PASSWORD_8);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_8") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // WiFi設定9
    #ifdef WIFI_SSID_9
    {
      String ssid = String(WIFI_SSID_9);
      String password = String(WIFI_PASSWORD_9);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_9") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // WiFi設定10
    #ifdef WIFI_SSID_10
    {
      String ssid = String(WIFI_SSID_10);
      String password = String(WIFI_PASSWORD_10);
      if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID_10") {
        WifiCredential cred;
        cred.ssid = ssid;
        cred.password = password;
        g_credentials.wifiNetworks.push_back(cred);
      }
    }
    #endif
    
    // Notion設定を追加
    g_credentials.notionToken = String(NOTION_TOKEN);
    g_credentials.notionDatabaseId = String(NOTION_DATABASE_ID);
    
    if (g_credentials.isValid()) {
      setStatusMessage("Loaded from secrets.h", 3000);
    } else {
      setStatusMessage("Invalid secrets.h", 3000);
    }
  #endif
}

void syncTime() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  setenv("TZ", "JST-9", 1);
  tzset();

  configTime(9 * 3600, 0, PRIMARY_NTP, SECONDARY_NTP, TERTIARY_NTP);

  const time_t compileTime = 1609459200;  // 2021-01-01
  for (int i = 0; i < 10; ++i) {
    time_t now = time(nullptr);
    if (now > compileTime) {
      g_timeSynced = true;
      Serial.println("[TIME] Sync successful");
      return;
    }
    delay(500);
  }

  Serial.println("[TIME] Sync failed");
}

void generateSessionId() {
  uint64_t chipId = ESP.getEfuseMac();
  uint32_t randomPart = esp_random();

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04X%04X-%08X",
           static_cast<uint16_t>(chipId >> 32),
           static_cast<uint16_t>(chipId & 0xFFFF),
           randomPart);
  g_sessionId = String(buffer);
}

// Queue機能：不揮発性メモリ（Preferences/NVS）に保存
// キューは再起動後も保持され、未送信データが失われません
void ensureQueueLoaded() {
  g_pendingQueue.clear();
  
  if (!g_prefs.begin("pomodoro", true)) {  // 読み取り専用モード
    Serial.println("[QUEUE] Failed to open preferences");
    return;
  }
  
  String queueJson = g_prefs.getString("queue", "");
  g_prefs.end();
  
  if (queueJson.length() == 0) {
    Serial.println("[QUEUE] No saved queue found");
    return;
  }
  
  // JSONをパースしてキューに復元
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, queueJson);
  
  if (error) {
    Serial.printf("[QUEUE] Failed to parse JSON: %s\n", error.c_str());
    return;
  }
  
  JsonArray items = doc["items"].as<JsonArray>();
  for (JsonObject itemObj : items) {
    NotionPendingItem item;
    item.sessionStart = itemObj["sessionStart"].as<time_t>();
    item.logTime = itemObj["logTime"].as<time_t>();
    item.roundedHours = itemObj["roundedHours"].as<float>();
    item.isoDate = itemObj["isoDate"].as<String>();
    item.displayDate = itemObj["displayDate"].as<String>();
    item.notes = itemObj["notes"].as<String>();
    item.weekdayId = itemObj["weekdayId"].as<String>();
    g_pendingQueue.push_back(item);
  }
  
  Serial.printf("[QUEUE] Loaded %d items from NVS\n", g_pendingQueue.size());
}

void saveQueue() {
  if (!g_prefs.begin("pomodoro", false)) {  // 読み書きモード
    Serial.println("[QUEUE] Failed to open preferences for writing");
    return;
  }
  
  if (g_pendingQueue.empty()) {
    // キューが空の場合はNVSからも削除
    g_prefs.remove("queue");
    Serial.println("[QUEUE] Cleared queue from NVS");
    g_prefs.end();
    return;
  }
  
  // キューをJSONにシリアライズ
  DynamicJsonDocument doc(4096);
  JsonArray items = doc.createNestedArray("items");
  
  for (const auto &item : g_pendingQueue) {
    JsonObject itemObj = items.createNestedObject();
    itemObj["sessionStart"] = item.sessionStart;
    itemObj["logTime"] = item.logTime;
    itemObj["roundedHours"] = item.roundedHours;
    itemObj["isoDate"] = item.isoDate;
    itemObj["displayDate"] = item.displayDate;
    itemObj["notes"] = item.notes;
    itemObj["weekdayId"] = item.weekdayId;
  }
  
  String queueJson;
  serializeJson(doc, queueJson);
  
  if (g_prefs.putString("queue", queueJson)) {
    Serial.printf("[QUEUE] Saved %d items to NVS\n", g_pendingQueue.size());
  } else {
    Serial.println("[QUEUE] Failed to save to NVS");
  }
  
  g_prefs.end();
}

void setStatusMessage(const String &message, unsigned long duration) {
  g_statusMessage = message;
  g_statusMessageExpireAt = millis() + duration;
  updateUi(true);
}

void startPhase(TimerPhase phase) {
  if (!g_sessionActive) {
    g_sessionStartEpoch = time(nullptr);
    if (g_sessionStartEpoch == 0) {
      // attempt to sync again if time not available
      syncTime();
      g_sessionStartEpoch = time(nullptr);
    }
    g_sessionActive = true;
  }

  g_currentPhase = phase;
  g_timerState = (phase == TimerPhase::Work) ? TimerState::Work : TimerState::Break;
  g_stateStartMillis = millis();
  g_pauseStartMillis = 0;

  buzzStart();
  updateUi(true);
}

void enterWaitingState(TimerPhase upcomingPhase, bool fromCompletion) {
  g_timerState = TimerState::Waiting;
  g_nextPhase = upcomingPhase;
  g_stateStartMillis = 0;
  g_pauseStartMillis = 0;
  g_currentPhase = upcomingPhase;
  if (fromCompletion) {
    buzzComplete();
  }
  updateUi(true);
}

unsigned long currentPhaseDuration() {
  return (g_currentPhase == TimerPhase::Work) ? WORK_DURATION_MS : BREAK_DURATION_MS;
}

unsigned long currentPhaseElapsed() {
  if (g_timerState == TimerState::Work || g_timerState == TimerState::Break) {
    return millis() - g_stateStartMillis;
  }
  if (g_timerState == TimerState::Paused) {
    return g_pauseStartMillis - g_stateStartMillis;
  }
  return 0;
}

unsigned long computeTotalElapsedMillis() {
  unsigned long total = g_totalAccumulatedMillis;
  if (g_timerState == TimerState::Work || g_timerState == TimerState::Break) {
    unsigned long elapsed = millis() - g_stateStartMillis;
    unsigned long phaseDuration = currentPhaseDuration();
    total += (elapsed < phaseDuration) ? elapsed : phaseDuration;
  } else if (g_timerState == TimerState::Paused) {
    total += g_pauseStartMillis - g_stateStartMillis;
  }
  return total;
}

void updateTimer() {
  if (g_timerState == TimerState::Work || g_timerState == TimerState::Break) {
    unsigned long duration = currentPhaseDuration();
    unsigned long elapsed = millis() - g_stateStartMillis;
    if (elapsed >= duration) {
      g_totalAccumulatedMillis += duration;
      TimerPhase upcoming = (g_currentPhase == TimerPhase::Work) ? TimerPhase::Break : TimerPhase::Work;
      enterWaitingState(upcoming, true);
    }
  }

  unsigned long now = millis();
  if (now - g_lastUiRefresh >= UI_REFRESH_INTERVAL_MS) {
    updateUi();
    g_lastUiRefresh = now;
  }
}

void updateUi(bool force) {
  unsigned long totalMillis = computeTotalElapsedMillis();
  unsigned long totalSeconds = totalMillis / 1000;

  bool isPaused = (g_timerState == TimerState::Paused);
  unsigned long timeLeftMillis = 0;
  if (g_timerState == TimerState::Work || g_timerState == TimerState::Break) {
    unsigned long elapsed = millis() - g_stateStartMillis;
    unsigned long duration = currentPhaseDuration();
    timeLeftMillis = (elapsed >= duration) ? 0 : (duration - elapsed);
  } else if (g_timerState == TimerState::Paused) {
    unsigned long elapsed = g_pauseStartMillis - g_stateStartMillis;
    unsigned long duration = currentPhaseDuration();
    timeLeftMillis = (elapsed >= duration) ? 0 : (duration - elapsed);
  }
  unsigned long timeLeftSeconds = timeLeftMillis / 1000;

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  bool needsRefresh = force ||
                      g_timerState != g_lastUiState ||
                      g_currentPhase != g_lastUiPhase ||
                      totalSeconds != g_lastUiTotalSeconds ||
                      timeLeftSeconds != g_lastUiTimeLeftSeconds ||
                      isPaused != g_lastUiPaused ||
                      g_pendingQueue.size() != g_lastUiQueueSize ||
                      wifiConnected != g_lastUiWifiStatus ||
                      (!g_statusMessage.isEmpty() && millis() < g_statusMessageExpireAt);

  if (!needsRefresh) {
    return;
  }

  g_canvas.fillSprite(TFT_BLACK);

  if (g_timerState == TimerState::Waiting) {
    drawWaitingScreen(totalMillis);
  } else {
    drawActiveScreen(isPaused, timeLeftMillis, totalMillis);
  }

  g_canvas.pushSprite(0, 0);

  g_lastUiState = g_timerState;
  g_lastUiPhase = g_currentPhase;
  g_lastUiTotalSeconds = totalSeconds;
  g_lastUiTimeLeftSeconds = timeLeftSeconds;
  g_lastUiPaused = isPaused;
  g_lastUiQueueSize = g_pendingQueue.size();
  g_lastUiWifiStatus = wifiConnected;
}

void drawWaitingScreen(unsigned long totalMillis) {
  uint16_t textColor = TFT_WHITE;
  
  // バーの設定（アクティブ画面と同じ）
  const int barY = 20;
  const int barHeight = 80;
  const int canvasWidth = g_canvas.width();
  
  // 待機画面用のバー（グレーで表示、横幅100%）
  uint16_t barColor = TFT_DARKGREY;
  g_canvas.fillRect(0, barY, canvasWidth, barHeight, barColor);
  
  // バーの中に重ねて表示する情報
  // 左寄せ：Stateと次のフェーズ
  g_canvas.setTextColor(textColor, barColor);
  g_canvas.setTextSize(2);
  String statePhaseText = "Waiting / Next: " + phaseLabel(g_nextPhase);
  int textY = barY + 15;
  g_canvas.setCursor(10, textY);
  g_canvas.print(statePhaseText);
  
  // 右寄せ：待機中なので残り時間は表示しない（または「--:--」を表示）
  // アクティブ画面と同じ位置に配置するが、待機中は表示しない

  // バーの下に表示：トータル経過時間と丸め込み値（アクティブ画面と同じ位置）
  g_canvas.setTextColor(textColor, TFT_BLACK);
  g_canvas.setTextSize(2);
  int infoY = barY + barHeight + 10;
  g_canvas.setCursor(10, infoY);
  g_canvas.print("Total: ");
  g_canvas.print(formatDuration(totalMillis));

  float roundedHours = toRoundedQuarterHours(totalMillis);
  g_canvas.setCursor(10, infoY + 25);
  g_canvas.printf("Rounded: %.2fh", roundedHours);

  // その下に小さく表示：ボタンアサイン、WiFi、キュー、POST状態など（アクティブ画面と同じ位置）
  g_canvas.setTextSize(1);
  int detailY = infoY + 55;
  
  // ボタンアサイン
  g_canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g_canvas.setCursor(10, detailY);
  g_canvas.print("A: Start");
  g_canvas.setCursor(10, detailY + 12);
  g_canvas.print("C: Send & Reset");

  // WiFi接続状態
  String wifiStatus;
  uint16_t wifiColor;
  if (WiFi.status() == WL_CONNECTED) {
    if (g_connectedSsid.length() > 0) {
      wifiStatus = "Wi-Fi: " + g_connectedSsid;
    } else {
      wifiStatus = "Wi-Fi: Connected";
    }
    wifiColor = TFT_GREEN;
  } else if (g_wifiConnecting) {
    if (g_connectedSsid.length() > 0) {
      wifiStatus = "Wi-Fi: Connecting to " + g_connectedSsid;
    } else {
      wifiStatus = "Wi-Fi: Connecting...";
    }
    wifiColor = TFT_YELLOW;
  } else {
    wifiStatus = "Wi-Fi: Offline";
    wifiColor = TFT_LIGHTGREY;
  }
  g_canvas.setTextColor(wifiColor, TFT_BLACK);
  g_canvas.setCursor(10, detailY + 24);
  g_canvas.print(wifiStatus);

  // キュー状態
  size_t queueSize = g_pendingQueue.size();
  uint16_t queueColor;
  if (queueSize == 0) {
    queueColor = TFT_LIGHTGREY;  // 空：グレー
  } else if (queueSize <= 3) {
    queueColor = TFT_GREEN;  // 少ない：緑色
  } else if (queueSize <= 7) {
    queueColor = TFT_YELLOW;  // 中程度：黄色
  } else {
    queueColor = TFT_RED;  // 多い：赤色
  }
  g_canvas.setTextColor(queueColor, TFT_BLACK);
  g_canvas.setCursor(10, detailY + 36);
  g_canvas.printf("Queue: %d / %d",
                  static_cast<int>(queueSize),
                  static_cast<int>(MAX_QUEUE_SIZE));

  // 電池残量
  int batteryLevel = M5.Power.getBatteryLevel();
  uint16_t batteryColor;
  if (batteryLevel > 50) {
    batteryColor = TFT_GREEN;
  } else if (batteryLevel > 20) {
    batteryColor = TFT_YELLOW;
  } else {
    batteryColor = TFT_RED;
  }
  g_canvas.setTextColor(batteryColor, TFT_BLACK);
  g_canvas.setCursor(10, detailY + 48);
  g_canvas.printf("Battery: %d%%", batteryLevel);

  // ステータスメッセージ（POST状態など）
  if (!g_statusMessage.isEmpty() && millis() < g_statusMessageExpireAt) {
    g_canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
    g_canvas.setCursor(10, detailY + 60);
    g_canvas.print(g_statusMessage);
    g_canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  }
}

void drawActiveScreen(bool paused, unsigned long timeLeftMillis, unsigned long totalMillis) {
  uint16_t barColor = (g_currentPhase == TimerPhase::Work) ? 0xf920 : 0x051f;
  uint16_t textColor = TFT_WHITE;

  // バーの設定
  const int barY = 20;  // バーのY位置（上に余白を確保）
  const int barHeight = 80;  // バーの高さ（広げる）
  const int canvasWidth = g_canvas.width();

  unsigned long duration = currentPhaseDuration();
  unsigned long elapsed = currentPhaseElapsed();
  float progress = clampValue(static_cast<float>(elapsed) / static_cast<float>(duration), 0.0f, 1.0f);
  // 残り時間に応じて幅が小さくなる（100%から始まって0%に近づく）
  float remainingProgress = 1.0f - progress;
  int barWidth = static_cast<int>(remainingProgress * canvasWidth);

  // バーの背景（黒）を描画
  g_canvas.fillRect(0, barY, canvasWidth, barHeight, TFT_BLACK);
  // 残り時間に応じたバーを描画
  g_canvas.fillRect(0, barY, barWidth, barHeight, barColor);

  // バーの中に重ねて表示する情報
  // 左寄せ：Stateとフェーズ（バーの中に表示）
  // バーの色の上に白文字で表示（背景色は透明ではなく、バーの色を指定）
  g_canvas.setTextColor(textColor, barColor);
  g_canvas.setTextSize(2);
  String stateText;
  if (paused) {
    stateText = "Paused";
    g_canvas.setTextColor(TFT_YELLOW, barColor);
  } else {
    stateText = "Running";
    g_canvas.setTextColor(textColor, barColor);
  }
  String statePhaseText = stateText + " / " + phaseLabel(g_currentPhase);
  // バーの中央より上に配置
  int textY = barY + 15;
  g_canvas.setCursor(10, textY);
  g_canvas.print(statePhaseText);

  // 右寄せ：残り時間（大きく表示、バーの中に表示）
  // バーの幅が小さくなっても見えるように、常に画面右端に表示
  g_canvas.setTextSize(4);
  String timeLeftStr = formatDuration(timeLeftMillis);
  int textWidth = g_canvas.textWidth(timeLeftStr);
  // バーの中央より下に配置
  int timeY = barY + 45;
  int timeX = canvasWidth - textWidth - 10;
  
  // 残り時間のテキスト全体がバーの範囲内にあるかどうかを判定
  // テキストの開始位置と終了位置（timeX + textWidth）を考慮
  if (timeX + textWidth <= barWidth) {
    // テキスト全体がバーの範囲内
    g_canvas.setTextColor(textColor, barColor);
  } else {
    // テキストがバーの範囲外（または一部が範囲外）
    g_canvas.setTextColor(textColor, TFT_BLACK);
  }
  g_canvas.setCursor(timeX, timeY);
  g_canvas.print(timeLeftStr);

  // バーの下に表示：トータル経過時間と丸め込み値
  g_canvas.setTextColor(textColor, TFT_BLACK);
  g_canvas.setTextSize(2);
  int infoY = barY + barHeight + 10;
  g_canvas.setCursor(10, infoY);
  g_canvas.print("Total: ");
  g_canvas.print(formatDuration(totalMillis));

  float roundedHours = toRoundedQuarterHours(totalMillis);
  g_canvas.setCursor(10, infoY + 25);
  g_canvas.printf("Rounded: %.2fh", roundedHours);

  // その下に小さく表示：ボタンアサイン、WiFi、キュー、POST状態など
  g_canvas.setTextSize(1);
  int detailY = infoY + 55;
  
  // ボタンアサイン
  g_canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g_canvas.setCursor(10, detailY);
  if (paused) {
    g_canvas.print("A: Resume");
  } else {
    g_canvas.print("A: Pause");
  }
  g_canvas.setCursor(10, detailY + 12);
  g_canvas.print("C: Send & Reset");

  // WiFi接続状態
  String wifiStatus;
  uint16_t wifiColor;
  if (WiFi.status() == WL_CONNECTED) {
    if (g_connectedSsid.length() > 0) {
      wifiStatus = "Wi-Fi: " + g_connectedSsid;
    } else {
      wifiStatus = "Wi-Fi: Connected";
    }
    wifiColor = TFT_GREEN;
  } else if (g_wifiConnecting) {
    if (g_connectedSsid.length() > 0) {
      wifiStatus = "Wi-Fi: Connecting to " + g_connectedSsid;
    } else {
      wifiStatus = "Wi-Fi: Connecting...";
    }
    wifiColor = TFT_YELLOW;
  } else {
    wifiStatus = "Wi-Fi: Offline";
    wifiColor = TFT_LIGHTGREY;
  }
  g_canvas.setTextColor(wifiColor, TFT_BLACK);
  g_canvas.setCursor(10, detailY + 24);
  g_canvas.print(wifiStatus);

  // キュー状態
  size_t queueSize = g_pendingQueue.size();
  uint16_t queueColor;
  if (queueSize == 0) {
    queueColor = TFT_LIGHTGREY;  // 空：グレー
  } else if (queueSize <= 3) {
    queueColor = TFT_GREEN;  // 少ない：緑色
  } else if (queueSize <= 7) {
    queueColor = TFT_YELLOW;  // 中程度：黄色
  } else {
    queueColor = TFT_RED;  // 多い：赤色
  }
  g_canvas.setTextColor(queueColor, TFT_BLACK);
  g_canvas.setCursor(10, detailY + 36);
  g_canvas.printf("Queue: %d / %d",
                  static_cast<int>(queueSize),
                  static_cast<int>(MAX_QUEUE_SIZE));

  // 電池残量
  int batteryLevel = M5.Power.getBatteryLevel();
  uint16_t batteryColor;
  if (batteryLevel > 50) {
    batteryColor = TFT_GREEN;
  } else if (batteryLevel > 20) {
    batteryColor = TFT_YELLOW;
  } else {
    batteryColor = TFT_RED;
  }
  g_canvas.setTextColor(batteryColor, TFT_BLACK);
  g_canvas.setCursor(10, detailY + 48);
  g_canvas.printf("Battery: %d%%", batteryLevel);

  // ステータスメッセージ（POST状態など）
  if (!g_statusMessage.isEmpty() && millis() < g_statusMessageExpireAt) {
    g_canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
    g_canvas.setCursor(10, detailY + 60);
    g_canvas.print(g_statusMessage);
    g_canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  }
}

void drawStatusFooter() {
  int baseY = 220;
  g_canvas.setTextSize(1);
  
  // WiFi接続状態を表示
  String wifiStatus;
  uint16_t wifiColor;
  if (WiFi.status() == WL_CONNECTED) {
    if (g_connectedSsid.length() > 0) {
      wifiStatus = "Wi-Fi: " + g_connectedSsid;
    } else {
      wifiStatus = "Wi-Fi: Connected";
    }
    wifiColor = TFT_GREEN;
  } else if (g_wifiConnecting) {
    if (g_connectedSsid.length() > 0) {
      wifiStatus = "Wi-Fi: Connecting to " + g_connectedSsid;
    } else {
      wifiStatus = "Wi-Fi: Connecting...";
    }
    wifiColor = TFT_YELLOW;
  } else {
    wifiStatus = "Wi-Fi: Offline";
    wifiColor = TFT_LIGHTGREY;
  }
  
  g_canvas.setTextColor(wifiColor, TFT_BLACK);
  g_canvas.setCursor(10, baseY);
  g_canvas.print(wifiStatus);
  
  // キュー状態を表示
  g_canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g_canvas.setCursor(10, baseY + 12);
  g_canvas.printf("Queue: %d / %d",
                  static_cast<int>(g_pendingQueue.size()),
                  static_cast<int>(MAX_QUEUE_SIZE));
}

String phaseLabel(TimerPhase phase) {
  return (phase == TimerPhase::Work) ? String("Work") : String("Break");
}

String stateLabel(TimerState state) {
  switch (state) {
    case TimerState::Waiting:
      return "Waiting";
    case TimerState::Work:
      return "Work";
    case TimerState::Break:
      return "Break";
    case TimerState::Paused:
      return "Paused";
  }
  return "Unknown";
}

String formatDuration(unsigned long millisValue) {
  unsigned long totalSeconds = millisValue / 1000;
  unsigned long hours = totalSeconds / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;

  char buffer[16];
  if (hours > 0) {
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  } else {
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu", minutes, seconds);
  }
  return String(buffer);
}

String formatTime(time_t epoch, const char *format) {
  if (epoch <= 0) {
    return String("--:--");
  }
  tm timeinfo{};
  localtime_r(&epoch, &timeinfo);
  char buffer[32];
  strftime(buffer, sizeof(buffer), format, &timeinfo);
  return String(buffer);
}

String isoDateFromTime(const tm &timeinfo) {
  char buffer[16];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
  return String(buffer);
}

String displayDateFromTime(const tm &timeinfo) {
  char buffer[16];
  strftime(buffer, sizeof(buffer), "%Y/%m/%d", &timeinfo);
  return String(buffer);
}

String weekdayIdFromTime(const tm &timeinfo) {
  static const char *WEEKDAY_IDS[7] = {
      "7_Sun", "1_Mon", "2_Tue", "3_Wed", "4_Thu", "5_Fri", "6_Sut"};
  int index = clampValue(timeinfo.tm_wday, 0, 6);
  return String(WEEKDAY_IDS[index]);
}

float toRoundedQuarterHours(unsigned long millisValue) {
  // トータル経過時間（分）を計算
  unsigned long totalMinutes = millisValue / (60 * 1000);
  
  // 15分刻みで0.25時間ずつ増やす法則を適用
  // 1-15分 → 0.25時間、16-30分 → 0.5時間、31-45分 → 0.75時間、46-60分 → 1.0時間...
  // 15分単位で切り上げて0.25を掛ける
  double quarters = ceil(static_cast<double>(totalMinutes) / 15.0);
  return static_cast<float>(quarters * 0.25);
}

void buzzStart() {
  M5.Speaker.tone(1200, 120);
}

void buzzComplete() {
  // 短い2音を鳴らす
  M5.Speaker.tone(1000, 150);
  delay(100);
  M5.Speaker.tone(1000, 150);
}

void buzzShortBeep() {
  // 長押し検知時の短い1音
  M5.Speaker.tone(800, 50);
  delay(50);  // 音が確実に鳴るように待機
  M5.Speaker.end();  // ブザーを明示的に停止
}

void resetSession(bool regenerateSessionId) {
  g_timerState = TimerState::Waiting;
  g_currentPhase = TimerPhase::Work;
  g_nextPhase = TimerPhase::Work;
  g_stateStartMillis = 0;
  g_pauseStartMillis = 0;
  g_totalAccumulatedMillis = 0;
  g_sessionStartEpoch = 0;
  g_sessionActive = false;
  if (regenerateSessionId) {
    generateSessionId();
  }
  updateUi(true);
}

void handleStartPauseButton() {
  switch (g_timerState) {
    case TimerState::Waiting:
      startPhase(g_nextPhase);
      setStatusMessage("Timer started", 2000);
      break;
    case TimerState::Work:
    case TimerState::Break:
      g_timerState = TimerState::Paused;
      g_pauseStartMillis = millis();
      setStatusMessage("Paused", 2000);
      updateUi(true);
      break;
    case TimerState::Paused: {
      unsigned long pausedDuration = millis() - g_pauseStartMillis;
      g_stateStartMillis += pausedDuration;
      g_timerState = (g_currentPhase == TimerPhase::Work) ? TimerState::Work : TimerState::Break;
      g_pauseStartMillis = 0;
      setStatusMessage("Resumed", 2000);
      updateUi(true);
      break;
    }
  }
}

void handleSendButton() {
  unsigned long totalMillis = computeTotalElapsedMillis();
  if (totalMillis == 0) {
    setStatusMessage("Nothing to send", 2000);
    return;
  }

  if (!g_sessionActive) {
    g_sessionStartEpoch = time(nullptr);
    g_sessionActive = true;
  }

  tm startTimeInfo{};
  localtime_r(&g_sessionStartEpoch, &startTimeInfo);

  NotionPendingItem item;
  item.sessionStart = g_sessionStartEpoch;
  item.logTime = time(nullptr);
  item.roundedHours = toRoundedQuarterHours(totalMillis);
  item.isoDate = isoDateFromTime(startTimeInfo);
  item.displayDate = displayDateFromTime(startTimeInfo);
  item.notes = formatTime(g_sessionStartEpoch, "%I:%M %p");
  item.weekdayId = weekdayIdFromTime(startTimeInfo);

  g_pendingQueue.push_back(item);
  saveQueue();
  setStatusMessage("Queued log for Notion", 2000);

  resetSession(true);
  attemptImmediateQueueFlush();
}

void attemptImmediateQueueFlush() {
  unsigned long now = millis();
  if (g_pendingQueue.empty()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (now - g_lastQueueAttempt < QUEUE_RETRY_INTERVAL_MS) {
    return;
  }

  NotionPendingItem item = g_pendingQueue.front();
  if (postToNotion(item)) {
    g_pendingQueue.erase(g_pendingQueue.begin());
    saveQueue();
    setStatusMessage("Notion updated", 2000);
    // Reset attempt timer to try immediately for next item
    g_lastQueueAttempt = millis();
    if (!g_pendingQueue.empty()) {
      // Try next item without waiting
      attemptImmediateQueueFlush();
    }
  } else {
    g_lastQueueAttempt = now;
  }
}

bool postToNotion(const NotionPendingItem &item) {
  if (WiFi.status() != WL_CONNECTED || !g_credentials.isValid()) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (!https.begin(client, NOTION_API_URL)) {
    Serial.println("[HTTP] Failed to init HTTPS");
    return false;
  }

  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + g_credentials.notionToken);
  https.addHeader("Notion-Version", NOTION_VERSION);

  DynamicJsonDocument doc(2048);
  JsonObject parent = doc.createNestedObject("parent");
  parent["database_id"] = g_credentials.notionDatabaseId;

  JsonObject properties = doc.createNestedObject("properties");

  JsonObject dateProp = properties.createNestedObject("Date");
  JsonObject dateValue = dateProp.createNestedObject("date");
  // Notion expects ISO date; requirement requests YYYY/MM/DD for display, so convert.
  String isoDate = item.isoDate;
  isoDate.replace('/', '-');
  dateValue["start"] = isoDate;

  JsonObject hoursProp = properties.createNestedObject("H");
  hoursProp["number"] = item.roundedHours;

  JsonObject notesProp = properties.createNestedObject("Notes");
  JsonArray titleArray = notesProp.createNestedArray("title");
  JsonObject titleObj = titleArray.createNestedObject();
  JsonObject textObj = titleObj.createNestedObject("text");
  textObj["content"] = item.notes;
  titleObj["plain_text"] = item.notes;

  JsonObject catProp = properties.createNestedObject("Cat");
  JsonObject selectObj = catProp.createNestedObject("select");
  selectObj["name"] = "DeskWork";

  JsonObject dddProp = properties.createNestedObject("ddd");
  JsonArray richArray = dddProp.createNestedArray("rich_text");
  JsonObject richObj = richArray.createNestedObject();
  JsonObject richText = richObj.createNestedObject("text");
  richText["content"] = item.weekdayId;
  richObj["plain_text"] = item.weekdayId;

  JsonObject fromM5Prop = properties.createNestedObject("fromM5");
  fromM5Prop["checkbox"] = true;

  String payload;
  serializeJson(doc, payload);

  int httpCode = https.POST(payload);
  bool success = httpCode >= 200 && httpCode < 300;
  if (!success) {
    Serial.printf("[HTTP] Notion POST failed: %d\n", httpCode);
    String response = https.getString();
    Serial.println(response);
  }
  https.end();

  return success;
}

}  // namespace

