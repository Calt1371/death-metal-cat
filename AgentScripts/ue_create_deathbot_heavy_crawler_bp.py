"""
ue_create_deathbot_heavy_crawler_bp.py

Creates BP_EnemyDeathBotHeavy and BP_EnemyDeathBotCrawler, both deriving from
DeathMetalCatEnemyBase, matching BP_EnemyDeathBotWalking's convention: PlaceholderMesh hidden
(real flipbook art in place of it), WalkFlipbook/AttackFlipbook wired in, all Health/Combat/Ranged
Attack stats left at the C++ base class defaults -- balance tuning is a separate pass, not part of
this import.

Both are melee/contact-damage attackers (their attack art is a single direct swipe/punch
animation, not a shoot/draw/loop pattern like DeathBotWalking/DeathBotFlying's gunfire), so only
WalkFlipbook + the new AttackFlipbook are wired -- ShootDrawFlipbook/ShootLoopFlipbook are left
unset, same as IdleFlipbook (neither sheet included a dedicated idle pose; Tick falls back to
whatever's already playing when neither WalkFlipbook nor AttackFlipbook applies, i.e. it'll just
hold on the last Walk/Attack frame -- acceptable for now, matching the "no guessing beyond what
was provided" instruction for this import).

bSpriteFacesReversed is deliberately NOT set here -- see the separate live PIE facing test run
after this script, and DeathMetalCatEnemyBase.h's own header comment on that property.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_deathbot_heavy_crawler_bp.py').read())"
"""

import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
base_class = unreal.load_class(None, "/Script/PythonTest.DeathMetalCatEnemyBase")

JOBS = [
    ("DeathBotHeavy", "BP_EnemyDeathBotHeavy"),
    ("DeathBotCrawler", "BP_EnemyDeathBotCrawler"),
]

for enemy_name, bp_name in JOBS:
    bp_dest = f"/Game/Characters/Enemies/{enemy_name}/Blueprints"
    bp_full_path = bp_dest + "/" + bp_name

    if unreal.EditorAssetLibrary.does_asset_exist(bp_full_path):
        bp = unreal.EditorAssetLibrary.load_asset(bp_full_path)
        unreal.log(f"{bp_full_path} already exists, reusing")
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", base_class)
        bp = asset_tools.create_asset(bp_name, bp_dest, unreal.Blueprint, factory)
        unreal.log(f"created {bp_full_path}")

    cdo = unreal.get_default_object(bp.generated_class())

    walk_fb = unreal.EditorAssetLibrary.load_asset(f"/Game/Characters/Enemies/{enemy_name}/Flipbooks/FB_Enemy_{enemy_name}_Walk")
    attack_fb = unreal.EditorAssetLibrary.load_asset(f"/Game/Characters/Enemies/{enemy_name}/Flipbooks/FB_Enemy_{enemy_name}_Attack")
    if walk_fb is None or attack_fb is None:
        raise RuntimeError(f"missing flipbook(s) for {enemy_name}")

    cdo.set_editor_property("walk_flipbook", walk_fb)
    cdo.set_editor_property("attack_flipbook", attack_fb)

    placeholder_mesh = cdo.get_editor_property("placeholder_mesh")
    if placeholder_mesh:
        placeholder_mesh.set_editor_property("visible", False)

    cdo.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)

    unreal.log(f"{bp_name}: walk_flipbook -> {walk_fb.get_name()}, attack_flipbook -> {attack_fb.get_name()}, placeholder hidden")

unreal.log("=== DEATHBOT HEAVY/CRAWLER BLUEPRINTS COMPLETE ===")
