# Validation — 2026-11-09

- Pinned PSPDEV image: `sha256:ee385df3aa783189d5a033af5eb1d4a9cdd93718fb8bf652ec7bb48e54038a8b`.
- Clean CFW build: passed with no compiler/linker/import-fixup warnings. EBOOT title: PTube Alpha.
- Seven Node.js server tests: passed. Covers search protocol and credentials errors, upstream errors, per-client throttling, persistent global budget, preparation concurrency, media delivery, disabled unofficial playback, actionable error categories, and log redaction.
- Real FFmpeg/Python/HTTP integration: passed under the 1 GiB/two-CPU/64-process, read-only-container settings. The generated eight-second clip produced 120 JPEG frames at 640x360, 44.1 kHz stereo PCM, 8.0109 seconds of aligned content, and a 2,809,537-byte PTV stream. Every frame decoded successfully with FFmpeg; audio was non-silent.
- Docker Compose configuration: accepted by Docker Compose 2.40.3.
- Live YouTube worker: successfully fetched and converted `jNQXAC9IVRw` on the development network. The older test ID `BaW_jenozKc` was unavailable. No YouTube video content is included in the archives.
- Archive inspection: PSP/GAME/PTube layout, EBOOT header, server dotfiles, and VPS IP configuration verified. The server archive uses a fixed file allowlist and excludes real `.env` secrets.

Not verified: real YouTube API search with the owner's key, deployment on 204.12.227.121, PSP/emulator execution, actual frame rate, A/V synchronization, pause/stop behavior on hardware, Wi-Fi compatibility, and compatibility across CFW variants. See UBUNTU-SETUP.md for the next test.
