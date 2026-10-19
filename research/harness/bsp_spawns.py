"""Print a co-op map's spawn points and landmarks, read from the BSP entity lump.

bsp_spawns.py <map name> [...]

Spawns: team, enabled, origin, angles. Landmarks: test-chamber doors with
their distance from the first spawn (the far one is the exit), and the
centroid of the puzzle elements, which is where a camera placed at the exit
should look to show the chamber.
"""
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import GAME

MAP_DIRS = [os.path.join(GAME, folder, "maps") for folder in ("portal2", "portal2_dlc1", "portal2_dlc2")]
PUZZLE = {"prop_floor_button", "prop_button", "prop_laser_catcher", "prop_laser_relay", "prop_weighted_cube",
          "prop_floor_cube_button", "prop_floor_ball_button", "trigger_catapult", "prop_tractor_beam",
          "prop_wall_projector", "prop_monster_box", "env_portal_laser", "info_placement_helper"}


def map_path(map_name):
    for folder in MAP_DIRS:
        path = os.path.join(folder, map_name + ".bsp")
        if os.path.exists(path):
            return path
    raise FileNotFoundError(map_name)


def entities(map_name):
    with open(map_path(map_name), "rb") as handle:
        data = handle.read()
    ident = struct.unpack_from("<4s", data, 0)[0]
    assert ident == b"VBSP", ident
    offset, length = struct.unpack_from("<ii", data, 8)  # lump 0: entities
    text = data[offset:offset + length].decode("latin-1")
    return [dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block)) for block in re.findall(r"\{(.*?)\}", text, re.S)]


def vector(text):
    return tuple(float(v) for v in text.split())


def summary(map_name):
    spawns, doors, puzzle = [], [], []
    for entity in entities(map_name):
        classname = entity.get("classname", "")
        origin = vector(entity.get("origin", "0 0 0")) if "origin" in entity else None
        if classname == "info_coop_spawn":
            spawns.append((entity.get("StartingTeam", "?"), entity.get("Enabled", "?"), origin,
                           vector(entity.get("angles", "0 0 0")), entity.get("targetname", "")))
        elif origin and (classname == "prop_testchamber_door" or
                         (classname == "prop_dynamic" and entity.get("targetname") == "@exit_door")):
            doors.append((origin, vector(entity.get("angles", "0 0 0")), entity.get("targetname", "")))
        elif classname in PUZZLE and origin:
            puzzle.append(origin)
    start = spawns[0][2] if spawns else (0, 0, 0)
    centroid = tuple(sum(p[i] for p in puzzle) / len(puzzle) for i in range(3)) if puzzle else None
    return spawns, doors, puzzle, start, centroid


def look_angles(eye, target):
    dx, dy, dz = (target[i] - eye[i] for i in range(3))
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = -math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    return pitch, yaw


if __name__ == "__main__":
    for map_name in sys.argv[1:]:
        spawns, doors, puzzle, start, centroid = summary(map_name)
        print(f"== {map_name}: {len(puzzle)} puzzle elements, centroid {centroid}")
        for team, enabled, origin, angles, name in spawns:
            print(f"  spawn team {team} enabled {enabled} origin {origin} angles {angles} {name}")
        for origin, angles, name in sorted(doors, key=lambda d: math.dist(d[0], start)):
            look = look_angles(origin, centroid) if centroid else None
            print(f"  door origin {origin} angles {angles} dist {math.dist(origin, start):6.0f} {name} look {look}")
