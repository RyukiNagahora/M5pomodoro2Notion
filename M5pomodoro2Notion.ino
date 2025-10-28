#include <M5Stack.h>

unsigned long startMillis;
unsigned long pausedMillis = 0;
unsigned long elapsedMillis;
bool isRunning = false;
bool isPaused = false;
bool isWorkTime = true;
bool isBuzzerOn = true; // ブザーの初期状態
int cycleCount = 1; // サイクルを1から始める

const unsigned long workDuration = 25 * 60 * 1000; // 25 minutes in milliseconds
const unsigned long breakDuration = 5 * 60 * 1000; // 5 minutes in milliseconds

const uint16_t WORK_COLOR = 0xf920; // 赤 (#ff2600)
const uint16_t BREAK_COLOR = 0x051f; // 青 (#00a2ff)
const uint16_t PAUSE_COLOR = 0x52aa; // グレー (#575757)

unsigned long lastMinutes = 0;
unsigned long lastSeconds = 0;
bool lastIsWork = true;
bool lastIsPaused = false;

int barWidth = 0; // 進行バーの幅を保持する変数

TFT_eSprite sprite = TFT_eSprite(&M5.Lcd); // TFT_eSpriteのインスタンスを作成

void setup() {
  M5.begin();
  M5.Power.begin(); 
  M5.Lcd.clear();
  M5.Lcd.setRotation(1); // 画面の向きを設定
  sprite.setColorDepth(8); // カラーデプスを設定
  sprite.createSprite(M5.Lcd.width(), M5.Lcd.height()); // スプライトのサイズを設定
  showInitialScreen();
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    if (isRunning) {
      if (isPaused) {
        isPaused = false;
        lastIsPaused = false;
        startMillis = millis() - pausedMillis;
      } else {
        isPaused = true;
        pausedMillis = millis() - startMillis;
        showPausedScreen();
      }
    } else {
      isRunning = true;
      isPaused = false;
      lastIsPaused = false;
      startMillis = millis();
      if (isBuzzerOn) buzzSingle();
      updateScreen();
    }
  }

  if (M5.BtnB.wasPressed()) {
    resetTimer();
  }

  if (M5.BtnC.wasPressed()) {
    isBuzzerOn = !isBuzzerOn; // ブザーのオン/オフを切り替え
    updateScreen();
  }

  if (isRunning && !isPaused) {
    unsigned long currentMillis = millis();
    elapsedMillis = currentMillis - startMillis;

    if (isWorkTime) {
      if (elapsedMillis >= workDuration) {
        isRunning = false;
        isWorkTime = false;
        if (isBuzzerOn) buzzDouble();
        updateScreen();
      } else {
        showTime(workDuration - elapsedMillis, workDuration, true);
      }
    } else {
      if (elapsedMillis >= breakDuration) {
        isRunning = false;
        isWorkTime = true;
        cycleCount++; // サイクルカウントを増やす
        if (isBuzzerOn) buzzDouble();
        updateScreen();
      } else {
        showTime(breakDuration - elapsedMillis, breakDuration, false);
      }
    }
  }
}

void showTime(unsigned long timeLeft, unsigned long totalDuration, bool isWork) {
  unsigned long minutes = (timeLeft / 1000) / 60;
  unsigned long seconds = (timeLeft / 1000) % 60;
  
  if (seconds != lastSeconds || isWork != lastIsWork || lastIsPaused) {
    lastMinutes = minutes;
    lastSeconds = seconds;
    lastIsWork = isWork;
    lastIsPaused = false;

    float progress = (float)timeLeft / totalDuration;
    barWidth = progress * 320;

    sprite.fillSprite(TFT_BLACK);
    sprite.fillRect(0, 0, barWidth, 240, isWork ? WORK_COLOR : BREAK_COLOR);

    sprite.setTextColor(TFT_WHITE);
    sprite.setCursor(10, 10);
    sprite.setTextSize(2);
    sprite.println(isWork ? "WorkTime" : "BreakTime");

    sprite.setTextSize(3);
    sprite.setCursor(10, 40);
    sprite.printf("%02lu:%02lu", minutes, seconds);

    sprite.setTextSize(2);
    sprite.setCursor(10, 70);
    sprite.printf("Cycle: %d", cycleCount);

    showBatteryStatus();

    sprite.pushSprite(0, 0); // スプライトを表示
  }
}

void showPausedScreen() {
  sprite.fillRect(0, 0, barWidth, 240, PAUSE_COLOR);
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextSize(2);
  sprite.setCursor(10, 10);
  sprite.println(isWorkTime ? "WorkTime" : "BreakTime");

  sprite.setTextSize(3);
  sprite.setCursor(10, 40);
  sprite.printf("%02lu:%02lu", lastMinutes, lastSeconds);

  sprite.setTextSize(2);
  sprite.setCursor(10, 70);
  sprite.printf("Cycle: %d", cycleCount);

  sprite.setCursor(10, 100);
  sprite.println("Paused");

  showBatteryStatus();

  sprite.pushSprite(0, 0); // スプライトを表示
  lastIsPaused = true;
}

void showInitialScreen() {
  sprite.fillSprite(TFT_BLACK);
  sprite.setTextSize(2);
  if (isWorkTime) {
    sprite.setTextColor(WORK_COLOR);
    sprite.setCursor(10, 10);
    sprite.println("WorkTime");
  } else {
    sprite.setTextColor(BREAK_COLOR);
    sprite.setCursor(10, 10);
    sprite.println("BreakTime");
  }

  sprite.setTextColor(TFT_WHITE);
  sprite.setCursor(10, 40);
  sprite.println("Press");
  sprite.setCursor(10, 60);
  sprite.println("BtnA to");
  sprite.setCursor(10, 80);
  sprite.println("start");

  sprite.setCursor(10, 100);
  sprite.printf("Cycle: %d", cycleCount);

  showBatteryStatus();

  sprite.pushSprite(0, 0); // スプライトを表示
}

void updateScreen() {
  if (isRunning) {
    unsigned long currentMillis = millis();
    elapsedMillis = currentMillis - startMillis;
    if (isWorkTime) {
      showTime(workDuration - elapsedMillis, workDuration, true);
    } else {
      showTime(breakDuration - elapsedMillis, breakDuration, false);
    }
  } else {
    showInitialScreen();
  }
}

void showBatteryStatus() {
  int batteryLevel = M5.Power.getBatteryLevel();
  bool isCharging = M5.Power.isCharging();

  sprite.setTextColor(TFT_WHITE);
  sprite.setCursor(240, 10); // 画面の右上に表示
  sprite.setTextSize(1);

  if (isCharging) {
    sprite.println("Charging");
  } else {
    sprite.printf("Battery: %d%%", batteryLevel);
  }

  sprite.setCursor(240, 30); // 電池残量の下に表示
  sprite.printf("Buzzer: %s", isBuzzerOn ? "On" : "Off");
}

void buzzSingle() {
  M5.Speaker.tone(1000, 200); // 1KHzの音を200ms鳴らす
}

void buzzDouble() {
  M5.Speaker.tone(1000, 200); // 1KHzの音を200ms鳴らす
  delay(200);
  M5.Speaker.tone(1000, 200); // 1KHzの音を200ms鳴らす
}

void resetTimer() {
  isRunning = false;
  isPaused = false;
  startMillis = 0;
  pausedMillis = 0;
  elapsedMillis = 0;
  isWorkTime = true;
  showInitialScreen();
}
