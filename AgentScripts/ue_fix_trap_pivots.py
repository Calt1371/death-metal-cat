"""
ue_fix_trap_pivots.py

Switches every sprite frame of FB_Trap_Electric, FB_Trap_Saw, and FB_Trap_SpikeFloor from CUSTOM
per-frame pivots to CENTER_CENTER, matching the working convention confirmed on Cayde's own
flipbooks (and now DeathBot's fixed Walk cycle). FB_Trap_SpikeColumn is deliberately excluded --
its CUSTOM pivots are for how it triggers/emerges, not a bug, per explicit instruction.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_trap_pivots.py').read())"
"""

import unreal

FOLDER = "/Game/Environments/CityBiome/Traps"
SPRITE_NAMES = (
    [f"SP_Trap_Electric_{i:02d}" for i in range(1, 9)]
    + [f"SP_Trap_Saw_{i:02d}" for i in range(1, 9)]
    + [f"SP_Trap_SpikeFloor_{i:02d}" for i in range(1, 9)]
)

asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()

fixed = 0
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
    fixed += 1

    unreal.log(f"[FIX] {name}: pivot_mode {before_mode} (custom_pivot={before_pivot}) -> CENTER_CENTER")

unreal.log(f"=== TRAP PIVOT FIX COMPLETE -- {fixed}/{len(SPRITE_NAMES)} sprites updated ===")
