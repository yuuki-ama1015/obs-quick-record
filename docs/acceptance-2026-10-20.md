# 2026-10-20 Windows acceptance results

## Environment and isolation

Windows 11 x64, OBS Studio 32.2.2, RTX 3060, one 1920 x 1080 monitor at 100% Windows scaling. Tests used a dedicated portable copy of OBS, a separate profile/scene collection and synthetic four-colour targets. Desktop and microphone audio sources were disabled in that profile. Installed OBS settings and recordings were not used as test data.

Most recordings tested the released 0.1.5 code. The automatic-exit regression was then fixed in 71512ae and retested in the same isolated environment. The new probe in tests/acceptance-driver.cpp is an optional test module, refuses loading outside an explicitly configured QA directory and is excluded from installation and ZIP packages. Its polling overhead must not be attributed to the product.

## Automated checks

- Windows build [39399982642](https://github.com/yuuki-ama1015/obs-quick-record/actions/runs/39399982642): build, all nine CTest checks and package allowlist passed for 71512ae.
- Native assistant [39399982640](https://github.com/yuuki-ama1015/obs-quick-record/actions/runs/39399982640): passed for the same commit.
- The installed native assistant self-test and installer-check.ps1 passed on this PC.
- The Qt settings test passed at QT_SCALE_FACTOR 1, 1.25, 1.5 and 2. These are simulated Qt scaling checks, not acceptance at those Windows display settings or on mixed-DPI monitors.
- Regression tests now cover hidden-owner activation preserving automatic exit, visible main-window activation retaining OBS, and foreground safety with hidden/minimized/unrelated windows.

## Real OBS results

| Operation | Result and boundary |
| --- | --- |
| Region selection | Releasing the drag did not start recording; Enter started it. Selected physical pixels filled the existing 1920 x 1080 canvas. |
| Window capture | Standard Window Capture selected the target, used automatic/BitBlt for the fixture, and followed a moved window. |
| Monitor capture | Standard Monitor Capture recorded the single display. |
| Manual stop | Invoking the registered OBS hotkey callback stopped the recording. Stopping while the OBS main window was foreground also succeeded. This is not a physical-key delivery test. |
| Foreground safety | Showing and activating the test OBS main window prevented the start callback from opening a selector. |
| External stop | OBS Frontend stop and Auto Stop's five-second stillness condition both reached Quick Record cleanup. |
| Cleanup | Previous scene restored; temporary sources/scenes removed. Normal shutdown logs in completed cases reported plugin unload and zero OBS memory leaks. |
| Capacity mode | NVENC recorded 1280 x 720 with approximately 1/30-second packet intervals. Turning it off in the same session restored 1920 x 1080 / 60 fps. The OBS MKV metadata limitation described in README remains. |
| Independent settings | Quick Record settings and the Auto Stop settings entry opened while the main OBS window stayed hidden. |
| Output error | Terminating only the dedicated OBS mux helper produced output error -8, restored the previous scene, removed temporary sources, showed a notice and retained OBS. The deliberately damaged file was not decodable and is not counted as a normal recording. This does not simulate a GPU failure. |
| Encoder start failure | P010 with x264 failed as expected; the start timeout cleaned up and displayed an error without creating a recording. OBS was retained until explicitly closed. |
| Playback integrity | All eight normal test recordings fully decoded with FFmpeg (exit code 0). No private videos or screenshots are committed or packaged. |

## Automatic-exit bug and fix

The Qt main window received WindowActivate while it was hidden. LauncherSession treated that as the user restoring OBS and relinquished automatic-exit ownership. Foreground safety could also mistake the hidden owner of an independent tool for the visible OBS main window.

The fix only treats main-window activation as user restoration when the main window is visible. Start safety additionally requires a visible, non-minimized OBS main window. The existing Show handling and the ability to stop a Quick Record recording while OBS is foreground remain.

After the fix, three independent owned sessions passed: cancel selection, manual stop, and Auto Stop stop. Each restored/cleaned up as applicable and exited OBS normally. The manual and Auto Stop recordings fully decoded; shutdown logs reported zero OBS memory leaks.

## Incomplete acceptance conditions

- A Qt test target's maximized appearance retained stale content, also visible on the desktop. Window movement passed, but resizing content was not accepted as correct. The follow-up Chromium test was interrupted by the user before recording; resizing, occlusion and Windows Graphics Capture require a separate test.
- Physical Alt+R visibility was reported by the user, but a selector request was already pending, so the complete physical-key-to-launch path was not isolated. Callback tests do not establish OS key delivery.
- Actual Windows 125%, 150%, 200%, mixed DPI, additional monitors, Windows 10, other OBS 32.x versions, GPUs and capture methods remain unverified in this run.
- Re-login with the native assistant, clean-user installation and UAC with another administrator account were not performed in this run. Direct Startup-link inspection is not a re-login test.
- The candidate's owned-session retention after a real output error was not separately retested after the ownership fix; the earlier error case and automated retention checks passed.

The installed assistant used 14.07 MiB working set and 1.91 MiB private bytes in one 30-second idle observation, with CPU time delta 0 ms. This single observation is not a before/after benchmark. The separately recorded three-run isolated benchmark in docs/testing.md remains the basis for the earlier memory comparison; no recording-speed improvement percentage was measured.
