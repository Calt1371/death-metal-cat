"""
ue_set_deathbot_flying_reversed_facing.py

Sets bSpriteFacesReversed = true on BP_EnemyDeathBotFlying -- confirmed in-game (same symptom as
DeathBotWalking: visibly faces away from the player while chasing) that this Blueprint's sprite art
shares the same reversed-default-orientation convention. Uses the same per-Blueprint override
already added to ADeathMetalCatEnemyBase for DeathBotWalking; no C++ change needed.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_set_deathbot_flying_reversed_facing.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/Enemies/DeathBotFlying/Blueprints/BP_EnemyDeathBotFlying"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

before = cdo.get_editor_property("bSpriteFacesReversed")
cdo.set_editor_property("bSpriteFacesReversed", True)
cdo.modify()
after = cdo.get_editor_property("bSpriteFacesReversed")

unreal.log(f"bSpriteFacesReversed: {before} -> {after}")

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log("=== DEATHBOT FLYING FACING FIX COMPLETE ===")
