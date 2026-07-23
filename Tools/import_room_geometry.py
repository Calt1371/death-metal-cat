#!/usr/bin/env python3
"""
import_room_geometry.py

Takes a room's validated geometry JSON (Tools/room_geometry_<RoomID>.json, produced by
room_geometry_designer.py) and builds the actual room in the currently-open UE5 editor level via
the editor scripting bridge, REPLACING that room's existing flat placeholder floor (and anything
left over from a previous run of this same script) rather than adding alongside it.

Piece -> geometry mapping, walked left-to-right along X starting from the room's RoomShell
origin (accumulating running_x/running_z as each piece is placed):
    flat_run        -> floor, width=length
    ledge_step      -> floor at running_z + height_up, width=length
    gap             -> no geometry, just advances running_x by width
    wall_jump_shaft -> vertical wall (same style as the wall-jump test wall), height=wall_height
                       -- does NOT change running_z (matches the earlier wall-jump testing setup:
                       a single wall standing on one consistent floor level, not a guaranteed
                       transition to a persistently higher walkway)
    drop_down       -> floor at running_z - height_down
    enemy_arena     -> floor, width=width, labeled as an arena segment for Outliner readability
                       (functionally identical to flat_run otherwise)

After all pieces, places one AEncounterSpawnMarker per spawn_markers entry, centered within the
piece it references (after_piece_index), with MarkerID/MarkerType set from the JSON.

Every actor this script creates is named Floor_<RoomID>_##_.../Wall_<RoomID>_##_.../
Marker_<RoomID>_<marker_id> -- re-running it for a room first deletes every existing attached
actor whose label starts with Floor_<RoomID>, Wall_<RoomID>, or Marker_<RoomID> (the original
flat placeholder floor, named exactly Floor_<RoomID>, matches the first of these too), so it's
safe/idempotent to re-run after regenerating a room's JSON -- including re-running --all-rooms
over a room that already has this script's own markers placed, without ending up with
duplicates. Never touches ExitTrigger_*/BiomeEndMarker_*, or any pre-existing EncounterSpawnMarker
placed by hand under a DIFFERENT label (this script's own markers always start with
"Marker_" + the exact room ID, e.g. "Marker_ROOM1_...", which a hand-placed marker following the
"Marker_EnemySpawn_01"-style convention suggested elsewhere would not coincidentally match).

Usage:
    python import_room_geometry.py --room Room4A
    python import_room_geometry.py --all-rooms
"""

import argparse
import json
import os
import sys
import tempfile
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, os.path.join(TOOLS_DIR, "..", "AgentScripts"))

from remote_execution import RemoteExecution
import room_geometry_designer as geo_designer

VALID_ROOMS = ['Room1', 'Room2', 'Room3', 'Room4A', 'Room4B', 'Room5', 'Room6', 'Room7', 'Room8']

# Mirrors room_geometry_designer.py's own nominal-footprint constants for pieces with no width
# param of their own. Kept as separate literals (not imported at runtime inside the editor) since
# the editor-side script embeds them as plain numbers -- the editor's embedded Python has no
# `anthropic` package, so importing room_geometry_designer.py there would fail; this wrapper
# (running in the normal Tools/ Python environment) imports it safely for validate_room only.
WALL_JUMP_SHAFT_NOMINAL_WIDTH = geo_designer.WALL_JUMP_SHAFT_NOMINAL_WIDTH
DROP_DOWN_NOMINAL_WIDTH = geo_designer.DROP_DOWN_NOMINAL_WIDTH

_IMPORT_TEMPLATE = """
import json
import unreal

room_id_name = "__ROOM_ID_NAME__"
json_path = r"__JSON_PATH__"

WALL_JUMP_SHAFT_NOMINAL_WIDTH = __WALL_NOMINAL__
DROP_DOWN_NOMINAL_WIDTH = __DROP_NOMINAL__
CAPSULE_HALF_HEIGHT = 88.0
FLOOR_THICKNESS = 20.0
WALL_DEPTH_Y = 300.0
WALL_THICKNESS_X = 20.0

with open(json_path, "r", encoding="utf-8") as f:
    room = json.load(f)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()

room_shell = next(
    (a for a in all_actors if isinstance(a, unreal.RoomShell) and a.get_actor_label() == "RoomShell_" + room_id_name),
    None,
)
if room_shell is None:
    raise RuntimeError("[IMPORT GEOMETRY] No RoomShell_" + room_id_name + " found in the level")

shell_loc = room_shell.get_actor_location()
REF_Y = shell_loc.y

# ---- Remove existing geometry for this room (the flat placeholder floor, and anything left
# over from a previous run of this script) -- never touches ExitTrigger_*/BiomeEndMarker_*, or
# any pre-existing EncounterSpawnMarker placed by hand. ----
attached = room_shell.get_attached_actors(False, True)
removed_count = 0
for a in attached:
    label = a.get_actor_label()
    if label.startswith("Floor_" + room_id_name) or label.startswith("Wall_" + room_id_name) or label.startswith("Marker_" + room_id_name):
        actor_subsystem.destroy_actor(a)
        removed_count += 1
unreal.log_warning("[IMPORT GEOMETRY] " + room_id_name + ": removed " + str(removed_count) + " old floor/wall/marker actor(s)")

cube_mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")


def place_floor(center_x, z, width, label):
    floor_top_z = z - CAPSULE_HALF_HEIGHT
    floor_center_z = floor_top_z - (FLOOR_THICKNESS / 2.0)
    floor = actor_subsystem.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(center_x, REF_Y, floor_center_z))
    floor.set_actor_label(label)
    floor.static_mesh_component.set_static_mesh(cube_mesh)
    floor.set_actor_scale3d(unreal.Vector(width / 100.0, 3.0, 0.2))
    floor.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    ok = floor.attach_to_actor(room_shell, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    if not ok:
        unreal.log_error("[IMPORT GEOMETRY] Failed to attach " + label)


def place_wall(center_x, base_z, height, label):
    wall_bottom_z = base_z - CAPSULE_HALF_HEIGHT
    wall_center_z = wall_bottom_z + (height / 2.0)
    wall = actor_subsystem.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(center_x, REF_Y, wall_center_z))
    wall.set_actor_label(label)
    wall.static_mesh_component.set_static_mesh(cube_mesh)
    wall.set_actor_scale3d(unreal.Vector(WALL_THICKNESS_X / 100.0, WALL_DEPTH_Y / 100.0, height / 100.0))
    wall.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    ok = wall.attach_to_actor(room_shell, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    if not ok:
        unreal.log_error("[IMPORT GEOMETRY] Failed to attach " + label)


sorted_pieces = sorted(room["pieces"], key=lambda p: p["sequence_index"])

running_x = shell_loc.x
running_z = shell_loc.z
piece_bounds = {}

floor_count = 0
wall_count = 0

for piece in sorted_pieces:
    ptype = piece["type"]
    params = piece["params"]
    idx = piece["sequence_index"]

    if ptype == "flat_run":
        length = params["length"]
        label = "Floor_" + room_id_name + "_" + str(idx).zfill(2) + "_FlatRun"
        place_floor(running_x + length / 2.0, running_z, length, label)
        piece_bounds[idx] = (running_x, running_x + length, running_z)
        running_x += length
        floor_count += 1

    elif ptype == "ledge_step":
        height_up = params["height_up"]
        length = params["length"]
        running_z += height_up
        label = "Floor_" + room_id_name + "_" + str(idx).zfill(2) + "_LedgeStep"
        place_floor(running_x + length / 2.0, running_z, length, label)
        piece_bounds[idx] = (running_x, running_x + length, running_z)
        running_x += length
        floor_count += 1

    elif ptype == "gap":
        width = params["width"]
        piece_bounds[idx] = (running_x, running_x + width, running_z)
        running_x += width

    elif ptype == "wall_jump_shaft":
        wall_height = params["wall_height"]
        label = "Wall_" + room_id_name + "_" + str(idx).zfill(2) + "_WallJumpShaft"
        place_wall(running_x + WALL_JUMP_SHAFT_NOMINAL_WIDTH / 2.0, running_z, wall_height, label)
        piece_bounds[idx] = (running_x, running_x + WALL_JUMP_SHAFT_NOMINAL_WIDTH, running_z)
        running_x += WALL_JUMP_SHAFT_NOMINAL_WIDTH
        wall_count += 1

    elif ptype == "drop_down":
        height_down = params["height_down"]
        running_z -= height_down
        label = "Floor_" + room_id_name + "_" + str(idx).zfill(2) + "_DropDown"
        place_floor(running_x + DROP_DOWN_NOMINAL_WIDTH / 2.0, running_z, DROP_DOWN_NOMINAL_WIDTH, label)
        piece_bounds[idx] = (running_x, running_x + DROP_DOWN_NOMINAL_WIDTH, running_z)
        running_x += DROP_DOWN_NOMINAL_WIDTH
        floor_count += 1

    elif ptype == "enemy_arena":
        width = params["width"]
        label = "Floor_" + room_id_name + "_" + str(idx).zfill(2) + "_EnemyArena"
        place_floor(running_x + width / 2.0, running_z, width, label)
        piece_bounds[idx] = (running_x, running_x + width, running_z)
        running_x += width
        floor_count += 1

unreal.log_warning("[IMPORT GEOMETRY] " + room_id_name + ": placed " + str(floor_count) + " floor(s), " + str(wall_count) + " wall(s)")

marker_count = 0
type_map = {"EnemySpawn": unreal.EncounterMarkerType.ENEMY_SPAWN, "PickupSpawn": unreal.EncounterMarkerType.PICKUP_SPAWN}
for marker in room.get("spawn_markers", []):
    after_idx = marker["after_piece_index"]
    if after_idx not in piece_bounds:
        unreal.log_error("[IMPORT GEOMETRY] spawn_marker " + marker["marker_id"] + " references invalid after_piece_index " + str(after_idx))
        continue
    start_x, end_x, z = piece_bounds[after_idx]
    mid_x = (start_x + end_x) / 2.0

    marker_actor = actor_subsystem.spawn_actor_from_class(unreal.EncounterSpawnMarker, unreal.Vector(mid_x, REF_Y, z))
    marker_actor.set_actor_label("Marker_" + room_id_name + "_" + marker["marker_id"])
    marker_actor.set_editor_property("marker_id", marker["marker_id"])
    marker_actor.set_editor_property("marker_type", type_map[marker["marker_type"]])
    ok = marker_actor.attach_to_actor(room_shell, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    if not ok:
        unreal.log_error("[IMPORT GEOMETRY] Failed to attach marker " + marker["marker_id"])
    marker_count += 1

unreal.log_warning("[IMPORT GEOMETRY] " + room_id_name + ": placed " + str(marker_count) + " spawn marker(s)")

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()

unreal.log_warning("[IMPORT GEOMETRY] SUMMARY room=" + room_id_name + " floors=" + str(floor_count) + " walls=" + str(wall_count) + " markers=" + str(marker_count) + " removed_old=" + str(removed_count) + " final_x=" + str(running_x))
"""


def build_import_command(room_id_name: str, json_path: str) -> str:
    return (
        _IMPORT_TEMPLATE
        .replace("__ROOM_ID_NAME__", room_id_name)
        .replace("__JSON_PATH__", json_path)
        .replace("__WALL_NOMINAL__", repr(WALL_JUMP_SHAFT_NOMINAL_WIDTH))
        .replace("__DROP_NOMINAL__", repr(DROP_DOWN_NOMINAL_WIDTH))
    )


def import_room(remote_exec: RemoteExecution, room: str, timeout: float) -> bool:
    canonical_room = room
    room_id_name = canonical_room.upper()
    json_path = os.path.join(TOOLS_DIR, f"room_geometry_{canonical_room}.json")

    if not os.path.exists(json_path):
        print(f"ERROR: {json_path} does not exist -- generate it with room_geometry_designer.py first.")
        return False

    with open(json_path, "r", encoding="utf-8") as f:
        room_data = json.load(f)

    is_valid, error = geo_designer.validate_room(room_data)
    if not is_valid:
        print(f"ERROR: {json_path} failed validate_room() re-check: {error}")
        return False

    script_body = build_import_command(room_id_name, json_path)

    temp_fd, temp_path = tempfile.mkstemp(suffix=".py", prefix="import_room_geometry_")
    with os.fdopen(temp_fd, "w", encoding="utf-8") as f:
        f.write(script_body)

    command = f"exec(open(r'{temp_path}').read())"

    try:
        node_id = remote_exec.remote_nodes[0]["node_id"]
        remote_exec.open_command_connection(node_id)
        result = remote_exec.run_command(command, unattended=True, exec_mode="ExecuteStatement")
        remote_exec.close_command_connection()
    finally:
        os.remove(temp_path)

    if not result.get("success"):
        print(f"ERROR: editor reported failure for {canonical_room}:\n{result}")
        return False

    had_error = False
    for entry in result.get("output", []):
        print(f"[UE5] {entry.get('type')}: {entry.get('output')}")
        if entry.get("type") == "Error":
            had_error = True

    if had_error:
        print(f"ERROR: {canonical_room} import reported at least one error above.")
        return False

    print(f"Imported geometry for {canonical_room}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Import a room's validated geometry JSON into the live UE5 editor level.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--room", help=f"Single room to import. One of: {', '.join(VALID_ROOMS)}")
    group.add_argument("--all-rooms", action="store_true", help="Import all 9 real rooms (see VALID_ROOMS).")
    parser.add_argument("--timeout", type=float, default=5.0, help="Seconds to wait for the editor to respond to discovery.")
    args = parser.parse_args()

    if args.room:
        room_lookup = {name.lower(): name for name in VALID_ROOMS}
        canonical = room_lookup.get(args.room.lower())
        if canonical is None:
            print(f"ERROR: '{args.room}' is not a valid room. Must be one of: {', '.join(VALID_ROOMS)}")
            return 1
        rooms_to_import = [canonical]
    else:
        rooms_to_import = list(VALID_ROOMS)

    remote_exec = RemoteExecution()
    remote_exec.start()

    try:
        waited = 0.0
        poll_interval = 0.25
        while not remote_exec.remote_nodes and waited < args.timeout:
            time.sleep(poll_interval)
            waited += poll_interval

        if not remote_exec.remote_nodes:
            print("ERROR: No UE5 editor instance found. Is the editor running with Remote "
                  "Execution enabled, and on the same machine/network?")
            return 1

        all_ok = True
        for room in rooms_to_import:
            ok = import_room(remote_exec, room, args.timeout)
            all_ok = all_ok and ok
    finally:
        remote_exec.stop()

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
