# Native PTube validation — 2026-11-09

Build 6 adds cached battery/AC readings, saved power-mode selection and CPU
clock policies. Portable power-policy tests cover busy-state boosts, playback
floors, invalid mode fallback and menu/playback clock ordering. PSP hardware
tests remain necessary for battery readings, saved settings, overlay layout,
smooth playback in each mode and actual power savings.

This is a hardware-test build, not a claim of complete YouTube compatibility.

Build 4 adds a Discover home-menu entry, non-repeating random topic selection,
result shuffling and SELECT refresh. Portable tests cover topic bounds,
non-repetition, coverage and preservation of all result IDs when shuffling.
The PSP UI and live Discover flow still need a device test. Build 3's search
cancellation, HOME handler and independent operation deadlines are retained.

## Passed checks

- Real Allegrex cross-build of `ptube-app-elf` and `ptube-app-prx`.
- Executable `.text` is checked against the unchanged 4,480,000-byte engine ratchet on every native build. Build 3 remains around 1.34 MB; the exact result is recorded in `native/test-results/psp-build.log`.
- Complete ZIP verification: app title, ELF payload, MEMSIZE=1, CA certificates, font, licences, CRC and every file checksum. No server configuration, API key or user bookmark files are bundled.
- Native frontend: bookmark persistence, Japanese input helper and three saved YouTube search/API fixtures.
- Live native search for `PSP homebrew`: HTTP 200, seventeen parsed results, continuation link available; no Google Data API key.
- Native resolver plus remote MP4 demux: `jNQXAC9IVRw` resolved separate 320x240 video and AAC audio; both remote MP4 sources opened and parsed successfully. This is host network evidence, not playback on PSP hardware.
- The unchanged upstream retained-failure scheduler and web-runtime tests failed before our bounded-overlap fix and passed afterward. Private-network admission also progressed past its original missing-error failure.

## Remaining failures and missing evidence

The final full optimized host run registered 153 tests: **147 passed, three failed, two skipped and one disabled**. The failures are:

- `tilefinch-browser-engine-tests`: cached watch-description invalidation.
- `tilefinch-fetch-redirect-tests`: streaming CORS denial callback/metadata assertion.
- `tilefinch-media-http-range-tests`: partial/readahead streaming statistics assertion.

These broader failures were present in the imported engine before the local correction. They have not been suppressed, skipped, re-baselined or presented as passing. Although the native frontend and live resolver checks pass, remaining engine failures limit release confidence.

The update-root proof and fidelity-floor checks skipped because their external prerequisites/corpus are absent. Device-cost testing is disabled by upstream default. Those are not hardware validation.

Other live attempts (`aqz-KE-bpKQ`, `zKD__94GxWQ`, `jLhv5arvHNs`) failed during the resolver's watch fallback with **blocked private network target** in this environment. Security admission was not disabled. Therefore successful 360p resolution and playback of arbitrary modern videos have **not** been established here.

The user's first physical PSP run booted, loaded its font, joined saved Wi-Fi profile 3, and logged `load url=...results?search_query=thatpinklizard` before hanging. The supplied log contains no video-open event, so it does not establish that a Short caused this search stall.

Build 3 adds the missing browser-owner `fetch_background_transport_cooperate` calls in search/playback loops, allowing the PSP's demoted setup worker and firmware network service to run. It also adds a HOME callback, cancellable search, an independent 45-second search deadline (not refreshed by busy-loop heartbeats), guarded playback teardown, and previous-log retention. Clock-wrap boundary tests for the deadline helper pass. These changes have not yet been verified on physical hardware.

Actual 360p playback, audio synchronization, input, pause/seek and recovery remain to be tested. Preserve `ptube.log` and `ptube-previous.log` when reporting a failure.

Build/test logs are in `native/test-results/`; sanitized live resolver results are in `dist/native-live-*.json`. The native source archive includes the exact upstream source snapshot, adapted frontend, applied correction and build tools.
# Build 5 recovery validation

The recovery policy tests cover the single-attempt limit, absence of recovery
before any playback position exists, and the 20-second refresh boundary.
The native PSP build and install archive are checked separately. Hardware
validation is pending: play a video, briefly interrupt the access point, then
restore it and check whether playback resumes near the previous position.
Also check Back during buffering and a sustained outage: recovery must terminate
instead of retrying indefinitely. Preserve both logs before reopening the app.
