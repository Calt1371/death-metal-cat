"""
ue_set_deathbot_flying_speed.py

Sets MoveSpeed = 300 on BP_EnemyDeathBotFlying -- half of its actual observed flight speed
(600uu/s, the untouched engine MaxFlySpeed default, since Tick previously only set MaxWalkSpeed
and MOVE_Flying reads MaxFlySpeed instead). This property change alone won't take visible effect
until the paired C++ fix (Tick now also sets MaxFlySpeed = MoveSpeed) is compiled.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_set_deathbot_flying_speed.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/Enemies/DeathBotFlying/Blueprints/BP_EnemyDeathBotFlying"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

before = cdo.get_editor_property("MoveSpeed")
cdo.set_editor_property("MoveSpeed", 300.0)
cdo.modify()
after = cdo.get_editor_property("MoveSpeed")

unreal.log(f"MoveSpeed: {before} -> {after}")

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log("=== DEATHBOT FLYING SPEED SET COMPLETE (needs the paired C++ MaxFlySpeed fix compiled to take effect) ===")
