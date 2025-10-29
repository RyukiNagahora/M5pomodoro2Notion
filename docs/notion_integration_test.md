# Notion 連携テスト手順

M5Stack Basic から Notion データベースへログを送れるかを検証するための準備と操作手順をまとめたドキュメントです。`examples/NotionPostTest/NotionPostTest.ino` の実装内容に合わせています。

## 1. Notion 側の準備
- 内部インテグレーションを作成し、シークレットトークン（`secret_...`）を取得する。
- 送信先のデータベースを用意し、**Date 型のプロパティ**（例: `Date`）を作成しておく。テストスケッチはこのプロパティにボタン押下時刻のみを記録する。
- データベース右上の「・・・」→「Add connections」から作成したインテグレーションを招待し、アクセス権を付与する。
- データベース ID を取得する。
  - データベースをブラウザで開き、URL の `.../` と `?` の間にある 32 文字前後の識別子をコピーする。`?v=` 以降は含めない。

## 2. M5Stack 側の準備
1. `examples/NotionPostTest/secrets_template.h` を同じフォルダに `secrets.h` という名前でコピーする。
2. `secrets.h` 内の
   - `WIFI_SSID`
   - `WIFI_PASSWORD`
   - `NOTION_TOKEN`
   - `NOTION_DATABASE_ID`
   を実際の値に書き換える。`secrets.h` は `.gitignore` 済みのため Git には含まれない。
3. `examples/NotionPostTest/NotionPostTest.ino` を Arduino IDE もしくは PlatformIO で開き、M5Stack Basic に書き込む。

## 3. テストの流れ
1. デバイス起動時に指定した Wi-Fi へ接続を試行する。成功すると NTP を利用して時刻を同期し、LCD に「Wi-Fi Connected」と表示される。
2. `BtnA` を **300ms 以上長押し**すると、現在時刻を取得して Notion API (`https://api.notion.com/v1/pages`) に POST する。
3. `BtnB` を押すと Wi-Fi 接続処理を再試行し、必要であれば再度時刻同期を行う。
4. 送信結果は LCD 表示（Success / Failed）とシリアルモニタ（115200bps）に出力される。シリアルログには送信した JSON と HTTP ステータス、レスポンス本文も表示される。

## 4. セキュリティ上の注意
- テスト用として `WiFiClientSecure::setInsecure()` を呼び、証明書検証を行っていない。実運用では Notion API のルート証明書を設定し、検証を有効化すること。
- `secrets.h` に記載した Wi-Fi パスワードやトークンは外部に漏らさないよう取り扱いに注意する。本番運用時には設定 UI や安全なストレージへ移行する計画を立てる。

## 5. 次のステップ
- POST が成功したことを確認したら、ポモドーロロジック本体に送信処理を統合する。
- 通信失敗時のリトライやローカルキューの保持方式を検討し、安定運用に向けた実装を進める。
- 証明書検証や設定入力 UI など、本番運用に向けて必要なセキュリティ・UX 改善を行う。
