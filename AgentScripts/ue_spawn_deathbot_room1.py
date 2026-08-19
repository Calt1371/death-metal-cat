"""
ue_spawn_deathbot_room1.py

Hand-places one BP_EnemyDeathBotWalking into Room1's live level, attached to RoomShell_ROOM1
(required so ARoomShell::SetRoomActive's recursive attached-actor walk correctly hides/disables it
when the room deactivates -- same convention spawn_encounter_actors.py uses for pipeline-spawned
enemies).

NOT run through spawn_encounter_actors.py / the EncounterSpawnMarker pipeline: Room1 was
hand-built directly in the editor and has no live EncounterSpawnMarker actors (the JSON pipeline
that would create them is explicitly documented as dangerous to run against Room1-Room6, since it
would delete the real hand-placed floor geometry -- see Docs/golden_room_script_README.md). This
is a one-off manual placement instead, same "spawn via the live Python bridge" mechanism, just
without a marker to read a transform from.

Position: X=850, Y=60 -- offset from Room1's PlayerStart (1180, 60, 632, confirmed walkable) along
the same flat stretch near the room's second half, roughly matching where the original Room Geometry
Designer blockout (Tools/room_geometry_Room1.json) placed its second EnemySpawn marker (spawn_2,
after the second enemy_arena piece) before Room1 was hand-built with real art. Spawned 118 units
above that reference Z (750 instead of 632) rather than exactly on it, so the capsule settles onto
whatever the real floor surface is via normal gravity instead of risking spawning embedded in it.

Idempotent, same removal-before-rebuild idiom as spawn_encounter_actors.py: re-running this
deletes any existing actor labeled "SpawnedEnemy_ROOM1_DeathBot_00" first.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_spawn_deathbot_room1.py').read())"
"""

import unreal

ROOM_LABEL = "RoomShell_ROOM1"
ENEMY_BP_PATH = "/Game/Characters/Enemies/DeathBotWalking/Blueprints/BP_EnemyDeathBotWalking"
SPAWN_LABEL = "SpawnedEnemy_ROOM1_DeathBot_00"
SPAWN_LOC = unreal.Vector(850.0, 60.0, 750.0)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()

room_shell = next((a for a in all_actors if a.get_actor_label() == ROOM_LABEL), None)
if room_shell is None:
    raise RuntimeError(f"No {ROOM_LABEL} found in the level")

# Idempotent re-run: remove any previous instance of this exact spawn first.
removed = 0
for a in room_shell.get_attached_actors(False, True):
    if a.get_actor_label() == SPAWN_LABEL:
        actor_subsystem.destroy_actor(a)
        removed += 1
unreal.log(f"[SPAWN DEATHBOT] removed {removed} previous instance(s)")

enemy_bp = unreal.EditorAssetLibrary.load_asset(ENEMY_BP_PATH)
if enemy_bp is None:
    raise RuntimeError(f"Failed to load {ENEMY_BP_PATH}")
enemy_class = enemy_bp.generated_class()

enemy_actor = actor_subsystem.spawn_actor_from_class(enemy_class, SPAWN_LOC)
enemy_actor.set_actor_label(SPAWN_LABEL)
ok = enemy_actor.attach_to_actor(
    room_shell, "",
    unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
    False,
)
if not ok:
    raise RuntimeError(f"Failed to attach {SPAWN_LABEL} to {ROOM_LABEL}")

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()

unreal.log(f"[SPAWN DEATHBOT] spawned {SPAWN_LABEL} at {SPAWN_LOC}, attached to {ROOM_LABEL}, level saved")
unreal.log("=== SPAWN DEATHBOT COMPLETE ===")
