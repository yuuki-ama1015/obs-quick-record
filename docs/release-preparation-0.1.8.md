# 0.1.8 release preparation — 2026-11-01

Candidate: d24e166fa0b9f0a221eff0efb17013675a403ae2 on feat/selection-notification-performance.

- Windows build 41266816587: succeeded, including all 10 CTest checks and package validation.
- Native assistant check 41266816588: succeeded.
- Local candidate native self-test: exit 0 using Start-Process -Wait. An earlier direct invocation was inconclusive and is not counted as a failed product test.
- Local launcher-request-check and overlay-check: exit 0 with the installed OBS runtime, Qt minimal backend, and a workspace-local temporary directory.
- Local package-check: passed; only the expected 10 distribution files are included.
- Distribution ZIP SHA-256: A1583882087909096A63B3411336F3F3EE378AF886509C74A059CC095DA7FD7F.

An isolated runtime was prepared under build-acceptance-20261101. Shell-launched OBS did not expose a usable test window. The computer-use launch operation resolved to the installed OBS despite the supplied isolated executable path. No recording or stream was started. The update notice was dismissed and normal close was requested. This is not successful desktop acceptance; recording, cancellation, automatic exit, physical shortcuts and mixed-DPI acceptance remain unverified for this candidate.

GitHub CLI credentials are unavailable from the restricted execution context even after the user reported successful browser login. Do not repeatedly ask the user to log in or assume their credentials are invalid. The connected GitHub integration can update source but has no Release publication tool.

The initial main update was rejected by automatic approval review because its authorization was judged limited to the work branch. The user subsequently explicitly approved main, Release and installed-PC updates. Completion evidence follows.

## Publication and installation completed — 2026-11-01

- Main was fast-forwarded to 669833a10374417a81cd70bdb4913672d0df237a. The v0.1.8 tag points to that commit; changes after the tested candidate are documentation only.
- Published v0.1.8 as OBS Quick Record with obs-quick-record.zip as its only attachment. Downloaded the public asset again and verified the SHA-256 above. Removed the previous Release entry after verification, retaining Git tags and source history. Only v0.1.8 remains listed.
- GitHub CLI authentication works when the command can access the Windows credential store. No additional user login was necessary after the reported successful login.
- Updated through the packaged setup script, preserving the existing Startup shortcut with -NoStartup. The first installer attempt correctly refused while the assistant was running; it made no installation changes. Verified OBS was stopped, stopped only the assistant whose executable matched the installed path, then completed installation and restarted the assistant.
- Installed plugin DLL, assistant EXE and both locale files match the distribution package (all four SHA-256 comparisons passed).
- All 13 settings files recorded immediately before this update retained their hashes. This is limited to the recorded manifest, not a claim that every OBS file was compared.
- The assistant is running from the normal LocalAppData installation directory. Its log records hotkey registration 1:82 (Alt+R). The existing Startup shortcut points directly to that EXE with no arguments.
- No recording or streaming was started during this verification. Actual keyboard-to-recording acceptance and the other desktop conditions above remain unverified for 0.1.8.
