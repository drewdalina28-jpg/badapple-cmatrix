# Installation guide

Everything you need to get Bad Apple!! playing inside cmatrix, on Linux,
macOS or Termux on Android.

If you just want to try it, read [Quick start](#quick-start) and stop there.
Everything else is for when something goes sideways, or when you want to
build the thing yourself.

## Contents

- [What you need](#what-you-need)
- [Quick start](#quick-start)
- [Termux (Android)](#termux-android)
- [Debian and Ubuntu](#debian-and-ubuntu)
- [macOS](#macos)
- [Fedora, Arch and friends](#fedora-arch-and-friends)
- [Building by hand](#building-by-hand)
- [Fetching the movie and soundtrack](#fetching-the-movie-and-soundtrack)
- [Making your own movie](#making-your-own-movie)
- [Command line options](#command-line-options)
- [Putting `badapple` on your PATH](#putting-badapple-on-your-path)
- [Troubleshooting](#troubleshooting)
- [Verifying the install](#verifying-the-install)
- [Uninstalling](#uninstalling)
- [How the pieces fit together](#how-the-pieces-fit-together)

## What you need

| | |
| --- | --- |
| **C compiler** | gcc or clang |
| **ncurses** | development headers, so `cc` can find `curses.h` and `-lncurses` |
| **ffmpeg** | only to render a movie yourself |
| **python3** | only to render a movie yourself |
| **mpv** (or vlc) | to play the soundtrack |
| **git** | to clone the repository |
| **disk** | about 30 MB: 23 MB movie, 3 MB soundtrack |

The pre-rendered movie and soundtrack are not stored in the git repository;
they are attached to the [releases page](https://github.com/drewdalina28-jpg/badapple-cmatrix/releases)
and downloaded by the installer. Nothing copyrighted travels through git.

Your terminal should be at least 40x30 cells. The movie is drawn at whatever
resolution your terminal has, and the aspect ratio is preserved, so a wider
terminal means a bigger picture.

## Quick start

This works on Linux, macOS and Termux:

```sh
git clone https://github.com/drewdalina28-jpg/badapple-cmatrix.git
cd badapple-cmatrix
./install.sh
./badapple
```

`install.sh` does three things:

1. checks that the dependencies are there and installs the ones that are
   missing (using `pkg`, `apt-get` or `brew`, whichever it finds);
2. compiles `src/cmatrix.c` into `src/cmatrix-badapple`;
3. downloads `badapple.bpam` and `badapple_audio.m4a` from the latest release.

Press `q` to quit.

If you would rather install the dependencies yourself first, the per-platform
sections below list them, and `install.sh` will notice they are already
there and skip that step.

## Termux (Android)

Install the packages first, then run the same three commands as above:

```sh
pkg update
pkg install git clang make ffmpeg python curl mpv
git clone https://github.com/drewdalina28-jpg/badapple-cmatrix.git
cd badapple-cmatrix
./install.sh
./badapple
```

A few things are named differently on Termux than on Debian:

- the C compiler is **clang**; Termux provides `gcc` as a symlink to it, so
  the build works whichever name it uses;
- the interpreter is in the **python** package, and that package installs the
  `python3` command;
- **ncurses is part of the base Termux install**, so there is no extra package
  to add.

Sound works out of the box: Termux's mpv is built with the Android AAudio
and OpenSL ES outputs, so `./badapple` plays the soundtrack through the
phone's normal media stream. No extra permissions are needed for playback.

To keep the movie and soundtrack, add it to your path as described in
[Putting `badapple` on your PATH](#putting-badapple-on-your-path).

## Debian and Ubuntu

```sh
sudo apt-get update
sudo apt-get install -y build-essential libncurses-dev ffmpeg python3 curl git mpv
git clone https://github.com/drewdalina28-jpg/badapple-cmatrix.git
cd badapple-cmatrix
./install.sh
./badapple
```

`install.sh` can install these for you if you would rather not type them: it
runs `sudo apt-get update && sudo apt-get install ...` itself when it finds
`apt-get` and something is missing.

If you have `libncurses-dev` but the build still cannot find `-lncursesw`,
that is fine: the installer falls back to `-lncurses` on its own.

## macOS

Install the Xcode command line tools first (they provide the compiler), then
the rest with Homebrew:

```sh
xcode-select --install
brew install make ffmpeg python curl mpv
git clone https://github.com/drewdalina28-jpg/badapple-cmatrix.git
cd badapple-cmatrix
./install.sh
./badapple
```

macOS ships its own (older) copy of ncurses in the SDK, which is enough for
the build. If the compiler cannot find the curses headers at all, install a
current ncurses and point the compiler at it:

```sh
brew install ncurses
export CFLAGS="-I$(brew --prefix ncurses)/include"
export LDFLAGS="-L$(brew --prefix ncurses)/lib"
./install.sh
```

## Fedora, Arch and friends

`install.sh` only knows about `pkg`, `apt-get` and `brew`. On any other
system it prints a note telling you what is missing and carries on with the
build, so install the dependencies yourself first:

```sh
# Fedora
sudo dnf install gcc ncurses-devel ffmpeg python3 curl git mpv

# Arch
sudo pacman -S base-devel ncurses ffmpeg python curl git mpv
```

then

```sh
git clone https://github.com/drewdalina28-jpg/badapple-cmatrix.git
cd badapple-cmatrix
./install.sh
./badapple
```

## Building by hand

If you would rather not use the installer at all:

```sh
git clone https://github.com/drewdalina28-jpg/badapple-cmatrix.git
cd badapple-cmatrix
cc -O2 -Wall -o src/cmatrix-badapple src/cmatrix.c -lncursesw -lm
```

If your ncurses has no wide-character library, use the narrow one instead:

```sh
cc -O2 -Wall -o src/cmatrix-badapple src/cmatrix.c -lncurses -lm
```

`src/cmatrix.c` is a patched copy of [upstream cmatrix](https://github.com/abishekvashok/cmatrix),
so you can also build it with `make` if you prefer, or apply
`src/cmatrix-badapple.patch` to a checkout of upstream cmatrix and build
that.

## Fetching the movie and soundtrack

The installer does this for you, but on its own it is just two downloads:

```sh
./install.sh --download
```

or, by hand:

```sh
curl -fL -o badapple.bpam \
  https://github.com/drewdalina28-jpg/badapple-cmatrix/releases/latest/download/badapple.bpam
curl -fL -o badapple_audio.m4a \
  https://github.com/drewdalina28-jpg/badapple-cmatrix/releases/latest/download/badapple_audio.m4a
```

Both files go in the root of the repository, next to the `badapple` launcher.
The launcher looks for them there, so do not move them around.

If you only want the program and no movie, `./install.sh --no-download` builds
without downloading anything.

## Making your own movie

Point the tooling at any video and it renders the ASCII movie and cuts the
audio to exactly the movie's length, so the two loop in step:

```sh
./install.sh --render /path/to/your/video.mp4
./badapple
```

`--render` runs `tools/make_movie.sh`, which is also usable on its own:

```sh
tools/make_movie.sh video.mp4
tools/make_movie.sh video.mp4 mymovie.bpam myaudio.m4a 240 180 20
```

The arguments are the input video, the movie to write, the soundtrack to
write, and the width, height and frame rate. The defaults are 120x90 at 20
frames per second.

To do it by hand:

```sh
python3 tools/render.py video.mp4 badapple.bpam --width 120 --height 90 --fps 20 --duration 219.1
ffmpeg -i video.mp4 -vn -c:a aac -af apad -t 219.1 badapple_audio.m4a
```

The `--duration` value must match the length of the audio you cut, or the
picture and the sound will drift apart.

## Command line options

`./badapple` starts the movie in a window with the rain falling around it
and the soundtrack playing. `--no-audio` runs it silently.

| flag | meaning |
| --- | --- |
| `--no-audio` | picture only, no soundtrack |
| `-N` | full screen movie, no rain |
| `-C <color>` | colour of the rain (green by default) |
| `-b` | bold characters |
| `-u <delay>` | screen update delay, 0 to 10 (default 4) |
| `-r` | rainbow mode |
| `-s` | screensaver mode: exits on the first keystroke |
| `-a` | asynchronous scroll |
| `-o` | old-style scrolling |
| `-k` | characters change while scrolling |
| `-t <tty>` | terminal to draw on |

Anything the launcher does not recognise is passed straight through to
`cmatrix-badapple`, so the usual cmatrix flags all still work. Run
`./src/cmatrix-badapple -h` for the complete list.

Two environment variables change how it behaves:

| variable | meaning |
| --- | --- |
| `BADAPPLE_MOVIE` | path to the `.bpam` movie to play |
| `BADAPPLE_AUDIO` | path to the soundtrack to play |
| `BADAPPLE_BIN` | path to the `cmatrix-badapple` binary |
| `BADAPPLE_AUDIO_DELAY` | startup delay in milliseconds when mpv's IPC socket cannot be used (default 350) |

## Putting `badapple` on your PATH

The launcher finds the movie, the soundtrack and the binary relative to its
own location, so it must keep living in the repository. A small wrapper is
the easiest way to have `badapple` available from anywhere:

```sh
mkdir -p ~/bin
cat > ~/bin/badapple <<'EOF'
#!/bin/sh
exec "$HOME/badapple-cmatrix/badapple" "$@"
EOF
chmod +x ~/bin/badapple
```

Then add `~/bin` to your path. With fish:

```sh
fish_add_path --global --prepend ~/bin
```

With bash or zsh, put this in your `~/.bashrc` or `~/.zshrc`:

```sh
export PATH="$HOME/bin:$PATH"
```

Do not symlink `~/bin/badapple` to the launcher instead of wrapping it: the
launcher resolves its own directory, and through a symlink it would look for
the movie and the binary in `~/bin`, where they are not.

## Troubleshooting

**`badapple: movie file ... is missing`**

The movie was not downloaded. Run `./install.sh --download` in the repository.

**No sound**

Check that mpv is installed and can play the file on its own:

```sh
mpv badapple_audio.m4a
```

If mpv is missing, install it, or run the picture only with
`./badapple --no-audio`. On Linux, also check that a PulseAudio or PipeWire
sink exists and that the volume is up.

**The picture is there but nothing moves, or the sound drifts**

The player clocks the animation from mpv's playback position. If mpv cannot
be reached over its IPC socket, it falls back to a fixed startup delay,
which you can tune:

```sh
BADAPPLE_AUDIO_DELAY=500 ./badapple
```

**`cc: cannot find -lncursesw`**

Your ncurses development headers are missing. On Debian and Ubuntu:
`sudo apt-get install libncurses-dev`. On Fedora: `sudo dnf install
ncurses-devel`. The installer already falls back to `-lncurses` on its own,
so this only matters when you build by hand.

**`pkg install failed`**

Termux names some packages differently. The installer maps them for you, but
if you hit this by hand, the working set is:

```sh
pkg install git clang make ffmpeg python curl mpv
```

**The picture looks squashed**

The aspect ratio is preserved, so a very wide but short terminal gives a
short, wide picture. Resize the terminal to something closer to 4:3 for the
original look.

**The characters look like blocks**

They should not: the movie is rendered with plain ASCII (` .:-=+*#%@`), so
it needs no special font. If you see blocks, something else is drawing over
the screen.

**Nothing happens when you start it**

The program needs a real terminal. Running it over a pipe or with the
terminal detached gives it no size and it exits.

## Verifying the install

```sh
./src/cmatrix-badapple -V          # prints the version
ls -l badapple.bpam badapple_audio.m4a
./badapple --no-audio              # picture only, no sound
```

The two downloaded files should be about 23 MB and 3.5 MB. If they are much
smaller, the download was interrupted; delete them and run
`./install.sh --download` again.

To check the picture and the sound are in step, play a few seconds with
sound and watch for the moment the picture is clearly in time with the
music. The picture is not running alongside the soundtrack, it is clocked
from it.

## Uninstalling

```sh
cd ~
rm -rf badapple-cmatrix ~/bin/badapple
```

Nothing else is installed: the program is a single binary, the movie is a
single file, and the soundtrack is a single file.

If you installed the dependencies with `pkg` or `apt-get` and want them gone
too:

```sh
pkg uninstall ffmpeg mpv          # Termux
sudo apt-get remove ffmpeg mpv   # Debian / Ubuntu
brew uninstall ffmpeg mpv        # macOS
```

## How the pieces fit together

```
badapple-cmatrix/
├── badapple              launcher: finds the assets and starts the player
├── badapple.bpam         the movie: 120x90, 4381 frames, 20 fps
├── badapple_audio.m4a    the soundtrack: 219.1 s
├── install.sh            dependencies, build, download
├── src/
│   ├── cmatrix.c         patched cmatrix that can play a movie
│   └── cmatrix-badapple.patch   the same change as a diff against upstream
└── tools/
    ├── render.py         video -> .bpam
    ├── make_movie.sh     video -> .bpam + a soundtrack cut to match
    ├── check_audio_sync.py     checks the picture against the audio
    └── check_loop_sync.py      checks the sync across loop points
```

The `.bpam` file is a 24-byte header (`BPAM`, version, width, height,
microseconds per frame, frame count) followed by 4-bit grayscale frames. The
frames are kept as grayscale rather than pre-baked ASCII so the player can
re-fit the artwork to any terminal size at runtime, and the timing is stored
in microseconds rather than frames per second so the movie's period can be
made *exactly* the length of the audio.

That is what lets the picture and the sound loop together instead of slowly
sliding apart.
