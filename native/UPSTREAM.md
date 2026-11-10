# Native PTube attribution and provenance

This is an adaptation of KOMI Tube, not an independent implementation of its
networking, YouTube extraction, or firmware video decoder.

- KOMI Tube: https://github.com/kouminiku-9900/komi-tube
- Pinned commit: `47bf5677a5254dfc37d8716233e7b1861c51698d`
- Copyright (c) 2026 komi-tube contributors, MIT.
- Its vendored Tilefinch: https://github.com/stjanovitz/tilefinch
- Upstream base recorded by KOMI: `5a77720f31bf62007249ec458109da50ae7a5a05`, with KOMI's existing modifications.
- Copyright (c) 2026 Steven Janovitz, MIT, plus separately licensed dependencies.

`native/app/` adapts KOMI's native frontend; `native/common/komi_runtime.c`
adapts its runtime. Original identifiers remain to make provenance clear.
PTube adds English UI text, PTube branding, an English media preference, an
isolated installation folder, and reproducible Linux/container packaging.
Build 3 adds a HOME exit callback, cancellable search, independent operation
deadlines, last-session log retention and guarded media teardown. These
loops now also call the engine's required owner CPU-donation checkpoint so
its demoted connection-setup worker can run while the search UI is active.
These
frontend/runtime changes are maintained directly in `native/`; `adapt.mjs`
is only the initial-import helper and must not overwrite those maintained files.
Build 4 adds PTube's own topic-based Discover menu and result shuffling; it
does not use YouTube account recommendations or claim an official Trending feed.
The frontend shares the upstream HTTPS and PSP firmware media modules through
`native/ptube.cmake`; it does not replace those with the old server transport.

One local engine correction is applied reproducibly by `patch-upstream.py`:
`scheduled_mark_complete` used `snprintf` with overlapping source and
destination when its error argument already pointed into the result. This is
undefined behavior and erased errors on the Ubuntu host. A bounded `memmove`
preserves those errors. The original host scheduler, redirect and web-runtime
tests demonstrated failures before this patch; their unchanged assertions
serve as regression checks. No test baselines or limits are relaxed.

Source licences and third-party notices accompany the native release. Keep
them when redistributing it. The old `psp/` and `server/` prototype remains
available separately and is not used by this native build.
