# Bad Apple!! in cmatrix

The Bad Apple!! animation, rendered as ASCII and played by a patched
[cmatrix](https://github.com/abishekvashok/cmatrix) — with the original
soundtrack, and the Matrix rain falling around the artwork.

```
   ___  _        _              _                 _
  / _ )(_)______(_) ___  _ __ | |__  _ __  ___  | |
 / _ \ | / _// _ \ |/ _ \| '_ \| '_ \|/ _ \/ _ \ | |
/ ___/ |_\__ \___/ |  __/| | | | | | |  __/  __/ |_|
\____/ |_/___/_/ |_|\___||_| |_|_|_|\___\___|\___|\___/
```

## Quick start

```sh
git clone https://github.com/drewdalina28-jpg/badapple-cmatrix.git
cd badapple-cmatrix
./install.sh
./badapple
```

`install.sh` installs the dependencies (ffmpeg, python, a C compiler, ncurses
and mpv), builds the patched cmatrix, and downloads the pre-rendered movie
and soundtrack from the [latest release](https://github.com/drewdalina28-jpg/badapple-cmatrix/releases).

Then:

```
badapple              # movie in a window, rain falling around it, with sound
badapple -N           # fullscreen movie, no rain
badapple --no-audio   # picture only
badapple -C red       # different rain colour
badapple -b           # bold characters
```

Press `q` to quit.

## What it looks like

The artwork is the classic 4:3 shadow-art animation, box-averaged down to
whatever size your terminal is, painted with a character ramp and three
colour levels: dim green edges, bold green body, white hot core. The rain
keeps falling around it in cmatrix's own style.

## How it works

### The movie

`tools/render.py` runs the video through ffmpeg and packs the frames into a
compact `.bpam` file:

```
magic 'BPAM' | u32 version | u32 width | u32 height | u32 frame_us | u32 nframes | nibbles...
```

Frames are kept as 4-bit grayscale rather than pre-baked ASCII so the player
can re-fit the artwork to any terminal size at runtime.

Frame timing is stored in **microseconds** instead of frames per second, so
the movie's period can be made *exactly* the length of the audio. That is
what lets the picture and the sound loop together instead of slowly sliding
apart.

### The player

`src/cmatrix.c` is cmatrix with a patch that adds three options:

| flag | meaning |
| --- | --- |
| `-P <movie>` | play a `.bpam` movie over the matrix rain |
| `-S <audio>` | play a soundtrack, in sync with the movie |
| `-N` | movie only, full screen, no rain |

`src/cmatrix-badapple.patch` is the same change as a diff against upstream
cmatrix, if you would rather apply it to your own checkout.

### Audio/video sync

The picture is not merely running alongside the music — it is **clocked from
it**. mpv is started with an IPC socket, and the player asks it for
`time-pos`. Whatever mpv reports as "position 0" becomes the origin of the
animation clock, so the picture is locked to the music from the very first
frame and stays locked through every loop.

The sync is measured, not assumed. `tools/check_audio_sync.py` grabs the
terminal, asks mpv where it is, and compares the screen against the source
video at that timestamp:

```
round 0: audio at   3.678s -> best IoU 0.998 at offset -0.05s
round 1: audio at   9.564s -> best IoU 1.000 at offset +0.00s
round 2: audio at  17.160s -> best IoU 1.000 at offset +0.00s
round 3: audio at  22.366s -> best IoU 0.993 at offset +0.00s
```

`tools/check_loop_sync.py` does the same across repeated loop points.

## Making your own

Point the tooling at any video and it will cut the audio to exactly the
movie's length so the two loop in step:

```sh
./install.sh --render /path/to/your/video.mp4
./badapple
```

or do it by hand:

```sh
python3 tools/render.py video.mp4 badapple.bpam --width 120 --height 90 --fps 20 --duration 219.1
ffmpeg -i video.mp4 -vn -c:a aac -af apad -t 219.1 badapple_audio.m4a
```

Higher resolutions (`--width 240 --height 180`) look better on large
terminals, at the cost of disk and RAM.

## Requirements

* a C compiler, ncurses headers
* `ffmpeg` and `python3` (to build a movie)
* `mpv` (or `vlc`) for the soundtrack
* Linux, macOS or Termux on Android

## Notes

* The movie is drawn at whatever resolution your terminal has, and the
  aspect ratio is preserved.
* `q` quits and kills the audio player; closing the terminal (SIGHUP) also
  cleans up.
* If mpv's IPC socket cannot be used, the player falls back to a fixed
  startup delay, tunable with `BADAPPLE_AUDIO_DELAY` (milliseconds, default
  350).

## Credits and licensing

* `src/cmatrix.c` is a derivative of [cmatrix](https://github.com/abishekvashok/cmatrix)
  by Chris Allegretta and Abishek V Ashok, and is therefore under the
  GNU General Public License v3 (see `LICENSE`).
* The Bad Apple!! animation and music are the property of
  Team Shanghai Alice / Alstroemeria Records. This repository contains no
  copyrighted material: the movie and soundtrack are distributed as release
  assets, and you are welcome to build your own from whatever video you
  like with `tools/make_movie.sh`.

## License

GPL-3.0. See `LICENSE`.
