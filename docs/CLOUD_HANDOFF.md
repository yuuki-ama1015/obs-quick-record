# GitHubから開発を再開する

更新日：2026-10-04。対象はWindows用OBS Quick Record 0.1.0開発版です。未確認の実機条件があり、安定版・MVP受入完了とは宣言していません。

## 再開の入口

1. https://github.com/yuuki-ama1015/obs-quick-record のmainをcloneする。最新コードはmain、配布時点のコードは各Releaseのタグを参照する。
2. AGENTS.md、README.md、この文書、docs/design.md、docs/testing.mdの末尾を読む。担当者へ渡すプロンプトはdocs/CLOUD_PROMPT.md。
3. 最新の開発版Releaseは https://github.com/yuuki-ama1015/obs-quick-record/releases 。この更新を同封した版はv0.1.0-dev-review-fixes-r2。配布ZIP、ソースZIP、Git bundle、各SHA-256を提供する。DLL配布ZIPだけでは開発できない。
4. Windowsでビルドする場合はREADMEの手順を使う。Visual Studio 2022のx64 C++/Windows SDK、CMake、Ninja、Gitを用意する。tools/prepare-sdk.ps1が固定版のOBS/Qtを再取得する。GitHub ActionsのWindows buildも利用できる。

会話履歴、ローカルのソース・SDK・QA環境・引き継ぎファイルに依存しない。個人設定、録画、私的メモはGitHub/配布物に含めない。旧QA環境を復元する代わりに、必要な試験環境を新しく作る。

## 決定済みの動作

- OBS Studio 32.2.2、Windows 10/11 x64、OBSとABI互換のQt 6.11系、C++17以上。OBS本体は改造しない。
- 既定キーはAlt+R。Win+Shift+RはSnipping Toolとの競合があるため既定から外した。独立したQuick Record設定から変更でき、OBS終了中の起動アシストにも保存したキーが反映される。
- 通常は対象を選んでEnterで開始し、Escでキャンセル。Enterは選択UIにフォーカスがある間だけ有効。OBS前面時の開始禁止は既定ON、Quick Recordが始めた録画は同じキーで停止できる。
- 範囲・ウィンドウ・モニターをOBS標準ソースで録画する。範囲は1モニター内、座標はWindows物理ピクセル。既存Canvas/Output Resolutionと録画・音声設定を変えない。
- OBS標準Program出力と録画APIを使い、一時シーン/ソースを停止イベントで解放する。外部停止も同じ後処理を通す。独自エンコーダ、obs-websocket必須依存は作らない。
- obs-auto-stopは別プラグインのまま変更・リンク・固有API呼び出しをしない。録画の連携はOBS標準イベントのみ。ユーザー合意の追加導線として、存在するOBS管理の設定ドックを独立表示するボタンがある。未導入の場合はボタンを非表示にする。
- 常駐機能のユーザー向け名称は「OBS起動アシスト」。通知領域から自動起動のON/OFFと確認付きアンインストールができる。採用済みのOBS文字入りカメラアイコンを維持する。
- 起動アシストが新しく起動したOBSだけを所有し、録画完了/選択キャンセル後に正常終了を要求する。既に起動していたOBS、途中でOBS本体を表示した場合、外部録画/配信/Replay/VirtualCam開始時は自動終了しない。設定等の画面表示中は待つ。強制終了しない。
- 録画出力のstop信号で異常/結果不明を検出した場合は、自動終了を解除しOBSを保持して独立通知する。GUIのSTOPPED後処理は信号購読者の実行完了後に行う。
- 一括導入の管理者権限はプラグインコピーだけに限定する。元ユーザーの通常権限で起動アシストを配置・起動する。管理者として開いていないPowerShellから実行する。
- 設定保存に失敗したら旧設定/キーへ戻し、編集画面を維持する。待機時の設定再解析・プロセス探索・全面再描画を抑え、隠した選択画面を解放する。

## 実装の入口

- src/quick-record-controller.cpp：状態機械、録画イベント、起動要求、自動終了と通知。
- src/launcher-session.hpp、src/launcher-request.hpp：OBS所有権と一度だけ消費するPID付き起動要求。
- src/capture-controller.cpp：標準ソース、一時シーン、準備、停止後の復旧。
- src/quick-record-overlay.cpp、各selector：物理座標での選択、ハイライト、Qt画面。
- src/settings-window.cpp、src/hotkey-manager.cpp、src/settings.cpp：独立設定、OBSホットキー、保存。
- launcher/Launcher.cs、launcher/setup.ps1、launcher/install.ps1：通常権限の起動アシストと導入。

Source ID/setting keyを変更する場合は、tools/prepare-sdk.ps1が取得するOBS 32.2.2ソースで根拠を再確認する。推測でキーを書かない。Qt GUIはGUIスレッドで操作し、出力信号、Frontendイベント、ホットキーの解除とOBS参照カウントを守る。

## 最新の検証と次の作業

レビュー修正は4d1b3d1、8b06346、de9b56f、PC導入記録はd63276a。MSVCビルドとCTest全8件に成功（65.69秒）。mainとタグのWindows CI 37201755411/37201866343も成功。録画結果の異常/不明、信号通知順、一時busy所有権、設定保存失敗、キャッシュ、描画停止/破棄、インストール境界を試験している。

PC導入ではDLL/EXE/日英locale一致、設定保持、起動アシストの非昇格トークン、自動起動参照先、Alt+R登録を確認。20秒の単発測定では待機CPU時間が0.140625→0.046875秒、WorkingSet40.37→43.61MiB、Private33.95→31.82MiB。性能の改善率やメモリ全体の削減を保証しない。

次は以下の未確認条件から、実際に利用できる環境のものを試験する。

1. 最新版でOBS終了中から実キーAlt+R→選択→Enter→手動停止/Auto Stop停止→OBS正常終了→再Alt+Rの一連を確認する。
2. 起動アシスト所有の最小化OBSで、録画中の実出力障害を起こし、通知を閉じてもOBSが終了しないこと、後処理が済むことを確認する。GPU本体の故障と出力補助プロセスの障害を区別する。
3. 別管理者資格情報のUAC、新規Windows/別ユーザーで、起動アシストとOBSが元ユーザーの通常権限で動き、正しいStartupへ登録されることを確認する。
4. 200%、異種DPI、複数モニター、Windows 10、他のOBS 32.x、他キャプチャ方式のカーソル/REC除外を確認する。過去の125%/150%等の実機結果はdocs/testing.mdにある。

Frontend/Captureを置換したテストの合格を実機受入へ読み替えない。Linux等のクラウド環境ではソース確認はできても、Windows DLL/GUI/DPI試験ができたとは報告しない。ユーザーの通常録画/シーン設定は試験のために書き換えず、公式OBSのポータブルコピーに独立QAプロファイルを作る。4色画面はビルドしたcapture-target.exeで作れる。Auto Stopを使う試験は公開リポジトリの独立配布を別途導入する。

観測した問題を小さいコミットで修正し、docs/testing.mdへ条件・結果・未確認を記録する。ソースとReleaseを公開し、公開assetsを再取得してハッシュ一致を確認してから、承認されたローカル作業ファイルを整理する。インストール済み本体、必要な設定と自動起動登録は保持する。
# インストーラー更新（2026-10-05）

配布物の一括導入は同名の `.cmd` をダブルクリックする入口を使用します。Windows全体の実行ポリシーを変更せず、Windows PowerShellを `-NoProfile -ExecutionPolicy Bypass` で起動します。`.ps1` は成功・失敗の表示をEnterまで保持し、内部呼び出しでは `-NoPause` を指定します。管理者子プロセスはプラグインのコピーだけを行い、待機しません。実行ポリシーがスクリプト開始前に拒否した場合は `.ps1` 自身では対処できません。`tests/installer-check.ps1` は日本語・空白を含む展開先とRestrictedのプロセスポリシーで、配布入口のエラー表示・終了コードを検証します。

