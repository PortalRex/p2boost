"""Build the before/after slider page with embedded frames.

make_slider.py <capture root> <out.html>
"""
import base64
import io
import os
import sys

from PIL import Image

root, out = sys.argv[1:3]


def data_uri(path, width=1600, quality=82):
    image = Image.open(path).convert("RGB")
    image = image.resize((width, int(image.height * width / image.width)), Image.LANCZOS)
    buffer = io.BytesIO()
    image.save(buffer, "JPEG", quality=quality, optimize=True)
    return "data:image/jpeg;base64," + base64.b64encode(buffer.getvalue()).decode()


SCENES = [
    ("split", "Split-screen co-op lobby", "ss_map mp_coop_lobby_3, both players' views rendered", 158, 704,
     "with p2boost_fix partner_view_fps 120"),
    ("single", "One view", "sp_a2_laser_relays, the co-op host without +remote_view", 346, 953, ""),
]
template = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "slider_template.html"), encoding="utf-8").read()
blocks = []
for key, name, where, before, after, note in SCENES:
    a = data_uri(os.path.join(root, key, "vanilla_06.jpg"))
    b = data_uri(os.path.join(root, key, "optimized_06.jpg"))
    blocks.append(f'''
<section class="compare" id="{key}">
  <header class="compare-head">
    <h2>{name}</h2>
    <p class="where">{where}</p>
  </header>
  <div class="stage" data-compare>
    <img class="after" src="{b}" alt="{name}, with p2boost" draggable="false">
    <div class="before-wrap"><img class="before" src="{a}" alt="{name}, vanilla" draggable="false"></div>
    <div class="handle"><span class="knob">&#9664;&#9654;</span></div>
    <input class="range" id="range-{key}" type="range" min="0" max="100" value="50" aria-label="Compare {name}">
    <div class="tag tag-l"><span>Vanilla</span><strong>{before} FPS</strong></div>
    <div class="tag tag-r"><span>p2boost{(" " + note) if note else ""}</span><strong>{after} FPS</strong></div>
  </div>
  <p class="meta">Averages over 10-second frame traces at the same spot, <code>fps_max 0</code>. The screenshots were taken at <code>fps_max 999</code> so the live counter is visible top-right. Gain: {round((after / before - 1) * 100)}%.</p>
</section>''')
html = template.replace("<!--SCENES-->", "".join(blocks))
open(out, "w", encoding="utf-8").write(html)
print("wrote", out, os.path.getsize(out) // 1024, "KB")
