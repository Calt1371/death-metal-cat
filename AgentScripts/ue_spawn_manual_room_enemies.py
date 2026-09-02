"""
ue_spawn_manual_room_enemies.py

Generalizes ue_spawn_deathbot_room1.py's one-off manual DeathBot placement into a reusable,
re-runnable pass across every hand-built room. Exists specifically because the hand-built rooms
(Room1-Room6/4A/4B) have no live EncounterSpawnMarker actors and can never safely get any via the
geometry-import pipeline (Tools/import_room_geometry.py -- see Docs/golden_room_script_README.md
for why that's dangerous against these rooms). This bypasses that pipeline entirely: it finds
every AManualEnemySpawnMarker actor placed anywhere in the level (however many, in whatever
rooms), spawns whatever ADeathMetalCatEnemyBase subclass its EnemyClass property points at, at the
marker's exact transform, and attaches the spawned enemy to its room's RoomShell (required for
ARoomShell::SetRoomActive's recursive attached-actor walk to hide/disable it on room deactivation
-- same convention as both spawn_encounter_actors.py and ue_spawn_deathbot_room1.py).

ROOM ASSIGNMENT: a marker isn't required to be attached to its room's RoomShell (hand-dragging an
actor into a viewport doesn't auto-parent it to anything) -- instead, each marker is assigned to
whichever live RoomShell_* actor is nearest to it in plain 3D distance. Every RoomShell in this
level is separated from every other by thousands of units while each room's own span is at most a
few thousand, so nearest-origin is a reliable, zero-setup way to classify a marker dropped
anywhere inside a room's actual geometry -- confirmed against the live room layout before writing
this (ROOM2 @ x=8059 vs ROOM3 @ x=-13712 vs the y=-6270 branch rooms, etc.).

MARKERS WITH NO EnemyClass SET: logged and skipped, not treated as an error -- lets you drop a
marker to mark a spot before deciding what goes there yet, same "honest gap, not a fake" spirit as
spawn_encounter_actors.py's pickup handling.

LABELING / IDEMPOTENCY: every actor this script spawns is labeled
"ManualSpawnedEnemy_<ROOM>_<marker's unique object name>" -- deliberately a different prefix
("ManualSpawnedEnemy_", not "SpawnedEnemy_") from both ue_spawn_deathbot_room1.py's
"SpawnedEnemy_ROOM1_DeathBot_00" and spawn_encounter_actors.py's "SpawnedEnemy_<ROOM>_<marker_id>_##",
so this manual pass can never collide with or accidentally clean up either of those. Every run
first deletes every actor already carrying that prefix (across ALL rooms, not just ones with
markers right now) before respawning fresh from whatever markers currently exist -- safe to
re-run any time after adding, moving, retyping, or deleting markers in the editor.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_spawn_manual_room_enemies.py').read())"
"""

import unreal

MANUAL_SPAWN_PREFIX = "ManualSpawnedEnemy_"

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()

room_shells = [a for a in all_actors if a.get_actor_label().startswith("RoomShell_")]
if not room_shells:
    raise RuntimeError("No RoomShell_* actors found in the level -- is the right level loaded?")

markers = [a for a in all_actors if isinstance(a, unreal.ManualEnemySpawnMarker)]

# ---- Remove every previous manual spawn first (idempotent re-run, see docstring) ----
removed = 0
for a in all_actors:
    if a.get_actor_label().startswith(MANUAL_SPAWN_PREFIX):
        actor_subsystem.destroy_actor(a)
        removed += 1
unreal.log_warning(f"[MANUAL ENEMY SPAWN] removed {removed} previously-spawned manual enemy actor(s)")

# Re-fetch post-deletion so nothing below references a destroyed actor.
all_actors = actor_subsystem.get_all_level_actors()
room_shells = [a for a in all_actors if a.get_actor_label().startswith("RoomShell_")]


def nearest_room_shell(location):
    best, best_dist = None, None
    for shell in room_shells:
        dist = (shell.get_actor_location() - location).length()
        if best_dist is None or dist < best_dist:
            best, best_dist = shell, dist
    return best


spawned = 0
skipped_no_class = 0
per_room_counts = {}

for marker in markers:
    enemy_class = marker.get_editor_property("enemy_class")
    if enemy_class is None:
        unreal.log_warning(f"[MANUAL ENEMY SPAWN] marker '{marker.get_actor_label()}' has no EnemyClass set -- skipped")
        skipped_no_class += 1
        continue

    room_shell = nearest_room_shell(marker.get_actor_location())
    room_label = room_shell.get_actor_label().replace("RoomShell_", "")

    spawn_loc = marker.get_actor_location()
    enemy_actor = actor_subsystem.spawn_actor_from_class(enemy_class, spawn_loc)
    enemy_actor.set_actor_label(f"{MANUAL_SPAWN_PREFIX}{room_label}_{marker.get_name()}")
    ok = enemy_actor.attach_to_actor(
        room_shell, "",
        unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
        False,
    )
    if not ok:
        unreal.log_error(f"[MANUAL ENEMY SPAWN] failed to attach spawned enemy for marker '{marker.get_actor_label()}' to {room_shell.get_actor_label()}")

    spawned += 1
    per_room_counts[room_label] = per_room_counts.get(room_label, 0) + 1

unreal.log_warning(f"[MANUAL ENEMY SPAWN] markers found: {len(markers)}, spawned: {spawned}, skipped (no EnemyClass): {skipped_no_class}")
for room_label, count in sorted(per_room_counts.items()):
    unreal.log_warning(f"[MANUAL ENEMY SPAWN]   {room_label}: {count} enemy(ies)")

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()

unreal.log("=== MANUAL ENEMY SPAWN COMPLETE ===")
