"""
ue_fix_deathbot_shoot_flipbooks.py

BP_EnemyDeathBotWalking's ShootDrawFlipbook/ShootLoopFlipbook properties were never assigned,
even though FB_Robot_ShootDraw/FB_Robot_ShootLoop exist on disk -- confirmed live (both read as
None). ADeathMetalCatEnemyBase::BeginRangedAttack already has a documented fallback for an unset
ShootDrawFlipbook (skip straight to the firing loop with no windup), which is exactly the observed
bug: the projectile fires immediately with no visible gun-raise animation. Not a timing/sync issue
-- just two unset Blueprint defaults.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_deathbot_shoot_flipbooks.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/Enemies/DeathBotWalking/Blueprints/BP_EnemyDeathBotWalking"
DRAW_PATH = "/Game/Characters/Enemies/DeathBotWalking/Flipbooks/FB_Robot_ShootDraw"
LOOP_PATH = "/Game/Characters/Enemies/DeathBotWalking/Flipbooks/FB_Robot_ShootLoop"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

draw_fb = unreal.EditorAssetLibrary.load_asset(DRAW_PATH)
loop_fb = unreal.EditorAssetLibrary.load_asset(LOOP_PATH)
if draw_fb is None or loop_fb is None:
    raise RuntimeError(f"Failed to load flipbook assets: draw={draw_fb} loop={loop_fb}")

unreal.log(f"BEFORE: ShootDrawFlipbook={cdo.get_editor_property('ShootDrawFlipbook')} "
           f"ShootLoopFlipbook={cdo.get_editor_property('ShootLoopFlipbook')}")

cdo.set_editor_property("ShootDrawFlipbook", draw_fb)
cdo.set_editor_property("ShootLoopFlipbook", loop_fb)
cdo.modify()

unreal.log(f"AFTER: ShootDrawFlipbook={cdo.get_editor_property('ShootDrawFlipbook')} "
           f"ShootLoopFlipbook={cdo.get_editor_property('ShootLoopFlipbook')}")

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log("=== DEATHBOT SHOOT FLIPBOOK FIX COMPLETE ===")
