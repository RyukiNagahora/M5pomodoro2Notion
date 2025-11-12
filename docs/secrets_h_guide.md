# secrets.hファイルを使用した設定方法

## 概要
Arduino IDEで設定ファイルを別ファイル（`secrets.h`）として読み込む方法を実装しました。これにより、機密情報をメインのコードから分離できます。

## 使用方法

### ステップ1: secrets.hファイルを作成

プロジェクトのルートディレクトリ（`M5pomodoro2Notion.ino`と同じ場所）に`secrets.h`ファイルを作成します。

### ステップ2: 設定を入力

`secrets.h`ファイルを開き、以下のテンプレートに実際の値を入力します：

```cpp
// secrets.h - 機密情報を含む設定ファイル
// このファイルはGitにコミットしないでください（.gitignoreに追加済み）

#ifndef SECRETS_H
#define SECRETS_H

// WiFi設定
#define WIFI_SSID_1 "YOUR_WIFI_SSID_1"
#define WIFI_PASSWORD_1 "YOUR_WIFI_PASSWORD_1"

// バックアップWiFi設定（オプション）
#define WIFI_SSID_2 "YOUR_WIFI_SSID_2"
#define WIFI_PASSWORD_2 "YOUR_WIFI_PASSWORD_2"

// 3つ目のWiFi設定（オプション）
#define WIFI_SSID_3 "YOUR_WIFI_SSID_3"
#define WIFI_PASSWORD_3 "YOUR_WIFI_PASSWORD_3"

// 4つ目以降のWiFi設定も追加可能（オプション、最大10個まで）
// #define WIFI_SSID_4 "YOUR_WIFI_SSID_4"
// #define WIFI_PASSWORD_4 "YOUR_WIFI_PASSWORD_4"
// ... (必要なだけ追加)

// Notion設定
#define NOTION_TOKEN "YOUR_NOTION_TOKEN"
#define NOTION_DATABASE_ID "YOUR_DATABASE_ID"

#endif // SECRETS_H
```

### ステップ3: 実際の値を入力

例：
```cpp
#define WIFI_SSID_1 "MyWiFiNetwork"
#define WIFI_PASSWORD_1 "MyPassword123"
#define WIFI_SSID_2 "BackupWiFi"
#define WIFI_PASSWORD_2 "BackupPassword123"
#define NOTION_TOKEN "secret_xxxxxxxxxxxx"  // または "ntn_xxxxxxxxxxxx" の形式
#define NOTION_DATABASE_ID "1d6155367661809aa714e596439955f1"  // 32文字の英数字
```

**重要**:
- `NOTION_TOKEN`: NotionのIntegration Token（通常は`secret_...`または`ntn_...`で始まる）
- `NOTION_DATABASE_ID`: NotionデータベースのID（32文字の英数字）

### ステップ4: コードをアップロード

`secrets.h`ファイルを保存し、通常通りコードをアップロードします。

## 動作の仕組み

1. **自動検出**: `secrets.h`が存在する場合、自動的に読み込まれます
2. **設定の読み込み**: `secrets.h`から設定を読み込み、WiFiとNotionの認証情報を設定します
3. **WiFi接続の動作**: 
   - 起動時に`secrets.h`に記載されているWiFi情報から接続可能なネットワークを順次探し続けます
   - 接続に失敗した場合は次のWiFi設定を試行します
   - すべての設定を試行した後は最初の設定に戻って繰り返します
   - 接続はバックグラウンドで実行されるため、接続待機による起動遅延は発生しません
   - 接続が成功すると、接続できたSSIDがステータス表示に緑色で表示されます

## メリット

- ✅ **機密情報の分離**: メインコードから機密情報を分離
- ✅ **Git管理**: `.gitignore`に追加済みなので、誤ってコミットされる心配がない
- ✅ **簡単**: SPIFFSが動作しなくても使用可能
- ✅ **複数のWiFi**: 最大10個のWiFiネットワークを設定可能（自動検出・順次接続試行）
- ✅ **自動接続**: 設定したWiFiネットワークから接続可能なものを自動的に検出・接続
- ✅ **バックグラウンド実行**: WiFi接続がバックグラウンドで実行されるため、起動遅延が発生しない

## 注意事項

- `secrets.h`ファイルは`.gitignore`に追加済みです
- このファイルをGitにコミットしないでください
- 実際の値を入力する際は、ダブルクォート（`"`）を忘れないでください
- **M5デバイス上での入力・保存機能は提供されません**。設定は必ずPC上で`secrets.h`ファイルを編集してからアップロードしてください
- WiFi設定は最大10個まで設定可能です。1つ目（`WIFI_SSID_1`）は必須、2つ目以降はオプションです
- 複数のWiFi設定を追加した場合、接続可能なネットワークを順次検出・接続します

## ファイル構造

```
M5pomodoro2Notion/
├── M5pomodoro2Notion.ino
├── secrets.h              ← このファイルを作成（プロジェクトのルートディレクトリ）
├── docs/
│   ├── secrets_h_guide.md # このファイル
│   └── requirements.md
├── .gitignore             # secrets.hは除外済み
└── ...
```

## トラブルシューティング

### 設定が読み込まれない場合

1. `secrets.h`ファイルが正しい場所にあるか確認（`M5pomodoro2Notion.ino`と同じディレクトリ）
2. ファイル名が正確か確認（`secrets.h`）
3. 値にダブルクォートが含まれているか確認
4. プレースホルダー値（`YOUR_WIFI_SSID_1`など）を実際の値に置き換えているか確認
5. `#ifndef SECRETS_H`と`#endif // SECRETS_H`が正しく記述されているか確認

### secrets.hが見つからない場合

`secrets.h`ファイルが見つからない場合、画面に「secrets.h not found」というエラーメッセージが表示されます。この場合、WiFiやNotion機能は使用できませんが、タイマー機能は動作します。

### WiFi接続に失敗する場合

1. `WIFI_SSID_1`と`WIFI_PASSWORD_1`が正しく設定されているか確認（1つ目のWiFi設定は必須）
2. WiFiのSSIDとパスワードが正しいか確認
3. WiFiネットワークが利用可能か確認
4. 複数のWiFi設定を追加している場合、接続可能なネットワークが存在するか確認
5. 接続中はステータス表示が黄色で「Wi-Fi: Connecting...」と表示されます
6. 接続に失敗した場合は次のWiFi設定を自動的に試行します
7. すべての設定を試行した後は最初の設定に戻って繰り返します

### Notion接続に失敗する場合

1. `NOTION_TOKEN`が正しく設定されているか確認（`secret_...`または`ntn_...`で始まる）
2. `NOTION_DATABASE_ID`が正しく設定されているか確認（32文字の英数字）
3. NotionのIntegration Tokenが有効か確認
4. データベースIDが正しいか確認（NotionデータベースのURLから取得）

