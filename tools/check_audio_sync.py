#!/usr/bin/env python3
"""Verify that the picture is locked to the audio.

Starts the player with audio, asks mpv where it is in the track over its IPC
socket, grabs the terminal contents and compares them with the source video
frame at that same timestamp.

Needs the source video next to this script (or set $VIDEO) to compare against.
"""
import json
import os
import socket
import struct
import subprocess
import sys
import time

W, H = 120, 90
FPS = 20.0
COLS, ROWS = 90, 28
VIDEO = os.environ.get("VIDEO", "bp_pv.webm")
MOVIE = os.environ.get("MOVIE", "badapple.bpam")
AUDIO = os.environ.get("AUDIO", "badapple_audio.m4a")
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
        if "data" in msg and isinstance(msg["data"], (int, float)):
            return float(msg["data"])
    return None


def geometry():
    """Same fit logic as the player."""
    tw, th = W, H
    avail_w, avail_h = COLS - 4, ROWS - 2
    if tw > avail_w:
        th = th * avail_w // tw
        tw = avail_w
    if th > avail_h:
        tw = tw * avail_h // th
        th = avail_h
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
    subprocess.run(["tmux", "new-session", "-d", "-s", "audiosync", "-x", str(COLS),
                    "-y", str(ROWS),
                    f"sh -c '{ROOT}/badapple; sleep 60'"])
    time.sleep(4)

    tmp = os.environ.get("TMPDIR", "/tmp")
    socks = [f for f in os.listdir(tmp) if f.startswith("badapple-mpv-")]
    socks.sort(key=lambda f: os.path.getmtime(os.path.join(tmp, f)))
    sock_path = None
    for f in reversed(socks):
        try:
            mpv_timepos(os.path.join(tmp, f))
            sock_path = os.path.join(tmp, f)
            break
        except OSError:
            continue
    if sock_path is None:
        print("no live mpv ipc socket found")
        return

    worst = None
    for round_no in range(4):
        tp = mpv_timepos(sock_path)
        pane = subprocess.run(["tmux", "capture-pane", "-p", "-t", "audiosync"],
                              capture_output=True, text=True).stdout
        if tp is None:
            print("no time-pos from mpv")
            return
        ox, oy, tw, th = geometry()
        lines = pane.split("\n")
        art = []
        for y in range(oy, oy + th):
            ln = lines[y] if y < len(lines) else ""
            art.append([0 if i >= len(ln) or ln[i] == " " else 1
                        for i in range(ox, ox + tw)])
        # best matching source frame around the reported position
        best = (0.0, 0.0)
        for off in [x * 0.05 for x in range(-8, 9)]:
            t = tp + off
            m = video_mask(round(t * FPS) / FPS, tw, th)
            if m is None:
                continue
            m = [m[i * tw:(i + 1) * tw] for i in range(th)]
            inter = sum(1 for ya, yb in zip(art, m) for xa, xb in zip(ya, yb)
                        if xa and xb)
            union = sum(1 for ya, yb in zip(art, m) for xa, xb in zip(ya, yb)
                        if xa or xb)
            iou = inter / union if union else 0
            if iou > best[0]:
                best = (iou, off)
        print(f"round {round_no}: audio at {tp:7.3f}s -> best IoU {best[0]:.3f} "
              f"at offset {best[1]:+.2f}s")
        if worst is None or best[0] < worst[0]:
            worst = best
        time.sleep(3)

    subprocess.run(["tmux", "kill-server"], capture_output=True)
    print(f"\nworst IoU {worst[0]:.3f} at offset {worst[1]:+.2f}s")
    print("PASS" if worst[0] > 0.9 and abs(worst[1]) <= 0.25 else "CHECK")


if __name__ == "__main__":
    main()
