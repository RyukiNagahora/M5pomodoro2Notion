#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
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
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 15000UL;
constexpr unsigned long QUEUE_RETRY_INTERVAL_MS = 10000UL;
constexpr unsigned long STATUS_MESSAGE_DURATION_MS = 4000UL;

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

TimerState g_timerState = TimerState::Waiting;
TimerPhase g_currentPhase = TimerPhase::Work;
TimerPhase g_nextPhase = TimerPhase::Work;

unsigned long g_stateStartMillis = 0;
unsigned long g_pauseStartMillis = 0;
unsigned long g_totalAccumulatedMillis = 0;
unsigned long g_lastUiRefresh = 0;
unsigned long g_lastQueueAttempt = 0;
unsigned long g_lastWifiAttempt = 0;

time_t g_sessionStartEpoch = 0;
bool g_sessionActive = false;

bool g_buzzerEnabled = true;
bool g_timeSynced = false;
bool g_queueDirty = false;

String g_sessionId;
String g_statusMessage;
unsigned long g_statusMessageExpireAt = 0;

TFT_eSprite g_canvas = TFT_eSprite(&M5.Lcd);

TimerState g_lastUiState = TimerState::Waiting;
TimerPhase g_lastUiPhase = TimerPhase::Work;
unsigned long g_lastUiTotalSeconds = 0;
unsigned long g_lastUiTimeLeftSeconds = 0;
bool g_lastUiPaused = false;
bool g_lastUiBuzzer = true;
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
void resetSession(bool regenerateSessionId);
void handleStartPauseButton();
void handleBuzzerToggle();
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

  // WiFi接続フェーズ：接続成功まで待機
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("Connecting WiFi...");
  
  bool wifiConnected = false;
  unsigned long wifiStartTime = millis();
  const unsigned long MAX_WAIT_TIME = 60000;  // 最大60秒待機
  
  while (!wifiConnected && (millis() - wifiStartTime < MAX_WAIT_TIME)) {
    wifiConnected = connectWiFiAndWait(true);
    
    if (!wifiConnected) {
      M5.Lcd.setCursor(10, 40);
      M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
      M5.Lcd.print("Retrying...");
      delay(2000);
      M5.Lcd.fillRect(0, 40, 320, 40, TFT_BLACK);
    }
  }
  
  if (!wifiConnected) {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.print("WiFi Failed");
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.print("Cannot continue");
    while(1) delay(1000);  // WiFi接続失敗のため停止
  }
  
  // WiFi接続成功
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("WiFi Connected!");
  delay(1000);

  generateSessionId();
  resetSession(false);

  syncTime();

  updateUi(true);
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    handleStartPauseButton();
  }
  if (M5.BtnB.wasPressed()) {
    handleBuzzerToggle();
  }
  if (M5.BtnC.wasPressed()) {
    handleSendButton();
  }

  updateTimer();
  attemptImmediateQueueFlush();

  // Periodically retry Wi-Fi if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - g_lastWifiAttempt > WIFI_RETRY_INTERVAL_MS) {
      connectWiFi(true);
      g_lastWifiAttempt = now;
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

  for (const auto &cred : g_credentials.wifiNetworks) {
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      break;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    setStatusMessage("Wi-Fi disconnected");
  } else {
    setStatusMessage("Wi-Fi connected", 2000);
  }
  updateUi(true);
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

// Queue機能はメモリ上のみで動作（SPIFFS不要）
// キューはメモリ上に保持され、再起動時にクリアされます
void ensureQueueLoaded() {
  // メモリ上のみで動作するため、読み込み処理は不要
  g_pendingQueue.clear();
}

void saveQueue() {
  // メモリ上のみで動作するため、保存処理は不要
  // キューはメモリ上に保持され、再起動時にクリアされます
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

  if (g_buzzerEnabled) {
    buzzStart();
  }
  updateUi(true);
}

void enterWaitingState(TimerPhase upcomingPhase, bool fromCompletion) {
  g_timerState = TimerState::Waiting;
  g_nextPhase = upcomingPhase;
  g_stateStartMillis = 0;
  g_pauseStartMillis = 0;
  g_currentPhase = upcomingPhase;
  if (fromCompletion && g_buzzerEnabled) {
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
                      g_buzzerEnabled != g_lastUiBuzzer ||
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

  drawStatusFooter();
  g_canvas.pushSprite(0, 0);

  g_lastUiState = g_timerState;
  g_lastUiPhase = g_currentPhase;
  g_lastUiTotalSeconds = totalSeconds;
  g_lastUiTimeLeftSeconds = timeLeftSeconds;
  g_lastUiPaused = isPaused;
  g_lastUiBuzzer = g_buzzerEnabled;
  g_lastUiQueueSize = g_pendingQueue.size();
  g_lastUiWifiStatus = wifiConnected;
}

void drawWaitingScreen(unsigned long totalMillis) {
  g_canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  g_canvas.setTextSize(2);
  g_canvas.setCursor(10, 10);
  g_canvas.print("State: ");
  g_canvas.print(stateLabel(TimerState::Waiting));

  g_canvas.setCursor(10, 40);
  g_canvas.printf("Next: %s", phaseLabel(g_nextPhase).c_str());

  g_canvas.setCursor(10, 70);
  g_canvas.print("Total: ");
  g_canvas.print(formatDuration(totalMillis));

  float roundedHours = toRoundedQuarterHours(totalMillis);
  g_canvas.setCursor(10, 100);
  g_canvas.printf("Rounded: %.2fh", roundedHours);

  g_canvas.setTextSize(1);
  const int waitingMessageY = 130;
  const int waitingButtonsStartY = 160;

  if (!g_statusMessage.isEmpty() && millis() < g_statusMessageExpireAt) {
    g_canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
    g_canvas.setCursor(10, waitingMessageY);
    g_canvas.println(g_statusMessage);
    g_canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  int waitingButtonY = waitingButtonsStartY;
  g_canvas.setCursor(10, waitingButtonY);
  g_canvas.print("A: Start");
  waitingButtonY += 15;
  g_canvas.setCursor(10, waitingButtonY);
  g_canvas.print("B: Buzzer On/Off");
  waitingButtonY += 15;
  g_canvas.setCursor(10, waitingButtonY);
  g_canvas.print("C: Send & Reset");

  g_canvas.setCursor(10, 205);
  g_canvas.printf("Session: %s", g_sessionId.c_str());
}

void drawActiveScreen(bool paused, unsigned long timeLeftMillis, unsigned long totalMillis) {
  uint16_t barColor = (g_currentPhase == TimerPhase::Work) ? 0xf920 : 0x051f;
  uint16_t textColor = TFT_WHITE;

  unsigned long duration = currentPhaseDuration();
  unsigned long elapsed = currentPhaseElapsed();
  float progress = clampValue(static_cast<float>(elapsed) / static_cast<float>(duration), 0.0f, 1.0f);
  int barWidth = static_cast<int>(progress * g_canvas.width());

  g_canvas.fillRect(0, 0, barWidth, 80, barColor);

  g_canvas.setTextColor(textColor, TFT_BLACK);
  g_canvas.setTextSize(2);
  g_canvas.setCursor(10, 10);
  g_canvas.printf("%s", phaseLabel(g_currentPhase).c_str());

  g_canvas.setCursor(10, 40);
  if (paused) {
    g_canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
    g_canvas.setTextSize(3);
    g_canvas.print("Paused");
    g_canvas.setTextSize(2);
    g_canvas.setTextColor(textColor, TFT_BLACK);
  } else {
    g_canvas.print("Running");
  }

  g_canvas.setCursor(10, 80);
  g_canvas.print("Time left: ");
  g_canvas.print(formatDuration(timeLeftMillis));

  g_canvas.setCursor(10, 110);
  g_canvas.print("Total: ");
  g_canvas.print(formatDuration(totalMillis));

  float roundedHours = toRoundedQuarterHours(totalMillis);
  g_canvas.setCursor(10, 140);
  g_canvas.printf("Rounded: %.2fh", roundedHours);

  g_canvas.setTextSize(1);

  const int activeMessageY = 130;
  const int activeButtonsStartY = 160;
  if (!g_statusMessage.isEmpty() && millis() < g_statusMessageExpireAt) {
    g_canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
    g_canvas.setCursor(10, activeMessageY);
    g_canvas.println(g_statusMessage);
    g_canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  int activeButtonY = activeButtonsStartY;
  g_canvas.setCursor(10, activeButtonY);
  g_canvas.print("A: Pause/Resume");
  activeButtonY += 15;
  g_canvas.setCursor(10, activeButtonY);
  g_canvas.print("C: Send & Reset");

  g_canvas.setCursor(10, 205);
  g_canvas.printf("Session: %s", g_sessionId.c_str());
}

void drawStatusFooter() {
  int baseY = 220;
  g_canvas.setTextSize(1);
  g_canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g_canvas.setCursor(10, baseY);
  g_canvas.printf("Wi-Fi: %s  Queue: %d  Buzzer: %s",
                  (WiFi.status() == WL_CONNECTED) ? "Connected" : "Offline",
                  static_cast<int>(g_pendingQueue.size()),
                  g_buzzerEnabled ? "On" : "Off");
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
  double hours = static_cast<double>(millisValue) / 3600000.0;
  double rounded = round(hours * 4.0) / 4.0;
  return static_cast<float>(rounded);
}

void buzzStart() {
  M5.Speaker.tone(1200, 120);
}

void buzzComplete() {
  M5.Speaker.tone(1000, 200);
  delay(180);
  M5.Speaker.tone(1400, 200);
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

void handleBuzzerToggle() {
  g_buzzerEnabled = !g_buzzerEnabled;
  setStatusMessage(g_buzzerEnabled ? "Buzzer On" : "Buzzer Off", 2000);
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
  item.notes = formatTime(g_sessionStartEpoch, "%H:%M");
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

