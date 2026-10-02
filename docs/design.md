# API audit and decisions

Audited OBS 32.2.2 commit `ba2f32bdf791005443988a4955e963663e16b1ed` and obs-auto-stop commit `9dc96f981e9de7b6ddd90967a6623f1c7de5a3f8` before implementation.

- [window-capture.c](https://github.com/obsproject/obs-studio/blob/32.2.2/plugins/win-capture/window-capture.c): `window_capture`; `window`, `method` (0 Auto, 1 BitBlt, 2 WGC), `cursor`, `client_area`, `priority`.
- [window-helpers.c](https://github.com/obsproject/obs-studio/blob/32.2.2/libobs/util/windows/window-helpers.c): window value is escaped title:class:exe. Escape # as #22 first, then : as #3A. Only accept values present in the source's own property list.
- [duplicator-monitor-capture.c](https://github.com/obsproject/obs-studio/blob/32.2.2/plugins/win-capture/duplicator-monitor-capture.c): `monitor_capture`; `monitor_id` is the EnumDisplayDevices DeviceID (EDD_GET_DEVICE_INTERFACE_NAME), `method` (0 Auto, 1 DXGI, 2 WGC), `capture_cursor`. Validate ID against OBS property list.
- [OBSStudioAPI.cpp](https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/OBSStudioAPI.cpp), [OBSBasic_Transitions.cpp](https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/widgets/OBSBasic_Transitions.cpp): get_current_scene returns Program in Studio Mode. set_current_scene initiates a transition, not an immediate cut. Wait for transition completion and nonzero capture dimensions before recording.
- Private scene is supported by libobs, but SetCurrentScene updates the normal frontend currentScene only when found in its scene list. Use one marked reserved regular scene plus a private capture source; remove owned scene after stop. Never delete an unmarked name collision.
- Studio Mode may copy scene items on transition. Warm the source with a balanced inc/dec_showing pair and apply crop before switching, then wait for the actual transition target's capture dimensions. Waiting only on the original source misses a separately copied Window Capture instance.
- [obs-auto-stop plugin-main.cpp](https://github.com/yuuki-ama1015/obs-auto-stop/blob/9dc96f981e9de7b6ddd90967a6623f1c7de5a3f8/src/plugin-main.cpp): starts sensors on RECORDING_STARTED, resets on RECORDING_STOPPED. No plugin-specific integration is needed. Its source is reference-only, never linked or edited.

## Constraints

Canvas/output/encoders/audio devices are unchanged. Program scene switching necessarily affects streaming, virtual camera and replay buffer; reject new Quick Record sessions while those outputs are active. Audio follows OBS global audio settings; sources exclusive to the previous scene become inactive. Scene filters on the previous scene do not transfer to the temporary scene.

Recheck other output activity on each capture preparation timer tick, before advancing readiness or requesting recording. If another output starts while capture is warming or a transition is pending, cancel preparation, restore/clean up the temporary capture, and leave the other output running. This is a GUI-thread state check, not an atomic reservation of OBS output ownership.

All GUI and frontend changes run on the Qt GUI thread. Hotkeys queue context-bound calls. Unregister callbacks/hotkeys before deleting their targets. Ignore frontend events delivered after shutdown has begun: unregistration does not discard calls already queued to Qt. OBS 32.2.2 destroys its frontend API after SCRIPTING_SHUTDOWN and EXIT, before module unload. Release capture only after STOPPED or OBS shutdown.

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

