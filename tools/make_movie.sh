#!/bin/sh
# make_movie.sh <video> [movie-out] [audio-out] [width] [height] [fps]
#
# Turns a video into a Bad Apple movie plus a matching audio track, so that
# the picture and the sound loop together in step.

set -e

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(dirname "$HERE")

VIDEO="$1"
if [ -z "$VIDEO" ]; then
    echo "usage: make_movie.sh <video> [movie-out] [audio-out] [w] [h] [fps]" >&2
    exit 1
fi
if [ ! -f "$VIDEO" ]; then
    echo "make_movie.sh: no such file: $VIDEO" >&2
    exit 1
fi

OUT="${2:-$ROOT/badapple.bpam}"
AUDIO="${3:-$ROOT/badapple_audio.m4a}"
W="${4:-120}"
H="${5:-90}"
FPS="${6:-20}"

DUR=$(ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 "$VIDEO")
if [ -z "$DUR" ] || [ "$DUR" = "N/A" ]; then
    echo "make_movie.sh: cannot read the duration of $VIDEO" >&2
    exit 1
fi

echo "rendering $W x $H @ ${FPS}fps from $VIDEO (${DUR}s)"
python3 "$HERE/render.py" "$VIDEO" "$OUT" \
    --width "$W" --height "$H" --fps "$FPS" --duration "$DUR"

# the movie's own period, so the audio can be cut to exactly the same length
PERIOD=$(python3 - "$OUT" <<'PYEOF'
import struct, sys
d = open(sys.argv[1], 'rb').read(24)
fus, n = struct.unpack('<II', d[16:24])
print("%.6f" % (fus * n / 1e6))
PYEOF
)

echo "extracting audio cut to ${PERIOD}s"
ffmpeg -v error -y -i "$VIDEO" -vn -c:a aac -b:a 128k -af apad -t "$PERIOD" "$AUDIO"

echo
echo "movie: $OUT"
echo "audio: $AUDIO"
