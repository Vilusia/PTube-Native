# Set up PTube on your Ubuntu VPS

Target: **204.12.227.121, TCP port 8080**. This guide is for the hardware-test alpha, not a claim of production readiness. Nothing has been installed on your VPS yet.

## 1. Install Docker

If Docker Engine and the Compose plugin are already installed, keep them. Otherwise follow [Docker's Ubuntu installation guide](https://docs.docker.com/engine/install/ubuntu/). Check:

```sh
sudo docker version
sudo docker compose version
```

Allow approximately 2 GB of RAM and 4 GB of free disk for initial testing. The container is capped at 1 GB RAM, two CPUs, one conversion at a time, and four concurrent streams. Actual capacity and PSP performance need measurement.

## 2. Upload and start the demo

Upload `PTube-alpha-server.zip` to your VPS using your usual file transfer tool. Extract it into a new directory, then enter its `server` directory. For example:

```sh
mkdir -p ~/ptube
unzip PTube-alpha-server.zip -d ~/ptube
cd ~/ptube/server
cp .env.example .env
chmod 600 .env
nano .env
```

For the first remote PSP test, set these values in `.env`:

```dotenv
PTUBE_DEMO=1
ENABLE_UNOFFICIAL_PLAYBACK=0
PTUBE_BIND=0.0.0.0
PTUBE_PORT=8080
```

Leave `YOUTUBE_API_KEY` empty for now. The demo creates its own eight-second test video with a tone; it does not contact YouTube.

Initially allow TCP 8080 **only from your home public IP** using your VPS provider's network firewall. Docker-published ports can bypass UFW rules, so do not rely on UFW alone. Keep your existing SSH access rules. The client currently uses plaintext HTTP; search terms and viewing traffic are visible on the network. The Google API key stays on the server and is sent only to Google's HTTPS API.

Start the service:

```sh
sudo docker compose up -d --build
sudo docker compose ps
curl http://127.0.0.1:8080/health
```

The health response should begin `OK`. From a device on your allowed home network, `http://204.12.227.121:8080/health` should return the same response.

## 3. Install the PSP app

Extract `PTube-alpha-psp.zip` to the root of the PSP memory stick. It contains:

```text
PSP/GAME/PTube/EBOOT.PBP
PSP/GAME/PTube/server.txt
```

The included `server.txt` already contains:

```text
204.12.227.121 8080 1
```

The final number selects saved Wi-Fi profile 1. Change it if you use another saved profile. Turn on the WLAN switch and make sure the PSP can connect using that profile. Launch **PTube Alpha** from Game → Memory Stick on your ARK-4 PSP.

Press **START** to search. In demo mode any search returns the test video. Press **X** to prepare and play it. During playback, **START** pauses/resumes and **O** stops. Check both the picture and sound, then stop and replay it. HOME exits the app.

This build has been compiled, but not run on a physical PSP or in an emulator. If it fails, report the exact on-screen message, whether Wi-Fi connects, and whether the demo has picture, sound, or both. If video is slow or audio stutters, stop at this stage: the 360p software decoder and Wi-Fi throughput need hardware tuning before public access.

## 4. Get your YouTube search API key

1. Open [Google Cloud Console](https://console.cloud.google.com/) and create/select a project.
2. Open **APIs & Services → Library**, find **YouTube Data API v3**, and enable it.
3. Open **APIs & Services → Credentials → Create credentials → API key**.
4. Edit the key. Set **API restrictions** to **Restrict key → YouTube Data API v3**. Set **Application restrictions** to **IP addresses**, and add `204.12.227.121` if that is also your VPS's outgoing public IP. If outbound traffic uses a different address, use that address instead.
5. Put the key after `YOUTUBE_API_KEY=` in the VPS's `.env`. Keep the real file private; never include it in a client build or share it in chat.

No Google sign-in flow is needed in the PSP app for public searches. See [Google's getting-started guide](https://developers.google.com/youtube/v3/getting-started).

## 5. Enable real searches and experimental playback

Search uses the official Data API. Playback uses **yt-dlp and FFmpeg**, separately from the official API, to prepare a temporary 360p JPEG/PCM stream. This downloads/converts video and is subject to [YouTube's developer policies](https://developers.google.com/youtube/terms/developer-policies). It is not an official YouTube player or a policy-compliant substitute for the embedded player. Use only content you have permission to process, and assess these restrictions before offering a public service.

After the demo works, edit `.env`:

```dotenv
YOUTUBE_API_KEY=your-private-key
PTUBE_DEMO=0
ENABLE_UNOFFICIAL_PLAYBACK=1
DAILY_SEARCH_LIMIT=40
```

Apply the changes:

```sh
sudo docker compose up -d
```

Search results are limited to short videos for this alpha. Videos must have a known duration no longer than ten minutes. The first watch waits for complete preparation. Sources are selected at up to 360p; lower-resolution sources are enlarged to the fixed 640x360 transport. The PSP scales that to 480x270 with letterboxing. Target playback is approximately 14.98 fps, not 30 fps, and actual speed depends on the PSP and network.

The server caches up to eight prepared videos, each at most 256 MiB, for up to 30 minutes of availability. A video may be unavailable, restricted, or blocked from a datacenter IP. The service does not bypass account, age, region, DRM, or bot challenges. Search budget and per-address limits can also cause explicit errors. Do not put the current server behind a CDN or reverse proxy: its limits identify direct socket peers, and the PSP requires uncompressed Content-Length HTTP responses.

## Checks and maintenance

```sh
sudo docker compose logs --tail=50
sudo docker compose ps
sudo docker compose down
```

`down` stops the service and keeps its data volume. Never add `-v` unless you intend to delete that data. The updated server reports categorized failures to the app and retains bounded, redacted worker details in its logs. Older packages only logged a generic worker error. A terminal can run the worker directly for diagnosis:

```sh
sudo docker compose exec ptube /opt/worker/bin/python /app/prepare.py --video VIDEO_ID_HERE --output /tmp/check.ptv
```

For a short known test, replace `VIDEO_ID_HERE` with `jNQXAC9IVRw`. Paste the resulting error text when requesting help, keeping API keys private. A sign-in/bot or HTTP 403 response indicates a YouTube access problem on that server; an FFmpeg exception indicates conversion. Do not change firewall rules or API restrictions based only on a generic preparation error.

YouTube changes can break extraction. The tested worker uses yt-dlp 2026.10.26 and Node.js for JavaScript support. Updates require changing the pinned version in `Dockerfile`, rebuilding, and rerunning the demo and a permitted real-video test.

Before inviting other people, finish the PSP hardware test, then decide your quota, capacity, moderation, privacy policy, and hosting terms. This package is ready for a controlled hardware test, not unrestricted public launch.
