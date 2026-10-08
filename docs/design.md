# API audit and decisions

Audited OBS 32.2.2 commit `ba2f32bdf791005443988a4955e963663e16b1ed` and obs-auto-stop commit `9dc96f981e9de7b6ddd90967a6623f1c7de5a3f8` before implementation.

- [window-capture.c](https://github.com/obsproject/obs-studio/blob/32.2.2/plugins/win-capture/window-capture.c): `window_capture`; `window`, `method` (0 Auto, 1 BitBlt, 2 WGC), `cursor`, `client_area`, `priority`.
- [window-helpers.c](https://github.com/obsproject/obs-studio/blob/32.2.2/libobs/util/windows/window-helpers.c): window value is escaped title:class:exe. Escape # as #22 first, then : as #3A. Only accept values present in the source's own property list.
- `WINDOW_PRIORITY_TITLE` matches a title without requiring class/executable equality. Before preparation, reject multiple matching titles in the Window Capture property list, including case differences and different applications. Use libobs's `ms_find_window` and `ms_find_window_top_level` to verify both Auto capture search paths resolve to the selected HWND; recheck while warming and before start. Verify acquired title/class/executable through Window Capture's `get_hooked` procedure on both the original source and the Studio Mode Program copy. The standard source offers no setting to pin an HWND; these checks do not atomically lock Windows window creation or z-order changes, or change OBS's behavior after a recording has started.
- [duplicator-monitor-capture.c](https://github.com/obsproject/obs-studio/blob/32.2.2/plugins/win-capture/duplicator-monitor-capture.c): `monitor_capture`; `monitor_id` is the EnumDisplayDevices DeviceID (EDD_GET_DEVICE_INTERFACE_NAME), `method` (0 Auto, 1 DXGI, 2 WGC), `capture_cursor`. Validate ID against OBS property list.
- [OBSStudioAPI.cpp](https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/OBSStudioAPI.cpp), [OBSBasic_Transitions.cpp](https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/widgets/OBSBasic_Transitions.cpp): get_current_scene returns Program in Studio Mode. set_current_scene initiates a transition, not an immediate cut. Wait for transition completion and nonzero capture dimensions before recording.
- Private scene is supported by libobs, but SetCurrentScene updates the normal frontend currentScene only when found in its scene list. Use one marked reserved regular scene plus a private capture source; remove owned scene after stop. Never delete an unmarked name collision.
- Studio Mode may copy scene items on transition. Warm the source with a balanced inc/dec_showing pair and apply crop before switching, then wait for the actual transition target's capture dimensions. Waiting only on the original source misses a separately copied Window Capture instance.
- [obs-auto-stop plugin-main.cpp](https://github.com/yuuki-ama1015/obs-auto-stop/blob/9dc96f981e9de7b6ddd90967a6623f1c7de5a3f8/src/plugin-main.cpp): starts sensors on RECORDING_STARTED, resets on RECORDING_STOPPED. No plugin-specific integration is needed. Its source is reference-only, never linked or edited.

## Constraints

By default, canvas/output/encoders/audio devices are unchanged. The optional capacity mode described below temporarily limits the recording encoder only. Program scene switching necessarily affects streaming, virtual camera and replay buffer; reject new Quick Record sessions while those outputs are active. Audio follows OBS global audio settings; sources exclusive to the previous scene become inactive. Scene filters on the previous scene do not transfer to the temporary scene.

Recheck other output activity on each capture preparation timer tick, before advancing readiness or requesting recording. If another output starts while capture is warming or a transition is pending, cancel preparation, restore/clean up the temporary capture, and leave the other output running. This is a GUI-thread state check, not an atomic reservation of OBS output ownership.

All GUI and frontend changes run on the Qt GUI thread. Hotkeys queue context-bound calls. Unregister callbacks/hotkeys before deleting their targets. Ignore frontend events delivered after shutdown has begun: unregistration does not discard calls already queued to Qt. OBS 32.2.2 destroys its frontend API after SCRIPTING_SHUTDOWN and EXIT, before module unload. Release capture only after STOPPED or OBS shutdown.

Hold a separate scene-item reference while preparing capture. A scene can survive external removal of its items, so scene lifetime alone does not protect a borrowed item pointer. Treat item detachment or source/scene removal as failed readiness and immediately cancel preparation. Balance the extra item reference during cleanup. Closing any selection surface through WM_CLOSE/Alt+F4 cancels the entire overlay and the controller's countdown, just like Esc.

Windows physical desktop pixels are the selection boundary contract. Each monitor gets a separate Qt overlay; convert physical offsets to local logical paint coordinates using that monitor's size ratio, never multiply virtual desktop coordinates by one global DPI factor.

On Windows, `QScreen::name()` is a user-facing monitor name and may differ from Win32 `\\.\DISPLAYn`. Match Qt screens to physical Win32 monitors by their unchanged top-left screen position; Qt scales screen sizes but preserves those positions under mixed DPI. If no monitor can be mapped, selection cannot start.

Restore OFF cannot retain a live temporary capture (contradicts mandatory cleanup). It means preserve another regular scene if the user switched during recording; if Quick Record is still Program, return to the saved scene before removing it. This safety fallback is explained in Settings.

When restoring Program in Studio Mode, finish the transition before removing the temporary scene, then reapply the unchanged Preview using the frontend API. In OBS 32.2.2 with scene duplication OFF, pre-setting the transition to the restored source suppresses `transition_video_stop`; `TransitionFullyStopped()` does not refresh Program labels. `SetCurrentScene()` invoked by the preview API refreshes those labels. Starting a new restore transition instead can swap the removed temporary scene into Preview when swap mode is ON.

OBS 32.2.2 `OBSBasic::closeWindow()` saves the collection before `SCRIPTING_SHUTDOWN`. Thus a recording interrupted by normal shutdown or a crash can leave the marked temporary scene in the saved collection even after runtime cleanup. Store the previous user scene's UUID in the owned scene's settings. On loading a stale owned scene, restore that scene (or the unchanged Studio Preview / another user scene for older data) before removal if Program still refers to Quick Record or is empty. Preserve an already-selected user Program and any unmarked name collision. This uses standard frontend APIs and does not suppress collection saving while recording. It cannot recover a previous scene the user deleted, and does not repair interrupted recording files. Runtime resources are released at normal shutdown; a persisted stale scene is removed on the next load.

Win+Shift+R conflicts with Windows Snipping Tool on the QA PC. The default is Alt+R. The independent Quick Record Settings window edits only this plugin's select/stop hotkey through the OBS Hotkey API and persists its binding array; OBS Settings / Hotkeys remains another route. OBS hotkeys cannot reserve Windows shortcuts. Do not override Windows registrations. Existing saved hotkeys take precedence over the default.

## Phases

1. Module, hotkey, event state machine; current-scene capture build checkpoint.
2. Independent overlay/settings, foreground safety, local Enter/Escape.
3. Monitor capture, scene transaction, asynchronous readiness.
4. Physical-pixel region crop and mixed-DPI overlays.
5. Window picking/filtering and native Window Capture.
6. Indicator, persistence, lifecycle checks and coexistence validation.

### OBS録画エラー通知とSTOPPEDの順序（2026-10-02〜03実機確認）

OBS 32.2.2の`frontend/widgets/OBSBasic_Recording.cpp`の`RecordingStop`は、録画エラーのモーダル通知を閉じた後で`OBS_FRONTEND_EVENT_RECORDING_STOPPED`を送る。Quick Recordはこの標準イベントを受けてcleanupするため、通知が開いている間は一時シーンとREC表示が残る。録画中のQA muxer障害で実際のOBS_OUTPUT_ENCODE_ERRORと、この順序を確認した。通知を閉じた後の復帰・削除は9ms、正常終了時のリーク0件。OBS本体や他プラグインの通知を操作する仕組み、独自の停止検出は追加せず、標準イベントの順序を維持する。
# 任意Launcher（2026-10-03追加）

OBS終了中の呼び出しは外部の.NET Framework/Win32 Launcherで受ける。OBSの同一Windowsセッションのプロセスが存在しない間だけRegisterHotKeyを登録し、存在中は解除する。単一起動はユーザーSIDとセッションの名前付きMutexで制限する。起動は標準配置の通常版OBSに--minimize-to-trayを渡す。プラグイン本体は引き続きOBS正式Hotkey APIを使用する。

LauncherはPlugin Config Pathのlaunch-request.txtへ起動したOBS PIDだけを書く。プラグインはFINISHED_LOADING後のGUIタイマーで、対象PID一致・ファイルのサイズと60秒期限を確認し、一度だけ削除して通常toggleを呼ぶ。終了イベントでタイマーを停止し、終了後の呼び出しは無視する。要求は録画開始や対象座標を受け付けず、Idle時の選択画面表示に限る。前面安全設定と出力競合の判定を維持する。読み込み失敗・復旧確認などで60秒を超えればLauncherが取消・通知する。

OBSのobs_key_to_virtual_key（32.2.2のobs-hotkey.hで確認）で主要bindingのWindows仮想キーと修飾キーをsettings.jsonへ保存し、OBS終了後のLauncherが同じキーを使う。主binding以外の追加binding、マウスキー、ポータブル/カスタム配置の起動振り分けは対象外。OBS起動中のbindingは既存実装のまま。

## Auto Stop設定への任意のUI導線

ユーザーの追加指示により、設定画面だけは既存Auto Stop UIを開く任意の連携を許可した。Auto Stopのplugin-main.cppで登録されたQDockWidgetのobjectName `obs-auto-stop-dock` を、OBS標準Frontend APIで取得したmain widget配下から探す。QtのsetFloatingと通常Window属性で独立表示する。Auto Stopコードの変更・リンク・固有API呼び出し・設定ファイルの直接編集は行わない。録画制御は従来通り標準イベントだけを利用する。

対応ドックが存在しない場合はボタンを非表示にし、QPointerによりドック削除後のクリックも安全に扱う。ドックの所有権はOBSに残す。既存ドック配置はフロート表示に変わる。将来Auto Stopが登録IDを変更した場合は、この任意のUI導線のみ更新が必要。

## Optional recording size / frame rate limit (2026-10-08)

User-approved extension: add an opt-in maximum 720p / 30 fps mode to Quick Record. Keep the global canvas, video output, audio, encoder selection and saved OBS profile unchanged. Scale the existing recording encoder only, preserving the global output aspect ratio without upscaling. Round down to even pixels; use an integer frame divisor (50 → 25, 60000/1001 → 30000/1001). Restore scale mode and frame divisor at STOPPED, failed start, or shutdown. A briefly active/initialized encoder delays restoration until it is idle. Changes are in memory only; there is no profile recovery file after a crash.

Audited official OBS 32.2.2, commit `ba2f32bdf791005443988a4955e963663e16b1ed`:

- [SimpleOutput.cpp](https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/utility/SimpleOutput.cpp): `LoadRecordingPreset_Lossy` creates `simple_video_recording`. `Output/Mode=Simple` and `SimpleOutput/RecQuality=Small` or `HQ` use this dedicated encoder. `UpdateRecording` rewrites codec quality settings, so a pre-start CRF/CQP override would be lost. `SetupOutputs` sets encoder video but preserves encoder scale and frame divisor. Stream and Lossless have different ownership/outputs and are excluded.
- [AdvancedOutput.cpp](https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/utility/AdvancedOutput.cpp): recording setup reapplies profile scaling. Advanced mode is intentionally unsupported instead of temporarily rewriting persistent configuration.
- [obs-encoder.c](https://github.com/obsproject/obs-studio/blob/32.2.2/libobs/obs-encoder.c): `obs_encoder_set_frame_rate_divisor` returns false for active/initialized encoders. Check it before the void scale setters. `obs_encoder_set_video` preserves those properties. Getters report zero dimensions before video is attached, so derive bounds from `obs_get_video_info`. Reject pre-existing encoder scaling because there is no public raw-size getter for reliable pre-media restoration. Hold an owned encoder reference through cleanup.
- [obs-output.c](https://github.com/obsproject/obs-studio/blob/32.2.2/libobs/obs-output.c): output preferred-size overrides persist and can be reapplied to an attached encoder. Do not use `obs_output_set_preferred_size` because its raw previous value cannot be safely restored.

The feature does not reserve OBS outputs against arbitrary concurrent reconfiguration by other plugins. File-size savings depend on content/codec; unchanged audio remains a fixed contributor. Native OBS recordings after restoration retain their previous dimensions and frame rate. This does not repair selection-to-canvas stretching already present in Quick Record.

Observed with OBS 32.2.2 H.264/MKV: packet timestamps advance by about 1/30 s, but ffprobe r_frame_rate/avg_frame_rate still report the global 60 fps. [obs-ffmpeg-mux.c](https://github.com/obsproject/obs-studio/blob/32.2.2/plugins/obs-ffmpeg/obs-ffmpeg-mux.c), `add_video_encoder_params`, takes metadata fps from `obs_get_video`, not the recording encoder's divisor-adjusted video. Preserve the standard OBS output and document this metadata limitation instead of changing OBS or postprocessing user recordings. Other output formats require their own verification.
