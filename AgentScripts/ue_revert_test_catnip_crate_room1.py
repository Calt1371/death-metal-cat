"""
ue_revert_test_catnip_crate_room1.py

Undoes ue_spawn_test_catnip_crate_room1.py: restores BP_ItemCrate's CDO DropTable to the exact
original weighted table from AItemPickupCrate::AItemPickupCrate() (ItemPickupCrate.cpp), saves the
Blueprint, then removes the "TestCrate_CatNip_ROOM1" actor from Room1 and saves the level.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_revert_test_catnip_crate_room1.py').read())"
"""

import unreal

ROOM_LABEL = "RoomShell_ROOM1"
CRATE_BP_PATH = "/Game/Items/Crate/BP_ItemCrate"
SPAWN_LABEL = "TestCrate_CatNip_ROOM1"

ORIGINAL_DROP_TABLE = [
    unreal.CrateDropEntry(type=unreal.PickupResultType.SCRAPS, weight=40.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.SCRATCH_PATCH, weight=15.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.NINE_LIFE_STIM, weight=4.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.DEATH_DEFIER, weight=8.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.RAZOR_FANG, weight=6.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.DEADSHOT_ROUNDS, weight=6.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.STEEL_FUR, weight=6.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.FANCY_FEED, weight=5.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.GNARLY_AMP, weight=5.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.MIRROR_CLAW, weight=4.0),
    unreal.CrateDropEntry(type=unreal.PickupResultType.CAT_NIP, weight=1.0),
]

crate_bp = unreal.EditorAssetLibrary.load_asset(CRATE_BP_PATH)
if crate_bp is None:
    raise RuntimeError(f"Failed to load {CRATE_BP_PATH}")
crate_class = crate_bp.generated_class()
cdo = unreal.get_default_object(crate_class)
cdo.set_editor_property("drop_table", ORIGINAL_DROP_TABLE)
unreal.EditorAssetLibrary.save_loaded_asset(crate_bp)
unreal.log("[REVERT TEST CATNIP CRATE] BP_ItemCrate CDO DropTable restored to the original weighted table and saved")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()
removed = 0
for a in all_actors:
    if a.get_actor_label() == SPAWN_LABEL:
        actor_subsystem.destroy_actor(a)
        removed += 1
unreal.log(f"[REVERT TEST CATNIP CRATE] removed {removed} instance(s) of {SPAWN_LABEL}")

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()

unreal.log("=== REVERT TEST CATNIP CRATE COMPLETE ===")
