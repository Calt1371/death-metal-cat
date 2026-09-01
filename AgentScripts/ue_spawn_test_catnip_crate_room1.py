"""
ue_spawn_test_catnip_crate_room1.py

TEMPORARY test-only placement: drops one AItemPickupCrate into Room1, right next to Room1's
PlayerStart, for manually testing Cat Nip's "super mode" visuals/behavior in PIE without hunting
down a random-weighted crate or waiting on RNG.

DropTable is EditDefaultsOnly (see ItemPickupCrate.h) -- Python's set_editor_property refuses to
touch it on a placed *instance* ("cannot be edited on instances"), only on the Blueprint's class
defaults (CDO). So this overrides BP_ItemCrate's CDO DropTable to guarantee Cat Nip and saves the
Blueprint -- meaning EVERY crate in the game (not just this test one) will drop guaranteed Cat Nip
until reverted. Acceptable for a short manual test session; revert with
ue_revert_test_catnip_crate_room1.py before real play/other item testing resumes, since that
restores the exact original weighted table from AItemPickupCrate's constructor.

Same hand-placement convention as ue_spawn_deathbot_room1.py: Room1 has no live
EncounterSpawnMarker actors (hand-built level -- see that script's own docstring / CLAUDE.md), so
this spawns directly and attaches to RoomShell_ROOM1 so ARoomShell::SetRoomActive's recursive
attached-actor walk hides/disables it correctly on room deactivation.

Idempotent: re-running this first removes any previous "TestCrate_CatNip_ROOM1" instance.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_spawn_test_catnip_crate_room1.py').read())"
"""

import unreal

ROOM_LABEL = "RoomShell_ROOM1"
CRATE_BP_PATH = "/Game/Items/Crate/BP_ItemCrate"
SPAWN_LABEL = "TestCrate_CatNip_ROOM1"

# Room1's live PlayerStart is at (-190, 100, 632.0001) -- confirmed via live query 2026-08-23
# (ue_spawn_deathbot_room1.py's docstring PlayerStart figure (1180, 60, 632) is stale, the room
# layout has moved since that script was written). Offset a bit off to the side and spawned above
# floor Z so it settles via gravity, same reasoning as that script's own DeathBot placement.
SPAWN_LOC = unreal.Vector(-190.0, 250.0, 700.0)

crate_bp = unreal.EditorAssetLibrary.load_asset(CRATE_BP_PATH)
if crate_bp is None:
    raise RuntimeError(f"Failed to load {CRATE_BP_PATH}")
crate_class = crate_bp.generated_class()
cdo = unreal.get_default_object(crate_class)

original_table = cdo.get_editor_property("drop_table")
unreal.log(f"[TEST CATNIP CRATE] original DropTable had {len(original_table)} entries (see ue_revert_test_catnip_crate_room1.py for the exact values being temporarily replaced)")

cdo.set_editor_property("drop_table", [unreal.CrateDropEntry(type=unreal.PickupResultType.CAT_NIP, weight=1.0)])
unreal.EditorAssetLibrary.save_loaded_asset(crate_bp)
unreal.log("[TEST CATNIP CRATE] BP_ItemCrate CDO DropTable overridden to guaranteed Cat Nip and saved -- ALL crates drop Cat Nip until reverted")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()

room_shell = next((a for a in all_actors if a.get_actor_label() == ROOM_LABEL), None)
if room_shell is None:
    raise RuntimeError(f"No {ROOM_LABEL} found in the level")

removed = 0
for a in room_shell.get_attached_actors(False, True):
    if a.get_actor_label() == SPAWN_LABEL:
        actor_subsystem.destroy_actor(a)
        removed += 1
unreal.log(f"[TEST CATNIP CRATE] removed {removed} previous instance(s)")

crate_actor = actor_subsystem.spawn_actor_from_class(crate_class, SPAWN_LOC)
crate_actor.set_actor_label(SPAWN_LABEL)

ok = crate_actor.attach_to_actor(
    room_shell, "",
    unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
    False,
)
if not ok:
    raise RuntimeError(f"Failed to attach {SPAWN_LABEL} to {ROOM_LABEL}")

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()

unreal.log(f"[TEST CATNIP CRATE] spawned {SPAWN_LABEL} at {SPAWN_LOC}, attached to {ROOM_LABEL}, level saved")
unreal.log("=== TEST CATNIP CRATE COMPLETE ===")
