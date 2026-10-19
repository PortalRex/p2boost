"""Compose a before/after slider-wipe video from two screenshot sequences.

compose.py <scene dir> <out.mp4> <title> <left fps> <right fps> <config line>
"""
import glob
import math
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

scene, out, title, left_fps, right_fps, config = sys.argv[1:7]
W, H, FPS, SECONDS = 1920, 1080, 30, 9
FONTS = ["C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"]


def font(size):
    for path in FONTS:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


COUNTER = (2290, 0, 2560, 58)  # p2boost_overlay 2 counter, top-right of a 2560x1440 frame


def load(label):
    frames, counters = [], []
    for path in sorted(glob.glob(os.path.join(scene, f"{label}_*.jpg"))):
        image = Image.open(path).convert("RGB")
        counters.append(image.crop(COUNTER).resize((405, 87), Image.LANCZOS))
        frames.append(image.resize((W, H), Image.LANCZOS))
    return frames, counters


def wrap(draw, text, fnt, width):
    lines, line = [], ""
    for word in text.split():
        trial = (line + " " + word).strip()
        if draw.textlength(trial, font=fnt) > width and line:
            lines.append(line)
            line = word
        else:
            line = trial
    return lines + [line]


(left_frames, left_counters), (right_frames, right_counters) = load("vanilla"), load("optimized")
assert left_frames and right_frames, "missing frames"
big, mid, small = font(64), font(34), font(26)
tmp = tempfile.mkdtemp(prefix="p2b_")


def label(frame, draw, x, y, head, number, anchor, counter):
    text = f"{head}   {number} FPS"
    tw = max(draw.textlength(text, font=big), 405)
    pad = 24
    box_w, box_h = tw + 2 * pad, 64 + 87 + 3 * pad
    bx = x if anchor == "l" else x - box_w
    draw.rounded_rectangle((bx, y, bx + box_w, y + box_h), radius=18, fill=(12, 12, 14, 215))
    draw.text((bx + pad, y + pad - 8), head, font=big, fill=(200, 200, 205))
    hw = draw.textlength(head + "   ", font=big)
    draw.text((bx + pad + hw, y + pad - 8), f"{number} FPS", font=big, fill=(255, 190, 60) if anchor == "r" else (255, 255, 255))
    draw.text((bx + pad, y + pad + 60), "avg, 10 s frame trace", font=small, fill=(150, 150, 158))
    draw.text((bx + pad, y + pad + 92), "live in-game counter:", font=small, fill=(150, 150, 158))
    frame.paste(counter, (int(bx + pad), int(y + pad + 124)))


for n in range(FPS * SECONDS):
    t = n / FPS
    cut = 0.5 + 0.38 * math.sin(2 * math.pi * t / 6.0)
    cx = int(W * cut)
    index = int(t * 2) % len(left_frames)
    frame = right_frames[index % len(right_frames)].copy()
    frame.paste(left_frames[index].crop((0, 0, cx, H)), (0, 0))
    overlay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    # title band, leaving the top-right counter uncovered
    lines = wrap(draw, config, small, W - 420 - 64)
    band_h = 62 + 30 * len(lines)
    draw.rectangle((0, 0, W - 420, band_h), fill=(10, 10, 12, 200))
    draw.text((32, 16), title, font=mid, fill=(240, 240, 240))
    for k, line in enumerate(lines):
        draw.text((32, 58 + 30 * k), line, font=small, fill=(160, 160, 168))
    # handle
    draw.rectangle((cx - 2, 0, cx + 2, H), fill=(255, 255, 255, 235))
    draw.ellipse((cx - 34, H // 2 - 34, cx + 34, H // 2 + 34), fill=(255, 255, 255, 240))
    draw.polygon([(cx - 22, H // 2), (cx - 8, H // 2 - 12), (cx - 8, H // 2 + 12)], fill=(20, 20, 24))
    draw.polygon([(cx + 22, H // 2), (cx + 8, H // 2 - 12), (cx + 8, H // 2 + 12)], fill=(20, 20, 24))
    frame = Image.alpha_composite(frame.convert("RGBA"), overlay).convert("RGB")
    draw = ImageDraw.Draw(frame, "RGBA")
    label(frame, draw, 32, H - 270, "VANILLA", left_fps, "l", left_counters[index])
    label(frame, draw, W - 32, H - 270, "p2boost", right_fps, "r", right_counters[index % len(right_counters)])
    frame.save(os.path.join(tmp, f"f{n:04d}.png"))

subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", os.path.join(tmp, "f%04d.png"),
                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "17", "-movflags", "+faststart", out], check=True)
shutil.rmtree(tmp, ignore_errors=True)
print("wrote", out, os.path.getsize(out) // 1024, "KB")
