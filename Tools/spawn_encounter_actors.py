#!/usr/bin/env python3
"""
spawn_encounter_actors.py

Closes the identified gap between Level & Encounter Designer's population DECISIONS
(Tools/encounter_population_<RoomID>.json) and real actors in the live UE5 level -- until now,
nothing actually spawned an enemy or pickup at a populated AEncounterSpawnMarker
(EncounterSpawnMarker.h is explicit that markers are "purely a data marker with no spawn logic of
its own"). Same pattern as import_room_geometry.py: read the JSON, find the real placed marker
actor in the live level, spawn the appropriate class at its transform.

MARKER_ID -> LIVE ACTOR MAPPING (confirmed against the real level before building, not assumed):
marker_id has zero consistent naming convention across rooms (spawn_0 in Room1, SM_Room4B_01 in
Room4B, sm_01 in Room8 -- see prior schema audit), so this never parses/matches on actor label.
Instead it matches on the marker_id EDITOR PROPERTY -- import_room_geometry.py already sets this
property directly from the same JSON string when it places each AEncounterSpawnMarker
(marker_actor.set_editor_property("marker_id", marker["marker_id"])), so querying every
EncounterSpawnMarker attached to a room's RoomShell and matching on that property works
identically for all 9 rooms regardless of naming convention. Confirmed via live query against
Room1 (all 4 markers' marker_id properties matched their JSON entries exactly) before writing
this script.

PICKUP SPAWNING: population=="pickup" markers now spawn BP_ItemCrate (AItemPickupCrate) at the
marker's exact transform, same attach-to-RoomShell/idempotent-cleanup convention as enemies
(labeled "SpawnedPickup_<ROOM>_<marker_id>"). The crate itself rolls its own weighted drop table
(Scraps vs. one of the special items) on player overlap -- this script only places it, same
division of responsibility as enemy spawning (this script places BP_EnemyBase, the enemy's own
Tick/TakeDamage decide what happens next).

ENEMY SPAWNING: spawns BP_EnemyBase (the Blueprint subclass of ADeathMetalCatEnemyBase that
actually has its placeholder mesh/material configured -- not the raw C++ class) at the marker's
exact transform. When enemy_count > 1, additional instances are offset along X by
ENEMY_SPAWN_OFFSET_X, centered on the marker (e.g. count=3 -> offsets -80/0/+80) so their capsules
don't spawn exactly stacked on each other -- a simple, deterministic pattern, not physics-based
separation.

EMPTY MARKERS: no actor spawned, and the marker itself needs no cleanup -- AEncounterSpawnMarker
derives from ATargetPoint (renders as nothing, no gameplay footprint), and its own doc comment
already treats "no matching JSON entry" as the normal, harmless empty case. Population=="empty"
markers are exactly that same harmless case, just arrived at explicitly instead of by omission.

ROOM ACTIVATION: spawned enemies are attached to the room's RoomShell (same as floors/walls/
markers, see import_room_geometry.py) -- ARoomShell::SetRoomActive recurses over
GetAttachedActors(..., true) (recursive), so this is required for spawned enemies to correctly
hide/disable-collision when their room deactivates, not just cosmetic parenting.

IDEMPOTENCY: every actor this script spawns is labeled SpawnedEnemy_<ROOM_ID>_<marker_id>_## --
re-running for a room first deletes every existing attached actor whose label starts with
"SpawnedEnemy_<ROOM_ID>_", the same removal-before-rebuild idiom import_room_geometry.py already
uses for Floor_/Wall_/Marker_, so re-running after regenerating a room's population JSON never
duplicates spawned enemies.

Usage:
    python spawn_encounter_actors.py --room Room1
    python spawn_encounter_actors.py --all-rooms
"""

import argparse
import json
import os
import sys
import tempfile
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(TOOLS_DIR, "..", "AgentScripts"))

from remote_execution import RemoteExecution

VALID_ROOMS = ['Room1', 'Room2', 'Room3', 'Room4A', 'Room4B', 'Room5', 'Room6', 'Room7', 'Room8']

ENEMY_BP_PATH = "/Game/Characters/DeathMetalCat/Blueprints/BP_EnemyBase"
PICKUP_BP_PATH = "/Game/Items/Crate/BP_ItemCrate"

# Separates multiple simultaneously-spawned enemies from the same marker so their capsules don't
# spawn exactly stacked -- a simple, deterministic offset pattern, not physics-based separation.
ENEMY_SPAWN_OFFSET_X = 80.0

_SPAWN_TEMPLATE = """
import json
import unreal

room_id_name = "__ROOM_ID_NAME__"
json_path = r"__JSON_PATH__"
enemy_bp_path = "__ENEMY_BP_PATH__"
pickup_bp_path = "__PICKUP_BP_PATH__"
offset_x = __OFFSET_X__

with open(json_path, "r", encoding="utf-8") as f:
    population_data = json.load(f)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()

room_shell = next(
    (a for a in all_actors if isinstance(a, unreal.RoomShell) and a.get_actor_label() == "RoomShell_" + room_id_name),
    None,
)
if room_shell is None:
    raise RuntimeError("[SPAWN ENCOUNTER] No RoomShell_" + room_id_name + " found in the level")

attached = room_shell.get_attached_actors(False, True)

# ---- Remove any previously-spawned enemies/pickups for this room (idempotent re-run) ----
removed_count = 0
removed_pickup_count = 0
for a in attached:
    if a.get_actor_label().startswith("SpawnedEnemy_" + room_id_name + "_"):
        actor_subsystem.destroy_actor(a)
        removed_count += 1
    elif a.get_actor_label().startswith("SpawnedPickup_" + room_id_name + "_"):
        actor_subsystem.destroy_actor(a)
        removed_pickup_count += 1
unreal.log_warning("[SPAWN ENCOUNTER] " + room_id_name + ": removed " + str(removed_count) + " previously-spawned enemy actor(s), " + str(removed_pickup_count) + " previously-spawned pickup actor(s)")

# Re-fetch attached actors post-deletion so marker lookups below don't reference destroyed actors.
attached = room_shell.get_attached_actors(False, True)
markers_by_id = {}
for a in attached:
    if isinstance(a, unreal.EncounterSpawnMarker):
        markers_by_id[a.get_editor_property("marker_id")] = a

enemy_bp = unreal.EditorAssetLibrary.load_asset(enemy_bp_path)
if enemy_bp is None:
    raise RuntimeError("[SPAWN ENCOUNTER] Failed to load " + enemy_bp_path)
enemy_class = enemy_bp.generated_class()

pickup_bp = unreal.EditorAssetLibrary.load_asset(pickup_bp_path)
if pickup_bp is None:
    raise RuntimeError("[SPAWN ENCOUNTER] Failed to load " + pickup_bp_path)
pickup_class = pickup_bp.generated_class()

spawned_enemy_count = 0
spawned_pickup_count = 0
empty_count = 0
missing_marker_count = 0

for entry in population_data.get("populations", []):
    marker_id = entry["marker_id"]
    population = entry["population"]

    marker = markers_by_id.get(marker_id)
    if marker is None:
        unreal.log_error("[SPAWN ENCOUNTER] marker_id " + str(marker_id) + " has no matching live EncounterSpawnMarker -- skipped")
        missing_marker_count += 1
        continue

    if population == "empty":
        empty_count += 1
        continue

    if population == "pickup":
        # AItemPickupCrate (BP_ItemCrate) now exists -- spawns one crate at the marker's exact
        # transform, same attach-to-RoomShell convention as enemies. Always exactly one per
        # marker (unlike enemy_count, pickups have no count field -- see level_encounter_designer.py's
        # schema), so no ENEMY_SPAWN_OFFSET_X-style spacing is needed.
        marker_loc = marker.get_actor_location()
        pickup_actor = actor_subsystem.spawn_actor_from_class(pickup_class, marker_loc)
        pickup_actor.set_actor_label("SpawnedPickup_" + room_id_name + "_" + str(marker_id))
        ok = pickup_actor.attach_to_actor(room_shell, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
        if not ok:
            unreal.log_error("[SPAWN ENCOUNTER] Failed to attach SpawnedPickup_" + room_id_name + "_" + str(marker_id))
        spawned_pickup_count += 1
        continue

    if population == "enemy":
        marker_loc = marker.get_actor_location()
        count = entry["enemy_count"]
        for i in range(count):
            spawn_x = marker_loc.x + (i - (count - 1) / 2.0) * offset_x
            spawn_loc = unreal.Vector(spawn_x, marker_loc.y, marker_loc.z)
            enemy_actor = actor_subsystem.spawn_actor_from_class(enemy_class, spawn_loc)
            enemy_actor.set_actor_label("SpawnedEnemy_" + room_id_name + "_" + str(marker_id) + "_" + str(i).zfill(2))
            ok = enemy_actor.attach_to_actor(room_shell, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
            if not ok:
                unreal.log_error("[SPAWN ENCOUNTER] Failed to attach SpawnedEnemy_" + room_id_name + "_" + str(marker_id) + "_" + str(i).zfill(2))
            spawned_enemy_count += 1

unreal.log_warning(
    "[SPAWN ENCOUNTER] " + room_id_name + ": spawned " + str(spawned_enemy_count) + " enemy actor(s), "
    + str(spawned_pickup_count) + " pickup crate(s), "
    + str(empty_count) + " empty marker(s) left untouched, "
    + str(missing_marker_count) + " marker(s) not found"
)

dirty_before_save = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
unreal.log_warning("[SPAWN ENCOUNTER] dirty map packages before save: " + str(len(dirty_before_save)))

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()

dirty_after_save = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
unreal.log_warning("[SPAWN ENCOUNTER] dirty map packages after save: " + str(len(dirty_after_save)))
unreal.log_warning("[SPAWN ENCOUNTER] SUMMARY room=" + room_id_name + " spawned_enemies=" + str(spawned_enemy_count) + " skipped_pickups=" + str(skipped_pickup_count) + " empty=" + str(empty_count) + " missing_markers=" + str(missing_marker_count) + " removed_old=" + str(removed_count))
"""


def build_spawn_command(room_id_name: str, json_path: str) -> str:
    return (
        _SPAWN_TEMPLATE
        .replace("__ROOM_ID_NAME__", room_id_name)
        .replace("__JSON_PATH__", json_path)
        .replace("__ENEMY_BP_PATH__", ENEMY_BP_PATH)
        .replace("__PICKUP_BP_PATH__", PICKUP_BP_PATH)
        .replace("__OFFSET_X__", repr(ENEMY_SPAWN_OFFSET_X))
    )


def spawn_room(remote_exec: RemoteExecution, room: str, timeout: float) -> bool:
    room_id_name = room.upper()
    json_path = os.path.join(TOOLS_DIR, f"encounter_population_{room}.json")

    if not os.path.exists(json_path):
        print(f"ERROR: {json_path} does not exist -- generate it with level_encounter_designer.py first.")
        return False

    script_body = build_spawn_command(room_id_name, json_path)

    temp_fd, temp_path = tempfile.mkstemp(suffix=".py", prefix="spawn_encounter_actors_")
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
        print(f"ERROR: editor reported failure for {room}:\n{result}")
        return False

    had_error = False
    for entry in result.get("output", []):
        print(f"[UE5] {entry.get('type')}: {entry.get('output')}")
        if entry.get("type") == "Error":
            had_error = True

    if had_error:
        print(f"ERROR: {room} spawn pass reported at least one error above.")
        return False

    print(f"Spawned encounter actors for {room}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Spawn real actors in the live UE5 editor level from Level & Encounter Designer's population JSON.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--room", help=f"Single room to spawn. One of: {', '.join(VALID_ROOMS)}")
    group.add_argument("--all-rooms", action="store_true", help="Spawn all 9 real rooms.")
    parser.add_argument("--timeout", type=float, default=5.0, help="Seconds to wait for the editor to respond to discovery.")
    args = parser.parse_args()

    if args.room:
        room_lookup = {name.lower(): name for name in VALID_ROOMS}
        canonical = room_lookup.get(args.room.lower())
        if canonical is None:
            print(f"ERROR: '{args.room}' is not a valid room. Must be one of: {', '.join(VALID_ROOMS)}")
            return 1
        rooms_to_run = [canonical]
    else:
        rooms_to_run = list(VALID_ROOMS)

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
        for room in rooms_to_run:
            ok = spawn_room(remote_exec, room, args.timeout)
            all_ok = all_ok and ok
    finally:
        remote_exec.stop()

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
