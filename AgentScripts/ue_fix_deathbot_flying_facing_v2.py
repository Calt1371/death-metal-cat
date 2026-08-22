"""
ue_fix_deathbot_flying_facing_v2.py

Reverts bSpriteFacesReversed to false on BP_EnemyDeathBotFlying, matching the same fix just
confirmed for DeathBotWalking. Confirmed via in-PIE screenshot (player to the right, gun-arm
visibly pointing left) that the new Flying sheets (Idle/Shoot re-imported tonight) also use the
normal orientation convention -- the old reversed-art compensation is now backwards.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_deathbot_flying_facing_v2.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/Enemies/DeathBotFlying/Blueprints/BP_EnemyDeathBotFlying"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

before = cdo.get_editor_property("bSpriteFacesReversed")
cdo.set_editor_property("bSpriteFacesReversed", False)
cdo.modify()

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log(f"bSpriteFacesReversed: {before} -> False")
unreal.log("=== DEATHBOT FLYING FACING FIX v2 COMPLETE ===")
