"""
ue_set_deathbot_walking_reversed_facing.py

Sets bSpriteFacesReversed = true on BP_EnemyDeathBotWalking only -- the DeathBot facing bug was
confirmed live in PIE to be caused by this Blueprint's sprite art being authored facing -X by
default (opposite of the code's/Cayde's convention), not a bug in the facing-toward-player
calculation itself (verified: XDistance sign correctly drives Scale.X sign). BP_EnemyDeathBotFlying
and BP_EnemyBase are deliberately left at the property's default (false) -- Flying's own art
convention was never confirmed either way, and EnemyBase has no flipbook component at all.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_set_deathbot_walking_reversed_facing.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/Enemies/DeathBotWalking/Blueprints/BP_EnemyDeathBotWalking"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

before = cdo.get_editor_property("bSpriteFacesReversed")
cdo.set_editor_property("bSpriteFacesReversed", True)
cdo.modify()
after = cdo.get_editor_property("bSpriteFacesReversed")

unreal.log(f"bSpriteFacesReversed: {before} -> {after}")

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log("=== DEATHBOT WALKING FACING FIX COMPLETE ===")
