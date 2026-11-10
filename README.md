# PTube Native for PSP CFW

The current app is a standalone adaptation of **KOMI Tube / Tilefinch** with
direct YouTube access and PSP firmware H.264/AAC decoding. It prefers 360p and
does not use a VPS, Google Data API key or server-side conversion.

- Install: [Native PSP setup](docs/NATIVE-SETUP.md).
- App: `dist/PTube-native-psp.zip`, installed at `PSP/GAME/PTUBE_NATIVE`.
- Source: `dist/PTube-native-source.zip`.
- Attribution and pinned source: [native/UPSTREAM.md](native/UPSTREAM.md).
- Validation and remaining failures: [docs/NATIVE-TESTING.md](docs/NATIVE-TESTING.md).
- Windows build: `scripts/build-native.ps1`; add `-RunTests` for host tests.

The native package uses a separate folder so existing prototype data stays
intact. This adapted build requires testing on your PSP-3000 / ARK-4; upstream
device reports are not a verification of our binary. Other models and CFW
remain unqualified, and PSP-1000 is not supported by this 64 MB engine.

## Earlier VPS prototype

The material below describes the older app in `psp/` and `server/`, retained
for reference. It is **not** the native app's install or playback path.

### PTube server alpha

Native CFW app with YouTube Data API search, result selection, and an experimental VPS-backed video player. Configured for **204.12.227.121:8080**, Wi-Fi profile 1. Start with the included test video before enabling real YouTube playback.

## Install

See **[Ubuntu and PSP setup](docs/UBUNTU-SETUP.md)** for the complete walkthrough, Google API key creation, and controls. Release archives are generated in `dist/`: `PTube-alpha-psp.zip` and `PTube-alpha-server.zip`.

Extract the PSP archive to the memory-stick root. Run the server on your Ubuntu VPS using the included Docker Compose configuration. The server initially defaults to demo mode and localhost binding; setup explains enabling remote access. Nothing has been deployed to the VPS by this project.

## What works and what is unverified

- Compiles to a standard user-mode `EBOOT.PBP` with PSPDEV; clean build has no compiler or import-fixup warnings.
- Official API search service, metadata cache, persistent global daily search budget, per-address throttles, and bounded preparation/streaming slots. Live API search still needs your own key; API interactions are tested with mocks.
- Real FFmpeg conversion and HTTP streaming tested end to end with an eight-second generated clip: 120 valid 640x360 JPEG frames plus 44.1 kHz stereo PCM, about 8.01 seconds total.
- A short live public YouTube video was successfully fetched and converted on the development network. The old yt-dlp test video was unavailable. Your VPS's network may produce different results.
- **PSP hardware/emulator execution and CFW compatibility are not verified yet.** ARK-4 on PSP-3000 is the first target; PRO/ME/LME and PSP-1000 remain untested.

## Playback design and limits

The official Data API supplies search metadata, not native video streams. Experimental playback uses yt-dlp/FFmpeg and is disabled by default outside demo mode. This is separate from the official embedded-player path and subject to YouTube's restrictions. See the setup guide before enabling it or considering public access.

The alpha uses a deliberately simple `PTV1` transport: 640x360 JPEG frames and raw stereo PCM, targeting approximately **14.98 fps**, buffered in eight slots on the PSP. The screen shows 480x270 letterboxed video. Sources below 360p are enlarged, not enhanced. Videos are fully prepared before watching, limited to ten minutes/256 MiB, with no seeking. START pauses and O stops. Decoder throughput, Wi-Fi bandwidth, and A/V synchronization require testing on real hardware; this is not a promise of smooth 30 fps video.

HTTP is plaintext. Direct connections only: no HTTPS, IPv6, CDN, or reverse-proxy deployment in this alpha. Hostnames resolve over IPv4, or use the supplied numeric IP. No account login, cookies, bypasses for restricted videos, ads, subscriptions, or playlists are implemented.

## Development

Server tests (Node.js 22+):

Seven tests cover search, quotas, preparation, streaming, and categorized/redacted worker failures.

```sh
cd server
node --test
```

Real conversion/streaming integration test, after building the server image:

```sh
docker build -t ptube-server:alpha server
docker run --rm -v "$PWD/server:/workspace:ro" -w /workspace ptube-server:alpha node integration.mjs
```

On Windows with Ubuntu WSL and Docker available, `scripts/build.ps1` uses a pinned PSPDEV container to compile and package releases. `-SkipBuild` only repackages an existing binary. In a native PSPDEV environment, run `make -B` inside `psp`.

Protocol: 32-byte little-endian header (`PTV1`, width, height, audio rate, samples per frame, three reserved uint32 fields). Each frame is a uint32 JPEG length, JPEG bytes, then 2944 stereo signed-16-bit PCM samples (11776 bytes). A zero JPEG length terminates the stream. Frame rate is exactly `44100/2944`, keeping the audio aligned to PSP hardware requirements. JPEG frames are capped at 512 KiB and the native client rejects unsupported headers and truncated bodies.

Dependencies: [PSPDEV/PSPSDK](https://github.com/pspdev/pspdev), its packaged libjpeg, [FFmpeg](https://ffmpeg.org/), [yt-dlp](https://github.com/yt-dlp/yt-dlp), Node.js and Python. No third-party player source was copied into this project.
