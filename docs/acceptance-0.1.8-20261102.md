# 0.1.8 desktop acceptance — 2026-11-02

Environment: Windows 11 25H2 x64, OBS 32.2.2, NVIDIA RTX 3080, one 1920×1080 monitor at 96 DPI (100%). A separate portable runtime, profile, scene collection and test-only acceptance-driver module were used. The installed Quick Record DLL and locale files were copied into that runtime. No production profile was edited. Auto Stop was copied independently into the QA runtime, without changing either plugin's code or adding a dependency.

## Completed

| Case | Evidence |
| --- | --- |
| Hidden OBS | Probe reported main visible=0 while selector and recording were active. |
| Escape | Selector closed without starting recording. |
| Region confirmation | A 1400×720 physical rectangle was selected; probe still reported recording=false after mouse release. Enter then started recording. |
| Region capture | Standard monitor_capture with crop/scale produced all four fixture colours filling the output. Full decode passed. This first QA session used OBS defaults of 1280×720/30; later sessions explicitly used 1920×1080/60. |
| Manual stop path | Invoking the registered OBS hotkey callback stopped recording, restored scene QA and removed the reserved temporary scene. This is callback coverage, not physical-key delivery. |
| Window capture | The fixture window was highlighted and selected, Enter started standard Window Capture, and its synthetic content filled the output. 1920×1080/60 H.264 recording fully decoded. |
| External stop | Test module invoked standard Frontend recording_stop; Quick Record received STOPPED, restored QA and removed the temporary scene. |
| Monitor capture | Selected the single 1920×1080 monitor, confirmed with Enter, recorded the fixture via standard monitor_capture. Full decode passed at 1920×1080/60. The sampled frame showed no REC indicator. |
| Auto Stop coexistence | Official public Auto Stop source was checked for OBS_AUTOSTOP_MAX_SECONDS. Setting it to 5 only in the QA process produced the Auto Stop log: max recording duration reached, elapsed 5 s. No direct plugin API was called. |
| Owned-session automatic exit | A PID-specific exit-after-capture request was consumed through the directory watcher. After Auto Stop stopped the monitor recording, Quick Record restored QA, removed the temporary scene, unloaded and OBS exited. Shutdown log reported memory leaks: 0. Assistant log then registered Alt+R again. |
| Playback | All three normal synthetic recordings passed complete FFmpeg decode, exit 0. Extracted region/window/monitor frames were visually inspected. No videos or desktop screenshots are committed or distributed. |

Setup observations: the initial fixture executable lacked adjacent Qt DLLs; placing it beside the copied OBS runtime resolved startup. The initial profile copy was not the normalized active profile directory; after OBS generated Quick_Record_QA, explicit output settings were written there while OBS was closed. The first synthetic recording used the default Videos directory and was then moved into QA storage. These are fixture setup corrections, not product defects.

## Remaining boundaries

Physical Alt+R launch followed by Escape and automatic exit has been requested from the user; it is not yet counted as passed. Actual mixed DPI/additional monitors, other Windows/OBS versions, another Windows user/admin account, re-login, window movement/resizing/occlusion, and real output-error retention have not been retested for this version. The Auto Stop case above covers its maximum-duration stop, not stillness/silence/media detection. No general CPU or memory improvement percentage was measured. Earlier automated and acceptance evidence remains documented separately.
