"""
ue_fix_deathbot_walk_pivots.py

FB_Robot_Walk's 4 keyframe sprites (SP_Robot_Walk_01..04) were authored with CUSTOM per-frame
pivots that don't match FB_Robot_Idle's CENTER_CENTER pivot (nor each other) -- Idle's pivot sits
at the sprite's vertical center (Y~701 of 1402), while every Walk frame's pivot sits near the
bottom of its source image (Y~1170-1283), so switching Idle->Walk visibly yanks the rendered
sprite upward. Confirmed via live PIE polling that capsule size/actor Z/component transform never
change during walk -- this is purely a sprite-asset pivot mismatch, not a physics/collision issue.

Switches all 4 Walk sprites to CENTER_CENTER, matching Idle's (and Cayde's) working convention.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_deathbot_walk_pivots.py').read())"
"""

import unreal

SPRITE_NAMES = ["SP_Robot_Walk_01", "SP_Robot_Walk_02", "SP_Robot_Walk_03", "SP_Robot_Walk_04"]
FOLDER = "/Game/Characters/Enemies/DeathBotWalking/Sprites"

asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()

for name in SPRITE_NAMES:
    pkg = f"{FOLDER}/{name}"
    found = asset_registry.get_assets_by_package_name(pkg)
    if not found:
        unreal.log_error(f"NOT FOUND: {pkg}")
        continue
    sprite = found[0].get_asset()

    before_mode = sprite.get_editor_property("pivot_mode")
    before_pivot = sprite.get_editor_property("custom_pivot_point")

    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)

    after_mode = sprite.get_editor_property("pivot_mode")
    unreal.log(f"[FIX] {name}: pivot_mode {before_mode} (custom_pivot={before_pivot}) -> {after_mode}")

unreal.log("=== DEATHBOT WALK PIVOT FIX COMPLETE ===")
