# OBS Quick Record

OBSを起動・最小化したまま、範囲・ウィンドウ・モニターを選択して標準のOBS録画を開始するWindows用プラグインです。`obs-auto-stop`とは独立しており、連携にはOBS標準の録画イベントだけを使用します。

現在は **0.1.0 開発版** です。検証済みの範囲と未検証項目は [docs/testing.md](docs/testing.md) を参照してください。

## 動作環境

- Windows 10 / 11 x64、OBS Studio 32.2.2。
- OBSとABI互換のQt 6.11系でビルド。他のOBS 32.xは同梱Qtの互換性も確認してください。
- OBSの録画先・エンコーダ・音声は、先にOBS側で設定します。

## 配置

OBSを終了し、ZIP内の `obs-quick-record` フォルダーを `%ProgramData%\obs-studio\plugins\` にコピーします。

```text
%ProgramData%\obs-studio\plugins\obs-quick-record\
  bin\64bit\obs-quick-record.dll
  data\locale\ja-JP.ini
  data\locale\en-US.ini
```

QtやOBSのDLLをプラグインフォルダーへ追加する必要はありません。OBSログの `OBS Quick Record: plugin loaded` で読み込みを確認できます。

## 使い方

1. OBSを最小化し、録画対象のアプリを前面にします。
2. 既定の `Alt + R` を押します。
3. 「範囲」でドラッグ、または「ウィンドウ」「全画面」で対象をクリックします。
4. 内容を確認して **Enter** で録画します。選択しただけでは開始しません。**Esc** でキャンセルします。
5. 同じホットキーで停止します。OBS本体や別プラグインから停止しても元のシーンへの復元と一時ソースの解放を行います。

Enterは選択画面にフォーカスがあるときだけ有効です。既定ではOBSが前面のときにホットキーで選択画面を開きません。Quick Recordが開始した録画の停止は、OBSが前面でも可能です。既存の別録画は引き継ぎません。

Quick Record独立設定画面のショートカット欄をクリックし、新しいキーを押して保存すると、呼び出し・録画停止キーを変更できます。OBS本体の「設定 → ホットキー → Quick Record」からも変更できます。`Win + Shift + R`はWindows 11のSnipping Tool録画と競合したため、既定値から外しました。Altキーの組合せは前面アプリ固有のメニュー操作と重なる場合があります。

選択画面の設定ボタン、またはOBSの「ツール → Quick Record 設定」から設定できます。設定画面は独立ウィンドウです。即時開始と3秒カウントダウンは明示的に選択した場合のみ有効です。

## 録画と制限

- 出力解像度を変えず、選択範囲を既存Canvas全体に引き伸ばします。縦横比が異なる場合は変形します。
- 範囲録画は1台のモニター内に限定します。モニターをまたぐ選択は開始せず、再選択・キャンセルできます。
- ウィンドウ録画は標準Window Captureの自動方式を利用します。対象アプリや方式によって最小化・遮蔽時の取得に制限があります。
- 音声はOBSのグローバル音声設定を使用します。以前のシーンだけに存在する音声ソースや、そのシーンのフィルターは自動で複製しません。
- Program出力を切り替えるため、配信・リプレイバッファ・仮想カメラが稼働中の場合は開始を拒否します。
- REC表示にはWindowsのキャプチャ除外を使用します。取得方式によって除外できない場合は設定でOFFにしてください。
- 「以前のシーンへ戻す」をOFFにすると録画中に手動で選んだ別シーンを保持します。一時シーンのままの場合は、後処理のため保存したシーンに戻します。
- 前回の範囲は正常に録画開始できた場合に保存します。前回範囲の即時録画ホットキーは未実装です。
- OBSが終了している状態からの起動は対象外です。

専用設定はOBSのPlugin Config Path配下 `obs-quick-record/settings.json` に保存します。通常のWindows環境では `%APPDATA%\obs-studio\plugin_config\obs-quick-record\settings.json` です。ポータブル版ではポータブル設定配下になります。

## ビルド

Visual Studio 2022のx64 C++開発環境、Windows SDK、CMake 3.28以上、Ninja、Gitが必要です。Developer PowerShellで実行します。

```powershell
./tools/prepare-sdk.ps1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl "-DOBS_SOURCE_DIR=$pwd/.deps/obs-studio" "-DOBS_SDK_DIR=$pwd/.deps/sdk" "-DCMAKE_PREFIX_PATH=$pwd/.deps/qt"
cmake --build build
$env:PATH = "$pwd\.deps\runtime\bin\64bit;$env:PATH"
$env:QT_PLUGIN_PATH = "$pwd\.deps\qt\plugins"
ctest --test-dir build --output-on-failure
cmake --install build --prefix stage
```

SDK準備スクリプトはOBS 32.2.2のソース・公式配布DLL・公式Qt SDKを取得し、固定コミットとSHA-256を検証します。OBS本体の改造やビルド、obs-auto-stopの取得は不要です。GitHub Actionsにも同じビルド・チェック・ZIP成果物生成を用意しています。

APIの根拠と仕様上の判断は [docs/design.md](docs/design.md) に記載しています。ライセンスはGPL-2.0です。
