# M5Stack ポモドーロタイマー for Notion

M5Stack Basicを使用したポモドーロタイマーです。作業時間を測定し、Notionデータベースに自動で記録します。

## 機能

- ✅ **ポモドーロタイマー**: 作業25分・休憩5分のワークロードを交互に実行
- ✅ **時間計測**: 作業時間と休憩時間を含むトータル経過時間を累積計測
- ✅ **時間丸め**: 送信時に15分刻みで0.25時間ずつ丸める（1-15分→0.25時間、16-30分→0.5時間、31-45分→0.75時間、46-60分→1.0時間、61-75分→1.25時間...）
- ✅ **Notion連携**: 測定した時間をNotionデータベースに自動送信（POST実行時にNTPサーバーから現時刻を取得して補正）
- ✅ **WiFi接続**: 複数のWiFiネットワークに対応（最大10個、バックグラウンドで自動検出・接続）
- ✅ **キュー機能**: 送信失敗時は不揮発性メモリ（Preferences/NVS）にキューを保持して再送信（最大10個、再起動後も保持）
- ✅ **時刻同期**: NTPサーバーから時刻を取得して日本標準時（Asia/Tokyo）で記録

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
  - `Date`: Date型（作業開始日時の暦日をJSTで保持）
  - `H`: Number型（休憩を含むトータル経過時間を時間単位で丸めた値。15分刻みで0.25時間ずつ増やす法則で丸める）
  - `Notes`: Title型（作業開始時刻を`HH:MM AM/PM`形式（12時間制）で記録）
  - `Cat`: Select型（固定値`DeskWork`を設定）
  - `ddd`: Text型（曜日IDを`1_Mon`～`7_Sun`形式で保存）
  - `fromM5`: Checkbox型（M5Stackデバイスからの送信を示すフラグ。常に`true`）

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

### 2. 設定ファイルの作成

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

### 3. アップロード

1. `ツール` → `ボード` → `M5Stack-Core-ESP32`を選択
2. `ツール` → `Partition Scheme` → `Minimal SPIFFS (Large APPS with OTA)`を選択
3. USBケーブルでM5StackをPCに接続
4. `ツール` → `ポート`でCOMポートを選択
5. `スケッチ` → `アップロード`でプログラムをアップロード

## 使用方法

### 起動時

1. M5Stackを起動すると、WiFi接続がバックグラウンドで開始されます
2. 設定したWiFiネットワークに順番に接続を試行します（接続待機による起動遅延は発生しません）
3. 接続が成功すると、接続できたSSIDがステータス表示に緑色で表示されます
4. 接続中は黄色、未接続時はグレーで表示されます
5. すべての設定を試行した後は最初の設定に戻って繰り返します

### タイマー操作

**すべてのボタン操作は長押し（200ms）で実行されます。長押し検知時に短い1音（800Hz、50ms）が鳴ります。**

- **ボタンA（長押し）**: 
  - 待機状態: タイマーを開始（作業フェーズまたは休憩フェーズ）
  - 実行中: 一時停止/再開
- **ボタンC（長押し）**: 累積時間をNotionに送信してリセット（送信後、タイマー状態・累積値・次に開始するターンを初期化した待機状態に遷移）

**注意**: ブザーは常に有効で、ミュート機能は提供されません。

### 画面表示

#### 待機画面
- **プログレスバー**: 画面上部にグレー色のプログレスバーを表示
  - バーの中に「Waiting / Next: [Phase]」形式で表示（例：「Waiting / Next: Work」「Waiting / Next: Break」）
- **トータル経過時間**: 「Total: HH:MM:SS」形式で表示
- **丸め込み値**: 「Rounded: X.XXh」形式で表示
- **詳細情報**（小さく表示）:
  - ボタンアサイン: A: Start, C: Send & Reset
  - Wi-Fi接続状態: 接続成功時は「Wi-Fi: [SSID]」（緑色）、接続中は「Wi-Fi: Connecting...」（黄色）、未接続時は「Wi-Fi: Offline」（グレー）
  - キュー状態: 「Queue: X / 10」形式で表示（0個: グレー、1-3個: 緑色、4-7個: 黄色、8-10個: 赤色）
  - 電池残量: 「Battery: XX%」形式で表示（50%超: 緑色、20%超50%以下: 黄色、20%以下: 赤色）
  - POST状況: 送信中は「Queued log for Notion」（黄色）、成功時は「Notion updated」（黄色）、失敗時は「POST失敗」（黄色）

#### 実行画面（作業フェーズ・休憩フェーズ）
- **プログレスバー**: 画面上部にプログレスバーを表示
  - 作業フェーズでは赤系の色、休憩フェーズでは青系の色
  - 残り時間とともに幅が小さくなる（残り時間が減るにつれてバーの幅も減る）
  - バーの中に「State / Phase」形式で表示（例：「Running / Work」「Paused / Work」「Running / Break」）
    - 実行中は「Running」、一時停止時は「Paused」（黄色）と表示
  - 右寄せで残り時間を大きく表示
- **トータル経過時間**: 「Total: HH:MM:SS」形式で表示
- **丸め込み値**: 「Rounded: X.XXh」形式で表示
- **詳細情報**（小さく表示）:
  - ボタンアサイン: A: Pause（実行中）/ Resume（一時停止中）、C: Send & Reset
  - Wi-Fi接続状態、キュー状態、電池残量、POST状況（待機画面と同じ）

#### 通知
- **作業フェーズ・休憩フェーズ開始時**: 短い1音（1200Hz、120ms）が鳴る
- **作業フェーズ・休憩フェーズ終了時**: 短い2音（1000Hz、150ms × 2回、間隔100ms）のブザーと画面表示で完了を通知し、タイマーは待機状態に戻る

### Notionへの送信

- ボタンC（長押し）を押すと、現在の累積時間がNotionに送信されます
- 送信時間は15分刻みで0.25時間ずつ丸められます（1-15分→0.25時間、16-30分→0.5時間、31-45分→0.75時間、46-60分→1.0時間、61-75分→1.25時間...）
- POST実行時、必ずNTPサーバーから現時刻を取得してDateを補正します（デバイスの時刻がずれていても正確な日時で記録されます）
- 日時はすべて日本標準時（Asia/Tokyo）で計算・整形されます
- WiFiが接続されている場合、そのままPOSTを実行します
- WiFiが未接続の場合、POST情報をキューとして不揮発性メモリ（Preferences/NVS）に保存します（最大10個まで）
- WiFiが接続されると、自動的にキューからFIFO（先入先出）で順次送信されます
- キューは再起動後も保持されるため、電源断や再起動が発生しても未送信データが失われることはありません
- POSTが成功した場合、ステータスに「Notion updated」を表示します
- POSTが失敗した場合、ステータスに「POST失敗」を表示し、情報をキューとしてストックします

## プロジェクト構成

```
M5pomodoro2Notion/
├── M5pomodoro2Notion.ino    # メインプログラム
├── secrets.h                 # 設定ファイル（.gitignoreで除外）
├── docs/                     # ドキュメント
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
- 接続中はステータス表示が黄色で「Wi-Fi: Connecting...」と表示されます
- 接続に失敗した場合は次のWiFi設定を試行し、すべての設定を試行した後は最初の設定に戻って繰り返します

### Notionへの送信に失敗する

- WiFiが接続されているか確認（ステータス表示で「Wi-Fi: [SSID]」が緑色で表示されているか）
- NotionのIntegration Tokenが正しいか確認
- データベースIDが正しいか確認
- データベースに必要なプロパティが存在するか確認（`Date`, `H`, `Notes`, `Cat`, `ddd`, `fromM5`）
- 送信失敗時はキューに保存され、WiFi接続が復旧すると自動的に再送信されます
- キュー状態を確認（「Queue: X / 10」が表示されているか）

## 制限事項

- **キュー容量**: キューは不揮発性メモリ（Preferences/NVS）に保存されますが、Preferencesの容量制限（約4000バイト）により、保存可能なキューアイテム数には上限があります。最大保存数は10個で、UI上に「X / 10」形式で表示されます。
- **タイマー状態の保持**: タイマー状態やトータル経過時間は再起動時にリセットされます。
- **統計情報**: 作業フェーズ完了回数、当日累積作業時間などの統計情報は保持されません。
- **ブザー**: ブザーは常に有効で、ミュート機能は提供されません。

## ライセンス

LICENSEファイルを参照してください。

## 参考資料

- [M5Stack公式サイト](https://m5stack.com/)
- [Notion API ドキュメント](https://developers.notion.com/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)

