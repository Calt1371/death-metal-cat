"""
ue_set_deathbot_flying_flies_freely.py

Sets bFliesFreely = true on BP_EnemyDeathBotFlying only. This switches it to MOVE_Flying with
zero gravity (BeginPlay) and extends its chase movement to a normalized XZ direct-approach toward
the player instead of the X-only ground behavior (Tick). BP_EnemyDeathBotWalking and BP_EnemyBase
are deliberately left at the property's default (false) -- their movement stays byte-for-byte
unchanged.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_set_deathbot_flying_flies_freely.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/Enemies/DeathBotFlying/Blueprints/BP_EnemyDeathBotFlying"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

before = cdo.get_editor_property("bFliesFreely")
cdo.set_editor_property("bFliesFreely", True)
cdo.modify()
after = cdo.get_editor_property("bFliesFreely")

unreal.log(f"bFliesFreely: {before} -> {after}")

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log("=== DEATHBOT FLYING FREE-FLIGHT FIX COMPLETE ===")
