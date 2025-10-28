# Notion 連携テスト手順

M5Stack Basic から Notion データベースへログを投稿できるか確認するための準備手順とテスト方法です。

## 1. Notion 側の準備
- 内部インテグレーションを作成し、シークレットトークン（`secret_...`）を取得する。
- 作業ログを保存したいデータベースを用意し、以下のプロパティを持たせる。
  - `Date`（Date）
  - `Duration (min)`（Number, 単位: 分）
  - `Break (min)`（Number, 単位: 分）
  - `Cycle Type`（Select, 例: `Focus`, `Long Break`）
  - `Source`（Select or Text）
  - `Memo`（Rich text, 任意）
  - `Tags`（Multi-select, 後から手動で設定）
- データベースの右上「…」→「Connect to」にて、作成したインテグレーションへアクセス権を付与する。
- データベース ID を取得する。
  - データベースをブラウザで開く → URL 中の `/` と `?` で囲まれた 32 文字前後の ID をコピー。

## 2. M5Stack 側で必要な情報
テストスケッチでは以下の値をハードコードする。後から設定 UI を実装するまでの暫定措置。
- `WIFI_SSID` / `WIFI_PASSWORD`
- `NOTION_TOKEN`（`secret_` から始まるトークン）
- `NOTION_DATABASE_ID`

## 3. テスト内容
1. M5Stack を起動すると指定した Wi-Fi へ接続する。
2. サンプルの作業ログ JSON を組み立て、`https://api.notion.com/v1/pages` に POST。
3. HTTP ステータス 200/201 を受け取れば Notion 側にページが生成される。
4. 成功・失敗は LCD 表示とシリアルログで確認できるようにする。

## 4. セキュリティ注意
- テストでは証明書検証を簡易化するため `WiFiClientSecure::setInsecure()` を使用する。
  - 実運用では Notion のルート証明書を設定し、証明書検証を有効化すること。
- 機密情報（Wi-Fi パスワード、トークン）は本番導入時に外部設定ファイルや UI から入力できるよう拡張する。

## 5. 次のステップ
- テストスケッチで通信が成功したことを確認後、ポモドーロ本体ロジックへ送信処理を組み込む。
- 通信エラー時のリトライ・ローカルキュー管理を設計し、安定運用に向けた実装へ移行する。

## 6. 同梱テストスケッチ
- `examples/NotionPostTest/NotionPostTest.ino` を Arduino IDE / PlatformIO で開いて書き込む。
- スケッチ冒頭の `WIFI_SSID`, `WIFI_PASSWORD`, `NOTION_TOKEN`, `NOTION_DATABASE_ID` を必ず書き換える。
- Notion 側のデータベースには Date 型プロパティ（例: `Date`）を用意しておく。送信されるのはボタンを押した日時のみ。
- 起動時に Wi-Fi へ接続し、NTP 経由で現在時刻を取得する。LCD に「Wi-Fi Connected」が表示されたら `BtnA` を押して POST を実行できる。
- `BtnB` を押すと Wi-Fi 接続処理を再試行し、必要であれば再同期を行う。
- シリアルモニタ（115200bps）と LCD 表示で POST の結果（成功／失敗）を確認できる。
