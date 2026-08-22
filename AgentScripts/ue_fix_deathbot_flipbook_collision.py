"""
ue_fix_deathbot_flipbook_collision.py

BP_EnemyDeathBotWalking's Blueprint-added EnemyFlipbook (PaperFlipbookComponent) was left with
default collision (Query+Physics, blocking WorldStatic) when it was added -- a real, world-blocking
second collider riding along on the capsule, fighting it for floor contact and causing the enemy to
randomly bounce while walking. PlaceholderMesh (the inherited native cylinder from
ADeathMetalCatEnemyBase) was already correctly configured as visual-only (NoCollision, no overlap
events) in the C++ constructor -- this brings EnemyFlipbook in line with that same pattern, entirely
on the Blueprint side. Does NOT touch DeathMetalCatEnemyBase.h/.cpp.

Edits the SCS node's component TEMPLATE (not a spawned instance's CDO), which is what the
Blueprint editor's Components panel actually persists -- setting properties on a spawned/temp
instance never survives a save.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_deathbot_flipbook_collision.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/Enemies/DeathBotWalking/Blueprints/BP_EnemyDeathBotWalking"
TARGET_NODE_NAME = "EnemyFlipbook"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
if bp is None:
    raise RuntimeError(f"Could not load {BP_PATH}")

subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib = unreal.SubobjectDataBlueprintFunctionLibrary
handles = subsystem.k2_gather_subobject_data_for_blueprint(bp)

template = None
for h in handles:
    data = subsystem.k2_find_subobject_data_from_handle(h)
    if lib.get_variable_name(data) == TARGET_NODE_NAME:
        template = lib.get_object_for_blueprint(data, bp)
        break

if template is None:
    node_names = [str(lib.get_variable_name(subsystem.k2_find_subobject_data_from_handle(h))) for h in handles]
    raise RuntimeError(f"No subobject named {TARGET_NODE_NAME} found. Nodes present: {node_names}")

before_collision = template.get_collision_enabled()
before_overlap = template.get_editor_property("generate_overlap_events")
unreal.log(f"[FIX] {TARGET_NODE_NAME} BEFORE: collision_enabled={before_collision} generate_overlap_events={before_overlap}")

template.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
template.set_editor_property("generate_overlap_events", False)
template.modify()

after_collision = template.get_collision_enabled()
after_overlap = template.get_editor_property("generate_overlap_events")
unreal.log(f"[FIX] {TARGET_NODE_NAME} AFTER: collision_enabled={after_collision} generate_overlap_events={after_overlap}")

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log("=== DEATHBOT FLIPBOOK COLLISION FIX COMPLETE ===")
