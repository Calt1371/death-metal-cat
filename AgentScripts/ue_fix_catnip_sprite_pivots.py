"""
ue_fix_catnip_sprite_pivots.py

Fixes CatNipJump (and, for safety, CatNipWalk/CatNipAttack -- same import script, same potential
bug) rendering unmirrored regardless of Scale.X sign. Root cause: each sprite's custom_pivot_point
was left at an ABSOLUTE sheet-space value (source_uv + half-dimension, e.g. (896, 640) for a sprite
whose source_uv is (768, 512)) instead of a LOCAL cell-relative value (128, 128 for a 256px cell).
Confirmed live (2026-08-25) that CatNipIdle's sprites (imported by an older script,
ue_import_item_sprites.py) have the correct local pivot and mirror correctly via Scale.X, while
CatNipJump's (imported by ue_import_catnip_moveset.py, which sets pivot_mode via Python but never
explicitly writes custom_pivot_point) do not -- setting pivot_mode alone apparently isn't enough to
get Python-created sprites' auto-computed pivot/render-geometry right; explicitly writing
custom_pivot_point is.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_catnip_sprite_pivots.py').read())"
"""

import unreal

SPRITE_DIRS = {
    "CatNipWalk": "/Game/Characters/DeathMetalCat/Sprites/CatNipWalk",
    "CatNipAttack": "/Game/Characters/DeathMetalCat/Sprites/CatNipAttack",
    "CatNipJump": "/Game/Characters/DeathMetalCat/Sprites/CatNipJump",
}

for label, sprite_dir in SPRITE_DIRS.items():
    asset_paths = unreal.EditorAssetLibrary.list_assets(sprite_dir, recursive=False)
    fixed = 0
    for asset_path in asset_paths:
        sprite = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(sprite, unreal.PaperSprite):
            continue
        dim = sprite.get_editor_property("source_dimension")
        correct_pivot = unreal.Vector2D(dim.x / 2.0, dim.y / 2.0)
        current_pivot = sprite.get_editor_property("custom_pivot_point")
        if current_pivot.x != correct_pivot.x or current_pivot.y != correct_pivot.y:
            sprite.set_editor_property("custom_pivot_point", correct_pivot)
            # Re-assert pivot_mode too -- forces the sprite to recompute its render geometry off
            # the now-correct pivot rather than leaving stale geometry baked from the wrong one.
            sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
            sprite.modify()
            unreal.EditorAssetLibrary.save_loaded_asset(sprite)
            fixed += 1
    print(f"{label}: fixed {fixed}/{len(asset_paths)} sprites")

print("=== CATNIP SPRITE PIVOT FIX COMPLETE ===")
