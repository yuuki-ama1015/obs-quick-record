# 進捗と方針

## 今後の実装方針
- Windows向け OBS Studio 外部プラグイン。OBS本体と obs-auto-stop は改造しない（連携はOBS標準録画イベントのみ）。
- OBS 32.2.2 / Qt 6.11系 ABI 互換を前提。録画はOBS標準API・標準ソースを使う。
- 再開時は README、`docs/CLOUD_HANDOFF.md`、`docs/design.md`、`docs/testing.md` を確認する（詳細な再開手順は CLOUD_HANDOFF を正とする）。
- 進捗はGitで管理し、指示があればGitHub（Release含む）へ反映。私的メモ・個人設定・録画はリポジトリ／配布物に含めない。

## 完了したこと
- 範囲／ウィンドウ／モニター選択からのOBS標準録画、ホットキー、設定UI、OBS起動アシスト連携を実装
- 0.1.5〜0.1.8系の性能改善・配布準備（通知性能、リクエスト監視の解放など）
- 2026-10-20 / 2026-11-02: 隔離QAランタイムでの受け入れ（範囲・ウィンドウ・モニター、Enter開始/Esc取消、外部停止、Auto Stop共存、所有セッション自動終了、物理Alt+Rのユーザー確認）
- 2026-11-01: 0.1.8 公開・導入ファイル確認の記録
- 2026-11-09 PROGRESS.md運用を導入
- 2026-11-09 旧進捗メモ（acceptance / release-preparation / performance-review / CLOUD_PROMPT）を本ファイルへ統合して削除
- 2026-11-10 コードレビュー（軽量化・安定性）を実施。OBS 32.2.2公式ソース（ba2f32b）と照合し、改善案R1〜R12を下の「レビュー改善案」に記録（コード変更なし・Linux上の静的確認のみ）

## 次にやること
1. レビュー改善案（下記R1〜R12）の採否を決める。採用するならR1→R2→R3の順に、実機再現→修正→controller-check等へ回帰条件追加
2. 未再試験の境界を優先度付けして埋める: 混在DPI／複数モニタ、他Windows・OBS版、別ユーザー／管理者アカウント、再ログイン、ウィンドウ移動・リサイズ・遮蔽、実出力エラー保持
3. Auto Stop共存は最大録画時間停止のみ確認済み。静止／無音／メディア検出との組み合わせは別途（推測）
4. 物理キーでの手動停止パスの再確認（記録上は登録コールバック経由の確認が中心）
5. READMEは0.1.5表記、CMakeは0.1.8（2026-11-10確認）。READMEを更新する。`docs/CLOUD_HANDOFF.md`・`docs/testing.md`に残る削除済み文書（CLOUD_PROMPT / performance-review / acceptance-* / release-preparation-0.1.8）への参照も整理
6. 実録画条件下でのCPU／メモリ改善率の測定は未実施（処理回数削減≠実機改善率）

## 未解決の問題・メモ
- MVP受入完了は未宣言。詳細な決定事項・実装入口は `docs/CLOUD_HANDOFF.md`、試験記録は `docs/testing.md`
- 性能レビューで強調: 隔離一時フォルダ試験≠本番UAC承認試験。制限環境の一時ファイル拒否を実環境不良と混同しない
- 旧詳細ログ: acceptance `40e6218` / `acceptance-2026-10-20` 系、performance-review `11295d8`、release-preparation は git 履歴を参照

### レビュー改善案（2026-11-10、未採否・未実装・実機未再現）
安定性（OBS 32.2.2ソースで根拠確認済み）
- R1【高】録画中のシーンコレクション切替でOBSが「Source Cleanup Error」を出して終了し得る。`quick-record-controller.cpp`のSCENE_COLLECTION_CHANGINGはRecording中に停止要求だけ行い、`CaptureController::previous`（元シーン参照）をSTOPPEDまで保持する。停止は非同期。OBSの`ClearSceneData`は残存入力ソースを検出すると`clearingFailed`にし、`LoadData`がエラー表示後`close()`する（OBSBasic_SceneCollections.cpp 1201・1637〜1665行）。案: CHANGING時点で`previous`を即解放（復元は不要）。
- R2【中〜高】外部録画が同期的に開始失敗すると`externalRecording`がtrueのまま残り、以後「録画中」扱いで選択も自動終了も止まる。OBSの`StartRecording`はSTARTING後の失敗でSTOPPEDを出さない（OBSBasic_Recording.cpp 132〜136行、SimpleOutput.cpp 888〜907行）。案: 外部STARTINGにも期限を付け、非アクティブなら解除。
- R3【中】自前の開始がSTARTING前に拒否された場合（保存先無効・空き容量不足・出力無効化）、10秒の期限まで一時シーンのまま待つ。`obs_frontend_recording_start`はGUIスレッドで同期実行なので、戻った時点でSTARTING未観測なら即失敗にできる。
- R4【低〜中】`recordingResult`の初期値が失敗値と同じで、stopシグナル未着と失敗を区別できない。高負荷時にGUI側が先にSTOPPEDを処理すると誤って失敗通知・自動終了解除。案: 未着の番兵値＋短い再確認。
- R5【低】REC表示は`show()`後に`SetWindowDisplayAffinity`を設定するため冒頭フレームに写り得る。`winId()`で先に設定。
- R6【低】選択中のモニター構成・解像度・DPI変更を監視していない。`screenAdded/screenRemoved/geometryChanged`でキャンセル。
- R7【低】OBS設定側で変えたホットキーは録画開始・設定保存・終了時にしか書き出さない。`hotkey_bindings_changed`シグナルで即保存すると異常終了時も起動アシストと一致。
軽量化（プラグインDLLは待機中の定期処理ゼロで余地は小さい。主に起動アシスト）
- R8【中・要実測】起動アシストのWinEventフックは`EVENT_OBJECT_CREATE..SHOW`を全プロセス対象で受ける（不要なDESTROYも含む）。OBS未起動の通常待機中、他アプリのウィンドウ生成・破棄・表示のたびに起床する。既存ベンチマークはコールバック回数を数えていない。案: ①回数を計測に追加 ②CREATEとSHOWの2フックに分割 ③プラグインのロード時合図（名前付きイベント等）に置換しフック廃止。
- R9【低〜中】キー競合（RegisterHotKey失敗）中は2秒ごとに全プロセス走査を続ける。通知監視が有効なら走査を省き、登録再試行だけ間隔を延ばす。
- R10【低】「前回の範囲を記憶する」は保存のみで読み出しがない（`lastRegion`/`lastMonitor`、`CaptureRegion`は未使用）。復元を実装するか、項目と録画開始時の書き込みを削る。
- R11【低】起動待ち中の500msタイマーは、既存のフォルダー通知＋60秒期限1本に置換可能。
- R12【低】配布EXEに自己テスト・ベンチマーク用コードが同梱。別ターゲットへ分離可能（常駐メモリへの効果は小）。
見送り推奨: 選択オーバーレイの常駐キャッシュ（4Kで1面あたり約33MB）、WinRT JSONの置換、REC表示の1秒タイマー削除。
