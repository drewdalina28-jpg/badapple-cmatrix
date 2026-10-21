#!/usr/bin/env python3
"""Render a video into a packed grayscale 'movie' file for cmatrix-badapple.

Format (all little endian):
    magic   : b'BPAM'          (4 bytes)
    version : u32              (2)
    width   : u32
    height  : u32
    frame_us: u32              microseconds per frame
    nframes : u32
    frames  : nframes * width * height nibbles (4-bit gray, high nibble first)

Grayscale is kept instead of pre-baked ASCII so the player can re-fit the
artwork to any terminal size (box average downscale) at runtime.
"""

import argparse
import os
import struct
import subprocess
import sys

MAGIC = b"BPAM"

HI_TABLE = bytes((b & 0xF0) for b in range(256))
LO_TABLE = bytes((b >> 4) for b in range(256))


def extract_gray(video, width, height, fps, start=0.0):
    """Yield raw gray8 frames from ffmpeg, one bytes-like per frame."""
    vf = f"scale={width}:{height}:flags=area,format=gray,fps={fps}"
    cmd = ["ffmpeg", "-v", "error"]
    if start:
        cmd += ["-ss", f"{start}"]
    cmd += [
        "-i", video,
        "-vf", vf, "-f", "rawvideo", "-pix_fmt", "gray", "-",
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    fsize = width * height
    n = 0
    try:
        while True:
            buf = proc.stdout.read(fsize)
            if buf is None or len(buf) < fsize:
                break
            yield buf
            n += 1
    finally:
        try:
            proc.stdout.close()
        except Exception:
            pass
        err = proc.stderr.read().decode("utf-8", "replace")
        proc.wait()
        if proc.returncode not in (0, None) and err.strip():
            sys.stderr.write(err)


def pack_nibbles(buf):
    """Pack one byte per pixel into 4-bit nibbles, high nibble first.

    Output byte j holds source pixels 2j (high nibble) and 2j+1 (low nibble).
    The two halves never share a bit, so a big-integer OR does the interleave.
    """
    hi = buf[0::2].translate(HI_TABLE)
    lo = buf[1::2].translate(LO_TABLE)
    n = len(hi)
    return (int.from_bytes(hi, "big") | int.from_bytes(lo, "big")).to_bytes(n, "big")


def preview(frame, width, height):
    """Print a crude text preview of one frame for eyeballing."""
    ramp = " .:-=+*#%@"
    for y in range(height):
        row = frame[y * width:(y + 1) * width]
        print("".join(ramp[min(9, v * 10 // 256)] for v in row))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--width", type=int, default=120)
    ap.add_argument("--height", type=int, default=90)
    ap.add_argument("--fps", type=int, default=20)
    ap.add_argument("--duration", type=float, default=0.0,
                    help="exact length of the movie in seconds; the frame "
                         "count and per-frame timing are fitted to it so the "
                         "picture loops in step with the audio")
    ap.add_argument("--preview", type=int, default=0,
                    help="print N preview frames as text and exit")
    ap.add_argument("--start", type=float, default=0.0,
                    help="seek position (seconds) before extracting")
    args = ap.parse_args()

    if args.preview:
        for i, frame in enumerate(extract_gray(args.input, args.width,
                                               args.height, args.fps,
                                               args.start)):
            if i >= args.preview:
                break
            print(f"--- frame {i} ---")
            preview(frame, args.width, args.height)
        return

    target = int(round(args.duration * args.fps)) if args.duration else 0
    count = 0
    with open(args.output, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<IIIII", 2, args.width, args.height, 0, 0))
        for frame in extract_gray(args.input, args.width, args.height, args.fps):
            if target and count >= target:
                break
            f.write(pack_nibbles(frame))
            count += 1
        if args.duration:
            frame_us = int(round(args.duration * 1e6 / count))
        else:
            frame_us = int(round(1e6 / args.fps))
        # patch frame timing and frame count
        f.seek(len(MAGIC) + 4 + 8)
        f.write(struct.pack("<II", frame_us, count))

    size = os.path.getsize(args.output)
    dur = count * frame_us / 1e6
    print(f"{args.output}: {count} frames @ {args.width}x{args.height} "
          f"({frame_us} us/frame, {dur:.6f}s, {size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
