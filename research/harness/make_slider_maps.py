"""Build the five-map before/after slider page from coop_maps.py output.

make_slider_maps.py <results dir> <out.html>
"""
import base64
import io
import json
import os
import sys

from PIL import Image

root, out = sys.argv[1:3]
DEEP = "--deep" in sys.argv  # captures taken at each chamber's exit, not the elevators
# red_racer's exit is an airlock whose pushers and map-end logic move the players, so it stays at the start
WHERE = {"mp_coop_paint_red_racer": ", at the arrival elevators (its exit airlock resets the players)"}
scenes = json.load(open(os.path.join(root, "scenes.json")))


def data_uri(path, width=1600, quality=80):
    image = Image.open(path).convert("RGB")
    image = image.resize((width, int(image.height * width / image.width)), Image.LANCZOS)
    buffer = io.BytesIO()
    image.save(buffer, "JPEG", quality=quality, optimize=True)
    return "data:image/jpeg;base64," + base64.b64encode(buffer.getvalue()).decode()


blocks, rows = [], []
for key, scene in scenes.items():
    before, after = round(scene["vanilla"]), round(scene["optimized"])
    gain = round((scene["optimized"] / scene["vanilla"] - 1) * 100)
    a = data_uri(os.path.join(root, f"{key}_vanilla.jpg"))
    b = data_uri(os.path.join(root, f"{key}_optimized.jpg"))
    blocks.append(f'''
<section class="compare" id="{key.replace('mp_coop_', '')}">
  <header class="compare-head">
    <h2>{scene["title"]}</h2>
    <p class="where"><code>ss_map {key}</code>, split-screen, both players' views{WHERE.get(key, ", at the chamber exit looking back over the puzzle") if DEEP else ""}</p>
  </header>
  <div class="stage" data-compare>
    <img class="after" src="{b}" alt="{scene['title']}, with p2boost" draggable="false">
    <div class="before-wrap"><img class="before" src="{a}" alt="{scene['title']}, vanilla" draggable="false"></div>
    <div class="handle"><span class="knob">&#9664;&#9654;</span></div>
    <input class="range" id="range-{key}" type="range" min="0" max="100" value="50" aria-label="Compare {scene['title']}">
    <div class="tag tag-l"><span>Vanilla</span><strong>{before} FPS</strong></div>
    <div class="tag tag-r"><span>p2boost, split view 2 at 120 Hz</span><strong>{after} FPS</strong></div>
  </div>
  <p class="meta">Gain {gain}%. p99 frame time {scene["vanilla_p99"]:.2f} ms vs {scene["optimized_p99"]:.2f} ms.</p>
</section>''')
    rows.append(f'<tr><td>{scene["title"]}</td><td class="n">{before}</td><td class="n">{after}</td>'
                f'<td class="n">{scene["vanilla_p99"]:.2f} ms</td><td class="n">{scene["optimized_p99"]:.2f} ms</td><td class="n gain">+{gain}%</td></tr>')

template = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "slider_template.html"), encoding="utf-8").read()
head, _, tail = template.partition("<!--SCENES-->")
# keep the template's styles and script; replace the intro and the table
heading = "p2boost across five co-op maps" + (", deep in each chamber" if DEEP else "")
where = ("Both players were placed at each chamber's exit door, looking back over the puzzle, and stood still."
         if DEEP else "Both players walked a few steps out of the arrival elevators and stood still.")
head = head.split("<div class=\"wrap\">")[0] + "<div class=\"wrap\">\n  <h1>" + heading + "</h1>\n" \
    "  <p class=\"lede\">Drag each handle. Left of it is vanilla Portal 2 (<code>mat_queue_mode 0</code>, no plugin fixes); right of it is the same frame with every p2boost optimization that gets split-screen past 700 FPS: all fixes, depth-map refresh 120, <code>snd_mix_async 1</code>, and player 2's view capped at 120 Hz. " + where + " RTX 5090, 7800X3D, 2560x1440.</p>\n"
table = ('  <div class="table"><table><thead><tr><th>Map</th><th class="n">Vanilla FPS</th><th class="n">p2boost FPS</th>'
         '<th class="n">Vanilla p99</th><th class="n">p2boost p99</th><th class="n">Gain</th></tr></thead><tbody>'
         + "".join(rows) + '</tbody></table></div>\n'
         '  <p class="note">Each FPS value is the mean of two 8-second frame traces per setting, taken in ABBA order at <code>fps_max 0</code>, with the game driven over its network console. The screenshots were taken afterwards at <code>fps_max 999</code> so the live counter (top-right of each half) is readable.</p>\n</div>\n')
script = tail[tail.index("<script>"):]
title = "<title>p2boost Deep Chamber Comparison</title>" if DEEP else "<title>p2boost Co-op Map Comparison</title>"
html = head.replace("<title>p2boost Before and After</title>", title) + "".join(blocks) + "\n" + table + script
open(out, "w", encoding="utf-8").write(html)
print("wrote", out, os.path.getsize(out) // 1024, "KB")
