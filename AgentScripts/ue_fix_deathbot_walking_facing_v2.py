"""
ue_fix_deathbot_walking_facing_v2.py

Reverts bSpriteFacesReversed to false on BP_EnemyDeathBotWalking. Confirmed via a direct in-PIE
A/B screenshot comparison (player positioned right, then left, of the DeathBot) that the NEW
sprite sheets (Idle/Walk/Shoot re-imported tonight) use the normal orientation convention -- the
character's face/front-arm assembly renders on the same side as positive Scale.X, matching Cayde's
own convention directly. The bSpriteFacesReversed=true flag was set for the OLD art (which really
was reversed) and is now actively causing the character to face away from the player with the new
art.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_deathbot_walking_facing_v2.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/Enemies/DeathBotWalking/Blueprints/BP_EnemyDeathBotWalking"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

before = cdo.get_editor_property("bSpriteFacesReversed")
cdo.set_editor_property("bSpriteFacesReversed", False)
cdo.modify()

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log(f"bSpriteFacesReversed: {before} -> False")
unreal.log("=== DEATHBOT WALKING FACING FIX v2 COMPLETE ===")
