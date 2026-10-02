# 2026-10-03 このPCへの導入とQA整理

通常版OBSのProgramDataプラグイン配置へ改訂6を導入。DLL SHA-256は実機検証済み0EE163DEBEF88602A48DAE4E9564EFC5337D46D3FA67D1B902CD6B042E3D83CC。通常版ログ2026-10-03 01-35-22.txtの01:35:27.886にplugin loadedとhotkey registeredを確認。通常版のシーン・録画設定はQA設定で置き換えていない。

ワークスペース直下のrecoveryに、QA設定とAuto Stop DLL/ロケールのseed、ログ・画像・試験メタデータZIP、Auto StopソースGit bundle、再構築スクリプト、READMEを保存。固定ハッシュの公式OBS 32.2.2 ZIPと配布改訂6から別フォルダーへ復元し、43設定ファイル、DLL、ロケール、portable marker、録画先を確認。復元QAのGUI再受入は未実施。復元先が既存の場合は上書きしない。ローカルバックアップは公開していない。

残っていた2つの4色テストアプリを終了。プラグインbuild/stage、今回の復元・導入確認用ステージ、重複OBSダウンロード、ルートのテスト用objを削除して残存なしを確認。ソースリポジトリと配布成果物は保持。

work全体約5.5GBの削除は、自動承認レビューがソース・証拠も含む広い範囲への承認が不十分として拒否。workは変更せず保持し、内訳をrecovery/work-inventory.jsonへ保存して、明示的な削除確認待ち。削除済みと扱わない。
