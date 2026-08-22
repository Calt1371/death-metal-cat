"""
ue_fix_enemy_flipbook_scale.py

Corrects EnemyFlipbook's RelativeScale3D on BP_EnemyDeathBotWalking and BP_EnemyDeathBotFlying
after tonight's re-imports shrank all their sprite sheets down to a uniform 256x256/frame grid.
The component's scale was still tuned for the OLD, much-higher-resolution per-frame sprites
(Walking ~1122x1402px, Flying ~397x396px) -- Paper2D bakes render-quad size directly from each
sprite's pixel dimensions, so leaving the old scale in place made every re-imported enemy render
noticeably smaller. New values preserve each enemy's previous on-screen height:
  Walking: 0.192479 * (1402/256) = 1.054
  Flying:  0.5 * (396/256) = 0.773

Edits the SCS component TEMPLATE (not a spawned instance's CDO) via SubobjectDataSubsystem, same
approach as tonight's earlier EnemyFlipbook collision fix -- this is what the Blueprint editor's
Components panel actually persists.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_enemy_flipbook_scale.py').read())"
"""

import unreal

TARGETS = [
    ("/Game/Characters/Enemies/DeathBotWalking/Blueprints/BP_EnemyDeathBotWalking", 1.054),
    ("/Game/Characters/Enemies/DeathBotFlying/Blueprints/BP_EnemyDeathBotFlying", 0.773),
]

subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib = unreal.SubobjectDataBlueprintFunctionLibrary

for bp_path, new_scale in TARGETS:
    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(bp)

    template = None
    for h in handles:
        data = subsystem.k2_find_subobject_data_from_handle(h)
        if lib.get_variable_name(data) == "EnemyFlipbook":
            template = lib.get_object_for_blueprint(data, bp)
            break

    if template is None:
        unreal.log_error(f"[SCALE FIX] No EnemyFlipbook found on {bp_path}")
        continue

    before = template.get_editor_property("relative_scale3d")
    template.set_editor_property("relative_scale3d", unreal.Vector(new_scale, new_scale, new_scale))
    template.modify()

    after = template.get_editor_property("relative_scale3d")
    unreal.log(f"[SCALE FIX] {bp_path}: {before} -> {after}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log("=== ENEMY FLIPBOOK SCALE FIX COMPLETE ===")
