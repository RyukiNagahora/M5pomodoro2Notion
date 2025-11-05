# コンパイルエラー解決方法

## エラー内容
```
fatal error: rom/miniz.h: No such file or directory
```

## 原因
M5Stackライブラリが古いESP32のAPI（`rom/miniz.h`）を使用しようとしています。ESP32の新しいバージョン（2.0.0以降）では、このヘッダーファイルが削除されています。

## 解決方法

### 方法1: ESP32ボードパッケージのバージョンを下げる（推奨）

1. Arduino IDEを開く
2. `ファイル` → `環境設定` → `追加のボードマネージャーのURL` を確認
   - 以下のURLが追加されていることを確認：
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
3. `ツール` → `ボード` → `ボードマネージャー` を開く
4. 検索ボックスに「ESP32」と入力
5. 「esp32 by Espressif Systems」を探す
6. バージョン **1.0.6** を選択してインストール
   - 既に新しいバージョンがインストールされている場合：
     - 一度アンインストール
     - バージョン1.0.6を選択してインストール
7. `ツール` → `ボード` → `M5Stack-Core-ESP32` を選択
8. 再度コンパイルを試す

### 方法2: M5Stackライブラリを最新版に更新

1. Arduino IDEを開く
2. `スケッチ` → `ライブラリをインクルード` → `ライブラリを管理`
3. 検索ボックスに「M5Stack」と入力
4. 「M5Stack」を探して「更新」をクリック
5. 最新版に更新されていない場合は、一度アンインストールして再インストール

### 方法3: M5Stackライブラリのpngle.cを修正（上級者向け）

M5Stackライブラリのpngle.cファイルを直接編集する方法：

1. ライブラリフォルダを開く：
   - Windows: `C:\Users\<ユーザー名>\Documents\Arduino\libraries\M5Stack\src\utility\pngle.c`
2. 31行目の `#include <rom/miniz.h>` を以下のように変更：
   ```c
   #include <miniz.h>
   ```
   - または、条件付きコンパイルを使用：
   ```c
   #if ESP_ARDUINO_VERSION_MAJOR >= 2
   #include <miniz.h>
   #else
   #include <rom/miniz.h>
   #endif
   ```

## 推奨される設定

- **ESP32ボードパッケージ**: 1.0.6
- **M5Stackライブラリ**: 最新版
- **Partition Scheme**: Default 4MB with spiffs (3MB APP/9.9MB SPIFFS)

## 注意事項

- ESP32 2.0.0以降を使用する場合は、M5Stackライブラリが対応している必要があります
- 方法3は、ライブラリを更新すると変更が失われる可能性があります

---

## エラー2: SDライブラリの互換性エラー

### エラー内容
```
C:\Users\...\libraries\SD\src/utility/Sd2PinMap.h:527:2: error: #error Architecture or board not supported.
```

### 原因
Arduino IDEの標準`SD`ライブラリがESP32をサポートしていません。M5Stackライブラリが`SD.h`をインクルードしていますが、ESP32用の適切なSDライブラリが必要です。

### 解決方法

#### 方法1: ESP32ボードパッケージを1.0.6に下げる（最も確実）

ESP32 1.0.6にはESP32用のSDライブラリが含まれており、この問題が発生しにくいです。

1. `ツール` → `ボード` → `ボードマネージャー` を開く
2. 「esp32 by Espressif Systems」を検索
3. バージョン **1.0.6** を選択してインストール
4. 再度コンパイルを試す

#### 方法2: Arduino標準SDライブラリを無効化する

ESP32には標準でSDライブラリが含まれているため、Arduino標準のSDライブラリと競合している可能性があります。

1. Arduino IDEのライブラリフォルダを確認：
   - `C:\Users\<ユーザー名>\AppData\Local\Arduino15\libraries\SD`
2. このフォルダを一時的にリネーム（例：`SD_backup`）
3. 再度コンパイルを試す

**注意**: この方法は、他のプロジェクトでSDライブラリが必要な場合に影響する可能性があります。

#### 方法3: ESP32用SDライブラリを明示的にインストール

1. `スケッチ` → `ライブラリをインクルード` → `ライブラリを管理`
2. 検索ボックスに「SD ESP32」と入力
3. ESP32用のSDライブラリをインストール
4. 再度コンパイルを試す

### 推奨される対応

**最も推奨される方法は、ESP32ボードパッケージを1.0.6に下げることです。**
これにより、`rom/miniz.h`エラーと`SD`ライブラリエラーの両方が解決される可能性が高いです。

