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

All GUI and frontend changes run on the Qt GUI thread. Hotkeys queue context-bound calls. Unregister callbacks/hotkeys before deleting their targets. Release capture only after STOPPED or OBS shutdown.

Windows physical desktop pixels are the selection boundary contract. Each monitor gets a separate Qt overlay; convert physical offsets to local logical paint coordinates using that monitor's size ratio, never multiply virtual desktop coordinates by one global DPI factor.

On Windows, `QScreen::name()` is a user-facing monitor name and may differ from Win32 `\\.\DISPLAYn`. Match Qt screens to physical Win32 monitors by their unchanged top-left screen position; Qt scales screen sizes but preserves those positions under mixed DPI. If no monitor can be mapped, selection cannot start.

Restore OFF cannot retain a live temporary capture (contradicts mandatory cleanup). It means preserve another regular scene if the user switched during recording; if Quick Record is still Program, return to the saved scene before removing it. This safety fallback is explained in Settings.

When restoring Program in Studio Mode, finish the transition before removing the temporary scene, then reapply the unchanged Preview using the frontend API. In OBS 32.2.2 with scene duplication OFF, pre-setting the transition to the restored source suppresses `transition_video_stop`; `TransitionFullyStopped()` does not refresh Program labels. `SetCurrentScene()` invoked by the preview API refreshes those labels. Starting a new restore transition instead can swap the removed temporary scene into Preview when swap mode is ON.

Win+Shift+R conflicts with Windows Snipping Tool on the QA PC. The default is Alt+R. The independent Quick Record Settings window edits only this plugin's select/stop hotkey through the OBS Hotkey API and persists its binding array; OBS Settings / Hotkeys remains another route. OBS hotkeys cannot reserve Windows shortcuts. Do not override Windows registrations. Existing saved hotkeys take precedence over the default.

## Phases

1. Module, hotkey, event state machine; current-scene capture build checkpoint.
2. Independent overlay/settings, foreground safety, local Enter/Escape.
3. Monitor capture, scene transaction, asynchronous readiness.
4. Physical-pixel region crop and mixed-DPI overlays.
5. Window picking/filtering and native Window Capture.
6. Indicator, persistence, lifecycle checks and coexistence validation.
