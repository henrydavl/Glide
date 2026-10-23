# Glide

A video player for Windows that plays everything, built so `.ts` recordings (TV, IPTV, camera) play without lag. It's Qt with libmpv as the engine, and a PotPlayer-style playlist that shows every video in the folder.

## Why .ts files lag in other players, and what Glide does about it

| Cause | Glide's default |
| --- | --- |
| Interlaced 1080i broadcast deinterlaced on the CPU | `deinterlace=auto`: only interlaced frames, on the GPU (D3D11 video processor) when hardware decoding is active, output at the full 50/60 fps field rate |
| MPEG-2 / H.264 / HEVC decoded in software | `hwdec=auto-safe`: D3D11VA, falling back to software only when the GPU can't handle the codec |
| Video shown through a copy or a compositor | On Windows mpv presents straight into the window with D3D11 (`vo=gpu-next`, `gpu-api=d3d11`) |
| 25/50 fps content judders on a 60 Hz screen | `video-sync=display-resample` locks playback to the display clock |
| Slow USB/network drives or bitrate spikes starve the decoder | 512 MiB / 30 s read-ahead cache |
| Streams announced late in TV/IPTV recordings | Deeper probing when the file opens |

Press **I** while playing to see the decoder (`d3d11va` means the GPU), whether the source is interlaced, and how many frames were dropped. **Tab** opens mpv's full statistics page.

## Keyboard

| Key | Action |
| --- | --- |
| Space | Play / pause |
| ← / → | Seek 5 s (Shift 1 s, Ctrl 30 s, Alt 60 s) |
| ↑ / ↓, mouse wheel | Volume |
| Enter, F, double-click | Fullscreen (Esc leaves) |
| Page Up / Page Down | Previous / next file |
| F6 or L | Show / hide playlist |
| A / S | Cycle audio / subtitle track |
| E | Audio window (equalizer, decoders, output) |
| [ / ] | Audio delay −/+ 50 ms |
| D | Deinterlace: auto → always → off |
| H | Hardware decoding on / off |
| . / , | Step one frame forward / back |
| I / Tab | Media info / statistics |
| Ctrl+O, Ctrl+Shift+O | Open file / open folder |

## Audio

Press **E** (or Audio button → Equalizer…) to open the audio window:

- **Equalizer:** 10 bands from 31 Hz to 16 kHz, ±12 dB each, plus a preamp. Changes apply live while you listen. Comes with built-in presets (Bass boost, Dialogue, Loudness and more); save your own with *Save as…*. A limiter stops boosted bands from clipping. *Night mode* compresses loud scenes so dialogue stays audible at low volume.
- **Decoders:** choose which decoder handles each format, like PotPlayer's built-in audio codec settings (e.g. AC-3 with `ac3` or `ac3_fixed`, MPEG Layer II with `mp2` or `mp2float`). Tick *Passthrough* to send Dolby/DTS untouched to an AV receiver over HDMI or S/PDIF. The status line shows which decoder is actually in use.
- **Output:** audio device, channel layout (automatic, stereo downmix, 5.1, 7.1), WASAPI exclusive mode, and audio delay for out-of-sync recordings.

Right-click the video for track menus, deinterlace options and low-power rendering (cheaper scalers for weak integrated GPUs). Files resume where you left off, and the last playlist comes back on the next launch.

## Getting the Windows build

**[Download the latest Windows build](https://github.com/henrydavl/Glide/releases/latest/download/Glide-windows-x64.zip)**

Every push to `main` builds Glide on GitHub Actions and publishes it as a new [release](https://github.com/henrydavl/Glide/releases) (`v0.1.<build number>`), so the link above always gives you the newest version. Unzip it anywhere and run `Glide.exe`; nothing needs installing. To update, replace the folder (your settings and resume points live in `%APPDATA%\Glide`). To make Glide your default player, right-click a `.ts` file → Open with → Choose another app → Glide.exe → Always.

### Building on Windows yourself

Install [MSYS2](https://www.msys2.org), open the **UCRT64** shell, then:

```bash
pacman -S --needed git mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,qt6-base,mpv}
./scripts/build-windows.sh
```

## Developing on macOS

```bash
brew install qtbase mpv pkgconf cmake   # use the ARM Homebrew in /opt/homebrew
./scripts/build-mac.sh
open build/Glide.app --args /path/to/video.ts
```

On macOS mpv renders through OpenGL inside Qt. That path is only for development, and the Windows D3D11 path is what ships. Set `GLIDE_VIDEO=gl` or `GLIDE_VIDEO=native` to force either path.

Regenerate the icons with `python3 scripts/make-icon.py` (needs Pillow).
