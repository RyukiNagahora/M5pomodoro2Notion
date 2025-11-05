# M5Stack ポモドーロタイマー for Notion

M5Stack Basicを使用したポモドーロタイマーです。作業時間を測定し、Notionデータベースに自動で記録します。

## 機能

- ✅ **ポモドーロタイマー**: 作業25分・休憩5分のワークロード
- ✅ **時間計測**: 作業時間を累積して計測
- ✅ **Notion連携**: 測定した時間をNotionデータベースに自動送信
- ✅ **WiFi接続**: 複数のWiFiネットワークに対応（最大10個、自動検出）
- ✅ **キュー機能**: 送信失敗時はメモリ上にキューを保持して再送信
- ✅ **時間同期**: NTPサーバーから時刻を取得して同期

## 必要な環境

### ハードウェア
- M5Stack Basic
- USBケーブル（M5StackとPCを接続）

### ソフトウェア
- Arduino IDE 2.3.6以降（または1.8.x系）
- ESP32ボードパッケージ 1.0.6
- M5Stackライブラリ

### Notion側の準備
- Notionワークスペース
- 内部インテグレーション（Integration Token）
- データベース（以下のプロパティが必要）:
  - `Date`: Date型
  - `H`: Number型（時間）
  - `Notes`: Title型
  - `Cat`: Select型（"DeskWork"を選択）
  - `ddd`: Rich Text型

## セットアップ

### 1. Arduino IDEの準備

1. Arduino IDEをインストール
2. ESP32ボードパッケージをインストール
   - `ファイル` → `環境設定` → `追加のボードマネージャーのURL`に以下を追加:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - `ツール` → `ボード` → `ボードマネージャー`で「ESP32」を検索
   - **バージョン1.0.6**をインストール
3. M5Stackライブラリをインストール
   - `スケッチ` → `ライブラリをインクルード` → `ライブラリを管理`
   - 「M5Stack」を検索してインストール

### 2. コンパイルエラーの解決

コンパイルエラーが発生する場合は、`docs/compilation_fix.md`を参照してください。

主な解決方法：
- ESP32ボードパッケージを1.0.6に設定
- Arduino標準SDライブラリを無効化（`C:\Users\<ユーザー名>\AppData\Local\Arduino15\libraries\SD`を`SD_backup`にリネーム）

### 3. 設定ファイルの作成

1. プロジェクトのルートディレクトリに`secrets.h`ファイルを作成
2. 以下のテンプレートをコピーして、実際の値を入力:

```cpp
#ifndef SECRETS_H
#define SECRETS_H

// WiFi設定（最大10個まで設定可能）
#define WIFI_SSID_1 "YOUR_WIFI_SSID_1"
#define WIFI_PASSWORD_1 "YOUR_WIFI_PASSWORD_1"

#define WIFI_SSID_2 "YOUR_WIFI_SSID_2"
#define WIFI_PASSWORD_2 "YOUR_WIFI_PASSWORD_2"

// ... 必要に応じて追加（WIFI_SSID_3 から WIFI_SSID_10 まで）

// Notion設定
#define NOTION_TOKEN "YOUR_NOTION_TOKEN"
#define NOTION_DATABASE_ID "YOUR_DATABASE_ID"

#endif // SECRETS_H
```

3. 実際の値を入力:
   - `WIFI_SSID_1`, `WIFI_PASSWORD_1`: WiFiのSSIDとパスワード
   - `NOTION_TOKEN`: NotionのIntegration Token（`secret_...`で始まる）
   - `NOTION_DATABASE_ID`: NotionデータベースのID（32文字の英数字）

詳細は`docs/secrets_h_guide.md`を参照してください。

### 4. アップロード

1. `ツール` → `ボード` → `M5Stack-Core-ESP32`を選択
2. `ツール` → `Partition Scheme` → `Minimal SPIFFS (Large APPS with OTA)`を選択
3. USBケーブルでM5StackをPCに接続
4. `ツール` → `ポート`でCOMポートを選択
5. `スケッチ` → `アップロード`でプログラムをアップロード

## 使用方法

### 起動時

1. M5Stackを起動すると、WiFi接続フェーズが開始されます
2. 設定したWiFiネットワークに順番に接続を試行します
3. 接続が成功すると、タイマー画面が表示されます
4. 接続に失敗した場合、エラー画面で停止します

### タイマー操作

- **ボタンA**: 
  - 待機状態: タイマーを開始
  - 実行中: 一時停止/再開
- **ボタンB**: ブザーのON/OFF切り替え
- **ボタンC**: 累積時間をNotionに送信してリセット

### 画面表示

- **待機画面**: 次のフェーズ（作業/休憩）と累積時間を表示
- **実行画面**: 残り時間、累積時間、丸め時間を表示
- **ステータス表示**: 画面下部にWiFi状態、キュー数、ブザー状態を表示

### Notionへの送信

- ボタンCを押すと、現在の累積時間がNotionに送信されます
- 送信時間は0.25時間（15分）刻みで丸められます
- WiFiが接続されていない場合、メモリ上にキューとして保存されます
- WiFiが接続されると、自動的にキューから送信されます

## プロジェクト構成

```
M5pomodoro2Notion/
├── M5pomodoro2Notion.ino    # メインプログラム
├── secrets.h                 # 設定ファイル（.gitignoreで除外）
├── docs/                     # ドキュメント
│   ├── compilation_fix.md    # コンパイルエラー解決方法
│   ├── secrets_h_guide.md    # secrets.h使用ガイド
│   └── requirements.md       # 要件仕様書
├── .gitignore                # Git除外設定
└── LICENSE                   # ライセンスファイル
```

## トラブルシューティング

### WiFi接続に失敗する

- `secrets.h`ファイルが正しく作成されているか確認
- WiFiのSSIDとパスワードが正しいか確認
- WiFiネットワークが利用可能か確認

### Notionへの送信に失敗する

- WiFiが接続されているか確認
- NotionのIntegration Tokenが正しいか確認
- データベースIDが正しいか確認
- データベースに必要なプロパティが存在するか確認

### コンパイルエラーが発生する

`docs/compilation_fix.md`を参照してください。

## ライセンス

LICENSEファイルを参照してください。

## 参考資料

- [M5Stack公式サイト](https://m5stack.com/)
- [Notion API ドキュメント](https://developers.notion.com/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)

