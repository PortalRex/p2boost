"""Count each game_profile.h signature's matches in every module of two installs.

Usage: python check_signatures.py <portal 2 root> <mod root> [mod game dir]

A signature carries over when both games match it the same way in every
module. Flags the rest, so a mod profile knows which fields it must
override. Struct offsets and vtable slots are not covered; those need the
game running (p2boost_status lists any fix that failed to install).
Signatures a mod profile already overrides (portal_stories_mel.cpp) are
read from that file and checked in their overridden form.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODULES = ["bin/engine.dll", "bin/materialsystem.dll", "bin/shaderapivk.dll", "bin/inputsystem.dll",
           "bin/vguimatsurface.dll", "bin/vgui2.dll", "bin/studiorender.dll", "bin/datacache.dll"]
FIELD = re.compile(r'const char\*\s+(\w+)\{\s*"([0-9A-Fa-f? ]+)"\s*\}')
OVERRIDE = re.compile(r'(\w+)\s*=\s*"([0-9A-Fa-f? ]+)"')


def compile_pattern(text):
    tokens = text.split()
    return re.compile(b"".join(b"." if "?" in token else re.escape(bytes([int(token, 16)])) for token in tokens), re.S)


def counts(root, client, patterns):
    images = {name: open(os.path.join(root, name), "rb").read() for name in MODULES}
    images["client.dll"] = open(os.path.join(root, client), "rb").read()
    result = {}
    for field, pattern in patterns.items():
        hits = {module: len(pattern.findall(data)) for module, data in images.items()}
        result[field] = {module: count for module, count in hits.items() if count}
    return result


def main():
    base_root, mod_root = sys.argv[1], sys.argv[2]
    mod_dir = sys.argv[3] if len(sys.argv) > 3 else "portal_stories"
    header = open(os.path.join(ROOT, "src/engine/game_profile.h"), encoding="utf-8").read()
    base = {name: text for name, text in FIELD.findall(header)}
    mod = dict(base)
    override_path = os.path.join(ROOT, "src/engine/portal_stories_mel.cpp")
    mod.update({name: text for name, text in OVERRIDE.findall(open(override_path, encoding="utf-8").read()) if name in base})

    base_hits = counts(base_root, "portal2/bin/client.dll", {name: compile_pattern(text) for name, text in base.items()})
    mod_hits = counts(mod_root, f"{mod_dir}/bin/client.dll", {name: compile_pattern(text) for name, text in mod.items()})
    bad = 0
    for name in base:
        left, right = base_hits[name], mod_hits[name]
        # the code searches one module (or checks a prologue already located
        # another way), so a signature is fine when both games match it the
        # same way in every module.
        ok = left == right and bool(left)
        if not ok:
            bad += 1
        print(f"{'ok ' if ok else 'BAD'} {name:42} portal2 {left or '{}'}  mod {right or '{}'}")
    print(f"{len(base)} signatures, {bad} need attention")


if __name__ == "__main__":
    main()
