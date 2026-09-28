# 検証記録

更新: 2026-09-28。**MVP受入完了ではありません。** 実装済みであることと、実際の録画で確認できたことを区別しています。

## 確認済み

| 項目 | 結果 |
|---|---|
| OBS 32.2.2 API・Capture設定キー | 固定コミットの実装を参照。根拠はdesign.md |
| Windows x64 DLLビルド | MSVC 14.44、OBS 32.2.2ヘッダー、Qt 6.11.1で成功 |
| geometry-check | CTest成功。矩形の逆方向ドラッグ、負の原点、モニターまたぎ拒否、100/125/150/200%変換、window設定値のエスケープ |
| OBSでの読み込み | 分離したOBS 32.2.2ポータブル環境で成功 |
| obs-auto-stopと同時読み込み | 成功。両プラグインのloadとAuto Stopドック登録をログで確認 |
| OBS Hotkey登録 | OBS設定画面にQuick Recordが表示され、製品既定のWin+Shift+Rが復元されることを確認。試験用Ctrl+Shift+F10も個別に表示を確認 |
| 独立設定画面 | Toolsメニューから表示し、日本語表示・既定のチェック状態・キャンセルを確認 |
| OBS標準Window Capture | 標準ソース設定からQA用ウィンドウを選択し、4色の映像をOBSプレビューと録画フレームで確認。1920×1080出力内でソース外が黒くなることも確認 |
| OBS録画・Auto Stop・停止イベント | OBS標準の録画ボタンで開始し、静止5秒のAuto Stop停止を確認。Quick Recordログにも同時刻の`recording stopped`を確認 |
| 検証動画 | `work/recordings/2026-09-28 20-19-25.mkv`。ffprobeでH.264 1920×1080、AAC、5.366秒を確認。末尾フレームを目視確認 |
| Idle状態でのOBS終了 | 13:40:36起動の試験セッションでmodule unload、正常終了、OBSログのmemory leaks: 0を確認 |

OBS標準Window Captureを使ったテスト映像では、4色の対象ウィンドウを1920×1080の既存Canvasに配置して録画しました。この検証ではソースをOBS UIから手動でシーンに追加し、OBS標準の録画ボタンで開始しています。Quick Recordによるソース準備・シーン切替を通した映像ではありません。

Auto Stopのログでは20:19:25に静止検出を開始し、5秒間の無変化後に録画停止を要求、20:19:31に停止しました。同じ時刻の`OBS Quick Record: recording stopped`ログにより、他経路で始まった録画の標準STOPPEDイベントもQuick Recordが受け取ることを確認しました。この試験だけではQuick Record用一時シーン・ソースの後処理や元シーン復元を確認できていません。

OBS設定でCtrl+Shift+F10へ割り当てた試験用ホットキーをUI自動入力で送りましたが、選択UIは開きませんでした。OBSのWindows側はGetAsyncKeyStateを25ms周期で監視します。短い合成入力が原因かは未確定です。

2026-09-28の実キー試験では、OBSのホットキー設定画面に製品既定のWin+Shift+Rが表示され、Quick Recordの登録ログも確認しました。しかし、この組合せを押すとWindows標準のSnipping Tool録画UIが起動し、Quick Recordの選択UIは開きませんでした。Microsoftの案内でもWin+Shift+RはSnipping Toolの録画ショートカットです（[操作方法](https://support.microsoft.com/en-us/windows/apps/use-snipping-tool-to-capture-screenshots)）。

Windowsで確認できたポリシーは、Win+Shift+RだけでなくWin+EやWin+RなどWinキーのショートカット全体を無効にします（[Microsoft Learn](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-admx-windowsexplorer#admx-windowsexplorer-nowindowshotkeys)）。この広い設定の適用は自動レビューで拒否されたため行っていません。`NoWinKeys`値は未設定で、Win+Shift+RをQuick Recordへ届ける実録画テストは未完了です。

Studio Modeでのクロップ確定順序、実際のProgram側Captureの準備待ち、frontend破棄前のcallback解除を追加修正し、ビルドしました。Quick Recordホットキーから開始した録画、手動停止、Auto Stop後の一時シーンcleanupと元シーン復元、最終修正版の録画中終了確認は未完了です。

CTestは制限された実行環境から起動すると0xc0000135で失敗しました。同じEXEの直接実行、および通常のWindows環境でのCTestは成功しています。テスト時のDLL探索先にはOBS配布物のbin/64bitを使用します。

Quick Recordの選択UIはまだ起動できていません。したがってQuick Record経由の録画映像、外部停止後の復元、録画中の終了安全性は確認済みとして扱いません。

## 手動受入手順

普段の環境と分けたOBSポータブル版を推奨します。OBSの録画設定を先に完成させ、同じOBSにobs-auto-stopを入れます。必要ならキー競合のない組合せをOBSのホットキー設定で指定します。OBSの「ホットキーフォーカスの動作」もバックグラウンドで有効にします。

1. OBS最小化、別アプリ前面でホットキーを押し、選択画面が出ることを確認する。OBS本体は最小化のままであること。
2. 範囲をドラッグし、離しただけでは録画しないことを確認する。選択中と選択後それぞれでEscを試し、キャンセルされることを確認する。
3. 選択画面以外でEnterを押しても開始しないことを確認する。再選択し、選択画面でEnterを押して開始する。
4. 出力ファイルを再生し、選んだ物理ピクセル範囲が既存Canvas全体に引き伸ばされ、選択UIを含まないことを確認する。
5. 同じキーで手動停止し、元のシーンへ戻り、一時シーンが消えることを確認する。次の録画をもう一度開始できること。
6. OBSを前面にしてIdleでキーを押し、開始しないことを確認する。Quick Record録画中はOBSが前面でも同じキーで停止できること。
7. Window Captureで対象を移動・リサイズし、追従と出力を確認する。OBS自身・設定・オーバーレイが選択対象にならないこと。
8. モニター選択で各画面を録画する。複数モニターをまたぐ範囲はエラー表示となり、開始しないこと。
9. 100/125/150/200%それぞれ、および異なるDPI・負の座標を持つモニター配置で境界のピクセルを確認する。geometry-checkだけでは実機検証の代わりにならない。
10. obs-auto-stopの静止検出を有効にして静止対象を録画し、Auto Stopが停止した後、Quick RecordがSTOPPEDを受けて元のシーンとIdleへ戻ることを確認する。
11. OBS停止ボタン、他プラグイン停止、録画開始失敗でも後処理できること。すでに別の録画中なら開始・シーン切替を拒否すること。
12. Studio ModeをONにし、シーン複製設定のON/OFFそれぞれで範囲・ウィンドウ録画と復元を確認する。
13. 3秒カウントダウン中にEsc・再選択・設定表示で開始を取り消せること。即時開始は設定した場合だけ起きること。
14. REC表示ON/OFF、カーソルON/OFF、前回範囲保存、設定とキーの再起動後の復元を確認する。REC表示の除外は使用するキャプチャ方式で映像を確認する。
15. 選択中・準備中・録画中・設定表示中にOBSを終了し、クラッシュや一時ソースの残存がないことを確認する。

ビルド時に生成される `capture-target.exe` は、移動・リサイズと色の境界確認用の静止した4色ウィンドウです。OBSのQt DLLがPATHにある環境で起動します。配布ZIPには含めません。

## 未実行の項目

- Quick Record選択UIから開始する録画・手動停止と後処理、およびWindowsのSnipping Toolと競合しない状態での既定Win+Shift+R実キー確認。
- 実機の異種DPI・複数モニター、Windows 10、OBS 32.2.2以外の32.x。
- GitHub Actions上での実行。ワークフローは追加済みですが、GitHubへの反映はまだ行っていません。

現状の成果物は手動試験用の開発版です。全受入項目が通るまで安定版・MVP完成とはしません。
