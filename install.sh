#!/bin/sh
# install.sh - set up Bad Apple in cmatrix.
#
#   ./install.sh                 install dependencies, build, fetch the movie
#   ./install.sh --render <vid>  build, then render the movie from <vid>
#   ./install.sh --download      build, then download the pre-rendered movie
#   ./install.sh --no-download   build only
#
# The pre-rendered movie is attached to the project's GitHub release, so
# nothing copyrighted has to travel through the repository itself.

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO="drewdalina28-jpg/badapple-cmatrix"
RELEASE_URL="https://github.com/$REPO/releases/latest/download"

say() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die() { printf '\033[1;31mxx\033[0m %s\n' "$*" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------- dependencies
install_deps() {
    say "checking dependencies"
    need=""
    for c in gcc make ffmpeg python3 curl; do
        have "$c" || need="$need $c"
    done
    if ! have mpv && ! have vlc; then
        need="$need mpv"
    fi
    [ -n "$need" ] || { say "all dependencies present"; return; }

    if have pkg; then          # Termux
        # Termux names its packages differently from Debian: the C compiler
        # is "clang" (the gcc command is a symlink to it) and the python3
        # interpreter comes from the "python" package.
        pkgs=""
        for p in $need; do
            case "$p" in
                gcc)     pkgs="$pkgs clang" ;;
                python3) pkgs="$pkgs python" ;;
                *)       pkgs="$pkgs $p" ;;
            esac
        done
        say "installing with pkg:$pkgs"
        pkg install $pkgs || die "pkg install failed"
    elif have apt-get; then     # Debian / Ubuntu
        sudo apt-get update
        sudo apt-get install -y build-essential ffmpeg python3 curl libncurses-dev mpv \
            || die "apt install failed"
    elif have brew; then       # macOS
        brew install $need || die "brew install failed"
    else
        warn "no known package manager, install these yourself: $need"
    fi
}

# ---------------------------------------------------------------------- build
build() {
    say "building cmatrix-badapple"
    cd "$HERE/src" || die "no src directory"
    if have cc; then
        cc -O2 -Wall -o cmatrix-badapple cmatrix.c -lncursesw -lm || \
            cc -O2 -Wall -o cmatrix-badapple cmatrix.c -lncurses -lm || \
            die "compilation failed"
    else
        gcc -O2 -Wall -o cmatrix-badapple cmatrix.c -lncursesw -lm || \
            gcc -O2 -Wall -o cmatrix-badapple cmatrix.c -lncurses -lm || \
            die "compilation failed"
    fi
    say "built $HERE/src/cmatrix-badapple"
}

# ------------------------------------------------------------------- download
download() {
    say "downloading the pre-rendered movie from the GitHub release"
    have curl || die "curl is required"
    curl -fL --progress-bar -o "$HERE/badapple.bpam" "$RELEASE_URL/badapple.bpam" \
        || die "could not download badapple.bpam"
    curl -fL --progress-bar -o "$HERE/badapple_audio.m4a" "$RELEASE_URL/badapple_audio.m4a" \
        || die "could not download badapple_audio.m4a"
    say "movie and soundtrack ready"
}

# --------------------------------------------------------------------- render
render() {
    [ -n "$1" ] || die "usage: ./install.sh --render <video>"
    [ -f "$1" ] || die "no such file: $1"
    say "rendering the movie from $1"
    "$HERE/tools/make_movie.sh" "$1" "$HERE/badapple.bpam" "$HERE/badapple_audio.m4a" \
        || die "rendering failed"
}

# ------------------------------------------------------------------------ main
MODE="download"
VIDEO=""
while [ $# -gt 0 ]; do
    case "$1" in
        --render)
            MODE="render"
            shift
            VIDEO="${1:-}"
            ;;
        --download) MODE="download" ;;
        --no-download) MODE="none" ;;
        -h|--help)
            sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done

install_deps
build

case "$MODE" in
    download) download ;;
    render)   render "$VIDEO" ;;
    none)     say "skipping the movie, run ./install.sh --download later" ;;
esac

say "done - run it with:  $HERE/badapple"
