#!/usr/bin/env python3
"""Check that the picture and the sound stay in step across a loop point.

Uses a short movie so that several loop points pass in a minute. Make one
first, e.g. from a 5 second clip of the source video:

    ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 clip.webm
    tools/make_movie.sh clip.webm test5.bpam test5_audio.m4a
    python3 tools/check_loop_sync.py
"""
import json
import os
import socket
import subprocess
import sys
import time

W, H = 120, 90
FPS = 20.0
COLS, ROWS = 90, 28
VIDEO = os.environ.get("VIDEO", "bp_pv.webm")
MOVIE = os.environ.get("MOVIE", "test5.bpam")
AUDIO = os.environ.get("AUDIO", "test5_audio.m4a")
ROOT = os.environ.get("BADAPPLE_ROOT", os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def mpv_timepos(sock_path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(1.0)
    s.connect(sock_path)
    s.sendall(b'{"command":["get_property","time-pos"]}\n')
    data = b""
    while b"\n" not in data:
        chunk = s.recv(4096)
        if not chunk:
            break
        data += chunk
    s.close()
    for line in data.decode("utf-8", "replace").splitlines():
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        if isinstance(msg.get("data"), (int, float)):
            return float(msg["data"])
    return None


def geometry():
    tw, th = W, H
    aw, ah = COLS - 4, ROWS - 2
    if tw > aw:
        th = th * aw // tw
        tw = aw
    if th > ah:
        tw = tw * ah // th
        th = ah
    return (COLS - tw) // 2, (ROWS - th) // 2, tw, th


def video_mask(t, tw, th):
    cmd = ["ffmpeg", "-v", "error", "-ss", f"{t}", "-i", VIDEO,
           "-vf", f"scale={W}:{H}:flags=area,format=gray", "-frames:v", "1",
           "-f", "rawvideo", "-pix_fmt", "gray", "-"]
    buf = subprocess.run(cmd, capture_output=True).stdout
    if len(buf) < W * H:
        return None
    mask = []
    for y in range(th):
        y0, y1 = y * H // th, max(y * H // th + 1, (y + 1) * H // th)
        for x in range(tw):
            x0, x1 = x * W // tw, max(x * W // tw + 1, (x + 1) * W // tw)
            s = n = 0
            for yy in range(y0, y1):
                row = yy * W
                for xx in range(x0, x1):
                    s += buf[row + xx]
                    n += 1
            mask.append(1 if s / n >= 96 else 0)
    return mask


def main():
    subprocess.run(["tmux", "kill-server"], capture_output=True)
    subprocess.run(["tmux", "new-session", "-d", "-s", "loop", "-x", str(COLS),
                    "-y", str(ROWS),
                    f"sh -c 'BADAPPLE_MOVIE={MOVIE} BADAPPLE_AUDIO={AUDIO} "
                    f"{ROOT}/badapple; sleep 60'"])
    time.sleep(2)
    tmp = os.environ.get("TMPDIR", "/tmp")
    socks = sorted((f for f in os.listdir(tmp) if f.startswith("badapple-mpv-")),
                   key=lambda f: os.path.getmtime(os.path.join(tmp, f)))
    sock_path = None
    for f in reversed(socks):
        try:
            mpv_timepos(os.path.join(tmp, f))
            sock_path = os.path.join(tmp, f)
            break
        except OSError:
            continue
    if sock_path is None:
        print("no live mpv ipc socket")
        return

    ox, oy, tw, th = geometry()
    print(f"art region: {tw}x{th} at ({ox},{oy})")
    worst = (1.0, 0.0)
    for wait in [1.0, 3.0, 5.5, 6.5, 8.0, 11.5, 16.0, 21.0]:
        time.sleep(wait if wait == 1.0 else wait - 0.0)
        tp = mpv_timepos(sock_path)
        pane = subprocess.run(["tmux", "capture-pane", "-p", "-t", "loop"],
                              capture_output=True, text=True).stdout
        lines = pane.split("\n")
        art = []
        for y in range(oy, oy + th):
            ln = lines[y] if y < len(lines) else ""
            art.append([0 if i >= len(ln) or ln[i] == " " else 1
                        for i in range(ox, ox + tw)])
        best = (0.0, 0.0)
        for off in [x * 0.05 for x in range(-10, 3)]:
            m = video_mask(round((tp + off) * FPS) / FPS, tw, th)
            if m is None:
                continue
            m = [m[i * tw:(i + 1) * tw] for i in range(th)]
            inter = sum(1 for ya, yb in zip(art, m) for xa, xb in zip(ya, yb)
                        if xa and xb)
            union = sum(1 for ya, yb in zip(art, m) for xa, xb in zip(ya, yb)
                        if xa or xb)
            iou = 1.0 if union == 0 else inter / union
            if iou > best[0]:
                best = (iou, off)
        print(f"audio {tp:6.2f}s -> IoU {best[0]:.3f} at {best[1]:+.2f}s")
        if best[0] < worst[0]:
            worst = best
        time.sleep(0.0)

    subprocess.run(["tmux", "kill-server"], capture_output=True)
    print(f"\nworst IoU {worst[0]:.3f} at {worst[1]:+.2f}s")
    # the pane snapshot is always a little older than the mpv query, so a
    # small negative offset is expected
    print("PASS" if worst[0] > 0.9 and -0.45 <= worst[1] <= 0.05 else "CHECK")


if __name__ == "__main__":
    main()
