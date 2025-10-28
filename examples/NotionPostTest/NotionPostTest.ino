#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets_template.h"
#warning "Using secrets_template.h placeholders. Create secrets.h with real credentials."
#endif

const char *NOTION_API_ENDPOINT = "https://api.notion.com/v1/pages";
const char *NOTION_VERSION = "2022-06-28";

WiFiClientSecure secureClient;

void connectToWiFi();
bool ensureTimeSync();
String currentIsoTime();
bool postNotionLog();
void showStatus(const char *title, const char *message, uint16_t color);
void showReadyScreen();

bool wifiReady = false;
bool isPosting = false;

void setup() {
  M5.begin();
  M5.Power.begin();
  Serial.begin(115200);
  delay(500);

  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(TFT_BLACK);
  showStatus("Wi-Fi", "Connecting...", TFT_YELLOW);

  connectToWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    ensureTimeSync();
    showReadyScreen();
  } else {
    showStatus("Wi-Fi", "Failed", TFT_RED);
  }
}

void loop() {
  M5.update();

  if (M5.BtnA.pressedFor(300)) {
    if (!wifiReady) {
      showStatus("Wi-Fi", "Retry...", TFT_YELLOW);
      connectToWiFi();
      if (WiFi.status() == WL_CONNECTED) {
        wifiReady = true;
        ensureTimeSync();
        showReadyScreen();
      } else {
        showStatus("Wi-Fi", "Failed", TFT_RED);
      }
      return;
    }

    if (!isPosting) {
      isPosting = true;
      showStatus("Notion", "Posting...", TFT_YELLOW);
      bool success = postNotionLog();
      if (success) {
        showStatus("Notion", "Success", TFT_GREEN);
      } else {
        showStatus("Notion", "Failed", TFT_RED);
      }
      delay(1500);
      showReadyScreen();
      isPosting = false;
    }
  }

  if (M5.BtnB.wasPressed()) {
    wifiReady = false;
    showStatus("Wi-Fi", "Retry...", TFT_YELLOW);
    connectToWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      wifiReady = true;
      ensureTimeSync();
      showReadyScreen();
    } else {
      showStatus("Wi-Fi", "Failed", TFT_RED);
    }
  }
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  const unsigned long timeout = 15000; // 15 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < timeout) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi connection failed.");
  }
}

bool ensureTimeSync() {
  static bool timeSynced = false;
  if (timeSynced) {
    return true;
  }

  configTime(9 * 3600, 0, "ntp.nict.jp", "pool.ntp.org", "time.cloudflare.com");

  struct tm timeinfo;
  const int maxAttempts = 10;
  for (int i = 0; i < maxAttempts; ++i) {
    if (getLocalTime(&timeinfo, 1000)) {
      timeSynced = true;
      Serial.println("Time synchronized.");
      return true;
    }
    delay(500);
  }

  Serial.println("Failed to synchronize time.");
  return false;
}

String currentIsoTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) {
    Serial.println("getLocalTime failed, using placeholder.");
    return "1970-01-01T00:00:00+00:00";
  }

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
  String iso(buffer);
  if (iso.length() >= 5) {
    iso = iso.substring(0, iso.length() - 2) + ":" + iso.substring(iso.length() - 2);
  }
  return iso;
}

bool postNotionLog() {
  secureClient.setInsecure(); // テスト用途: 実環境では証明書検証を設定する
  HTTPClient http;
  if (!http.begin(secureClient, NOTION_API_ENDPOINT)) {
    Serial.println("Failed to initialize HTTP client.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Notion-Version", NOTION_VERSION);
  http.addHeader("Authorization", String("Bearer ") + NOTION_TOKEN);

  String isoTime = currentIsoTime();
  String payload = String("{\"parent\":{\"database_id\":\"") + NOTION_DATABASE_ID + "\"},"
                      "\"properties\":{"
                      "\"Date\":{\"date\":{\"start\":\"" + isoTime + "\"}}"
                      "}"
                      "}";

  Serial.println("Sending payload:");
  Serial.println(payload);

  int statusCode = http.POST(payload);
  Serial.printf("HTTP Status: %d\n", statusCode);

  String response = http.getString();
  Serial.println("Response:");
  Serial.println(response);

  http.end();

  return statusCode == 200 || statusCode == 201;
}

void showStatus(const char *title, const char *message, uint16_t color) {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(color, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(20, 60);
  M5.Lcd.print(title);
  M5.Lcd.setCursor(20, 100);
  M5.Lcd.print(message);
}

void showReadyScreen() {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(20, 40);
  M5.Lcd.println("Wi-Fi Connected");

  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(20, 80);
  M5.Lcd.println("Press BtnA");
  M5.Lcd.setCursor(20, 100);
  M5.Lcd.println("to POST to Notion");
  M5.Lcd.setCursor(20, 140);
  M5.Lcd.println("BtnB: retry Wi-Fi");
}
