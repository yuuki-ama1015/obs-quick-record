# 進捗と方針

## 今後の実装方針
- Windows向け OBS Studio 外部プラグイン。OBS本体と obs-auto-stop は改造しない。
- OBS 32.2.2 / Qt 6.11系 ABI 互換を前提。録画はOBS標準API・標準ソースを使う。
- 進捗はGitで管理し、指示があればGitHub（Release含む）へ反映。私的メモ・個人設定・録画はリポジトリ／配布物に含めない。
- 再開時は README、docs/CLOUD_HANDOFF.md、docs/design.md、docs/testing.md を確認する。

## 完了したこと
- 範囲／ウィンドウ／モニター選択からのOBS標準録画、ホットキー、設定UI、OBS起動アシスト連携を実装
- 0.1.8候補の性能改善・配布準備と実機受け入れ記録（docs配下）
- 2026-11-02 物理ショートカット受け入れ確認とQA整理の記録
- 2026-11-09 PROGRESS.md運用を導入

## 次にやること
1. docs/testing.md / CLOUD_HANDOFF に残る未確認の実機条件を洗い出し、優先度を付ける（推測: MVP受入完了宣言の前）
2. README表記の現行バージョンと最新Releaseタグの整合を確認（推測）
3. 公開Issueは現時点で未作成。追加課題はIssueか docs に残す

## 未解決の問題・メモ
- README冒頭は 0.1.5 表記のまま。docs側は 0.1.8 受け入れ記録あり（表記ずれの可能性）
- オープンIssueなし
