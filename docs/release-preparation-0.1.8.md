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

The attempted main update was rejected by automatic approval review because its authorization was judged limited to the work branch. Explicit approval for main, Release and installed-PC updates has been requested. None of those updates has completed. Do not treat these preparation results as publication or installation evidence.
