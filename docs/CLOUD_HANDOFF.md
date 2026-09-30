# obs-quick-record 引き継ぎ書

更新日: 2026-09-30。これは **0.1.0 開発版** の引き継ぎです。MVPの全受入完了、GitHub公開、Release公開を意味しません。

## クラウド側で作業を始める方法

1. この文書を含む **ソースZIP** をクラウドCodexの作業に添付して展開する。Git履歴を引き継げる環境では、併せて作成した **Git bundle** からcloneする。DLL配布ZIPにはソースが入っていないため、コードの引き継ぎには使わない。
2. 展開後、`README.md`、`docs/design.md`、`docs/testing.md`、この文書を読む。実装の主な入口は `src/plugin-main.cpp` と `src/quick-record-controller.cpp`。
3. 展開したソースは、引き継ぎ前の検証済みコミット `49842b7` にこの文書を追加した状態。ソースZIPはGitのコミットから作成しており、未追跡のQA用OBS、録画、SDK、ビルド成果物は含まない。Git履歴はZIPに含まれず、Git bundleには含まれる。
4. 現時点でローカルリポジトリには **Git remoteが設定されていない**。クラウド環境がこのPCのローカルパスを直接参照できるとは想定しない。GitHubで継続する場合は、ユーザーが指定したリポジトリと公開範囲を確認し、ソース・履歴を移す。

ローカルの引き継ぎ用ファイルはソースリポジトリの**親**ディレクトリに置く。`obs-quick-record-source-handoff-2026-09-30.zip` はソース一式、`obs-quick-record-handoff-2026-09-30.bundle` はGit履歴。DLL配布候補 `obs-quick-record-0.1.0-dev-windows-x64-qa-2026-09-30-r2.zip` は別物。GitHub Actionsは `.github/workflows/build-windows.yml` に用意済みだが、GitHub上での実行は未確認。

## 目的と守るべき境界

- Windows 10/11 x64向けの独立したOBS Studio 32.2.2プラグイン。OBSを最小化したまま選択画面を出し、範囲・ウィンドウ・モニターを選択してOBS標準の録画を開始する。
- OBS Studio本体と `yuuki-ama1015/obs-auto-stop` は変更しない。Auto Stopとの連携はOBSの録画開始・停止イベントのみ。obs-auto-stopのライブラリや固有APIへ依存しない。
- このPCでは `Win+Shift+R` がWindows 11のSnipping Tool録画と競合したため、**ユーザー合意の既定キーはAlt+R**。Quick Record独立設定画面から変更できる。Enterは選択画面にフォーカスがある間だけ有効。既定の開始方式はEnter確認、OBS前面での開始禁止はON。録画中の同じキーによる停止は許す。
- OBSのProgram出力、録画設定、Canvas解像度を使う。選択範囲は既存Canvas全体へ引き伸ばす。MVPの範囲録画は1モニター内のみ。
- 一時シーン `__obs_quick_record_internal__` と一時ソース `__obs_quick_record_capture__` を使い、録画停止イベントで元のシーンへ戻して解放する。設計根拠とOBS 32.2.2のSource ID・設定キーは `docs/design.md` を参照。
- 開発はGitで記録する。ユーザーから「GitHubに反映」の指示がある場合、それにはソースの反映だけでなくZIP配布用Releaseも含む。現時点ではその指示はなく、GitHubへのpushやReleaseは行っていない。
- 最上位モデルで作業する場合、ユーザー指示によりサブエージェントは併用しない。

## ここまでの実装と検証

- `src/` は状態管理、Hotkey、Capture、Overlay、各Selector、独立設定画面、録画表示へ分割済み。日英ロケールとGPL-2.0を同梱。
- Windows MSVC 14.44 / Qt 6.11.1 / OBS 32.2.2でビルド済み。`geometry-check`、`hotkey-check`、`controller-check`、`overlay-check` の4件が成功。特に状態管理テストはカウントダウン中のキャンセル、開始失敗とタイムアウト、外部停止、終了時のタイマー解除を確認した。ただし録画・Frontend・選択画面の一部境界はテスト用実装へ置換している。
- 分離したOBS 32.2.2ポータブル環境で、OBS最小化からの実キーAlt+Rによる範囲録画、ウィンドウ録画、モニター録画、Auto Stopによる停止、手動停止、元シーン復帰、一時シーン削除、OBS通常終了時のリーク0件を確認。Studio Modeではシーン複製ON・プロパティ複製OFFで範囲とウィンドウの出力映像を確認。証拠・条件は `docs/testing.md` に記録している。
- 配布用DLLのSHA-256（直近の実録画テストで使ったDLLと同一）: `4DEE947580B1170CE238601CB6A2C2F50B55B18725905745758875FA05A5DE23`。ビルド設定とテスト後、配布ZIPは全7ファイルを展開元と照合済み。
- 引き継ぎ直前、分離QA用OBSは通常終了。QA設定はAlt+R、Enter確認、OBS前面時の開始禁止ON、Auto Stop静止5秒、Studio Mode OFFへ復元済み。最後のターンではカウントダウン試験の準備としてOBSを起動したが、ユーザーの引き継ぎ依頼により対象選択・録画試験は実施せず終了した。

## 次に進める作業

1. Windows実機上の分離OBSで、カウントダウン途中のEsc・再選択・設定表示、明示的な即時開始を確認。自動テスト成功と実機受入を混同しない。
2. ウィンドウ選択後・Enter前に対象を閉じた場合、録画開始失敗の後処理を確認。
3. 選択中、キャプチャ準備中、独立設定画面表示中のOBS通常終了を確認。
4. Studio Modeのシーン複製OFFでも範囲・ウィンドウ録画と復元を確認。
5. 別の機材が使える場合は異種DPI・複数モニター、Windows 10、他のOBS 32.xを検証。これらの環境を持たないクラウド実行だけで実機合格と判定しない。
6. 観測した不具合のみを最小限修正し、`docs/testing.md` に検証条件と結果を追記する。ビルドと必要なテストを実行し、開発版ZIPを作り直す。

ビルド手順は `README.md` と `tools/prepare-sdk.ps1` を参照。Windows/MSVC/Qt環境を持たないクラウド作業では、ソース確認やドキュメント更新はできてもDLLビルド・OBS GUI・DPIの実機受入は完了できない。クラウド側で何を検証できたかを分けて報告する。
