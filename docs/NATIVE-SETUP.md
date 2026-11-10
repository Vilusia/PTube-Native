# PTube Native — direct YouTube test build

## Build 6: battery display and power modes

The header shows battery percentage (or `--%` when unavailable), with `AC`
when external power is connected. During video playback it appears with the
player overlay; press a button to show the overlay.

On the home screen, select **Power** and press confirm to cycle modes:

| Mode | Menus | Playback |
| --- | --- | --- |
| Performance (default) | 333 MHz | 333 MHz |
| Balanced | 222 MHz | 266 MHz |
| Battery saver | 111 MHz | 222 MHz |

Loading, reconnecting and buffering temporarily use 333 MHz. Settings are saved
in `ptube-power.txt`; the install ZIP does not replace that file. If a reduced
clock causes uneven playback, choose Performance. Actual battery-life gains
and playback at reduced clocks still need device testing. Screen brightness
continues to use the PSP's own controls.

This app adapts the MIT-licensed KOMI Tube frontend and its Tilefinch HTTPS,
YouTube resolver and firmware media engine. Those projects supply the native
playback implementation; this is not an independent rewrite. Full licences,
credits and source provenance are included in NOTICES and UPSTREAM.md.

## Install on your PSP-3000 / ARK-4

1. Extract `PTube-native-psp.zip` to the root of the memory stick.
2. Confirm `PSP/GAME/PTUBE_NATIVE/EBOOT.PBP`, `roots.pem`, `fonts` and `NOTICES`
   are together. Copy the entire folder, not only the EBOOT.
3. Set the PSP's date/time correctly for HTTPS certificate checks. Turn on
   WLAN and test your saved Wi-Fi connection in the PSP settings.
4. Launch **PTube Native** from Game → Memory Stick. It uses the last saved
   access point or the PSP's connection chooser.
5. Select **Discover videos** for shuffled results from a randomly chosen
   topic. In that feed, SELECT loads a new topic and mix. These are topic-based
   suggestions, not account-personalized recommendations or an official
   YouTube Trending chart.
6. Select **Search YouTube**, enter a term using the system keyboard, then
   select a result to play.

No VPS, Google Data API key, `server.txt`, Docker service or PC is used during
playback. The older PTube prototype can remain installed alongside this app.
The native version does not send requests through 204.12.227.121.

## Controls

Confirm/back follow your PSP's X/O preference; use the on-screen icons.

- Home: confirm opens Search or Bookmarks; Triangle searches; START exits.
- Results: Up/Down selects; confirm plays; L/R changes page; START searches.
- Triangle saves/removes a bookmark; Square opens related videos.
- Player: confirm pauses/resumes; Left/Right seeks ten seconds; back stops.
- HOME opens the PSP's exit screen.
- Discover feed: SELECT refreshes with a different topic and shuffled results.

Build 3 adds Back cancellation while searching and an independent operation
watchdog. A search that cannot return normally exits to XMB after about 45
seconds; opening a video is bounded at about 65 seconds, closing at 15 seconds.
Those limits do not guarantee recovery from a firmware/kernel-level stall.
Automatic extra-page loading was removed so the first results show sooner.
The previous session's log is kept as `ptube-previous.log` on relaunch.

## Playback and troubleshooting

The preferred source is 360p, scaled onto the PSP's display by its graphics
hardware. Compatible H.264 Baseline/Main and AAC-LC are decoded by firmware.
This replaces the old JPEG/PCM server conversion and its 15 fps target.

This particular adaptation still needs your hardware test. Upstream's device
reports do not establish that our new build runs correctly. PSP-3000 with
64 MB is the target; PSP-1000 is not supported. PSP-2000/Go and other CFW are
not qualified here. Restricted videos, additional-token requirements,
network challenges and future YouTube changes can still prevent playback.

See `TESTING.md` for the recorded checks and remaining failures. The PSP
cross-build, package checks, native search fixtures and a live short-video
resolver test passed; three broader upstream host tests remain failing, and
other live videos were rejected by network admission. This is experimental.

If a video fails, record the displayed message and copy `ptube.log` from the
app folder. Logs may contain titles and diagnostic details, so review them
before sharing. Bookmarks stay in `ptube-bookmarks.txt` on your memory stick.
Do not delete your app folder when updating if you want to keep those files.

## Source and build

The source archive contains the exact MIT-licensed upstream snapshot in
`native/upstream-source.tar`, the adapted frontend and container build files.
The tar preserves the source snapshot for inspection. For the supplied build
scripts, create the matching Git checkout (with Unix line endings):

```sh
mkdir -p upstream
git -c core.autocrlf=false clone https://github.com/kouminiku-9900/komi-tube.git upstream/komi-tube
git -C upstream/komi-tube checkout 47bf5677a5254dfc37d8716233e7b1861c51698d
```

From the project root in a Linux environment with Docker:

```sh
docker build -t ptube-native-builder:1 -f native/Dockerfile native
docker run --rm -v "$PWD:/work" ptube-native-builder:1 sh /work/native/build.sh
```

`native/test-host.sh` runs the engine's host tests and the native frontend
fixtures using `native/Dockerfile.host` (Ubuntu/glibc with Node.js and rsync).
The cross-builder is Alpine; its host tools do not satisfy that test suite's
glibc requirements. Build dependencies are downloaded from locked URLs.
This build disables the unused voice feature and does not use KZE's closed
PSPTube implementation or its branding/assets.
# Build 5 playback recovery

After a video has started, a failed playback or a stream-link refresh with no
picture for 20 seconds triggers one connection recovery attempt. PTube closes
the media owners, shuts down the transport with a bounded wait, creates a fresh
transport and reopens the video, requesting a seek to the last known position.
Back still cancels playback. A transport that cannot shut down safely, or a
second playback failure, returns to the PSP menu with a message. No worker is
force-deleted and recovery does not loop indefinitely.

Install `PTube-native-build5.zip` by copying its `PSP` folder to the memory stick
and allowing replacement of app files. Existing bookmarks are not in the ZIP.
This recovery build still needs testing on a real PSP with an interrupted
connection; host checks cannot establish that firmware networking will recover.
