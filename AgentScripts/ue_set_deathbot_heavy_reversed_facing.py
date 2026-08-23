"""
ue_set_deathbot_heavy_reversed_facing.py

Sets bSpriteFacesReversed = true on BP_EnemyDeathBotHeavy only -- confirmed live in PIE (repurposing
the placed BP_EnemyDeathBotFlying instance's PaperFlipbookComponent to show Heavy's Walk flipbook,
same technique as every other facing test this session) that Heavy's art is authored facing -X by
default: with the player positioned to its right and Scale.X forced to +1 (unmirrored/natural), its
head/antenna pointed left, away from the player. Forcing Scale.X to -1 on the same setup visibly
turned it to face the player correctly, confirming the fix.

BP_EnemyDeathBotCrawler is deliberately left at the property's default (false) -- its own live test
(claw-attack flipbook, frame mid-swipe) showed the claw reaching directly toward the player at
Scale.X=+1 with no mirroring, so its unmirrored art already faces correctly. The two new enemies do
NOT share a facing convention with each other, exactly the kind of case this had to be checked for
independently rather than assumed uniform.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_set_deathbot_heavy_reversed_facing.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/Enemies/DeathBotHeavy/Blueprints/BP_EnemyDeathBotHeavy"

bp = unreal.load_object(None, BP_PATH + "." + BP_PATH.rsplit("/", 1)[-1])
cdo = unreal.get_default_object(bp.generated_class())

before = cdo.get_editor_property("bSpriteFacesReversed")
cdo.set_editor_property("bSpriteFacesReversed", True)
cdo.modify()
after = cdo.get_editor_property("bSpriteFacesReversed")

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log(f"bSpriteFacesReversed: {before} -> {after}")
unreal.log("=== DEATHBOT HEAVY FACING FIX COMPLETE ===")
