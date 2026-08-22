"""
ue_import_fancy_cayde.py

Imports the three new "Riding Fancy Pants" (mount) flipbooks for the Rage/Ultimate feature, from
RawAssets/Allies/Cayde_Riding_Fancy_Pants-*.png. All three confirmed 1280x1280 RGBA, a clean
5x5=25-frame grid (256x256/cell) -- same convention as the rest of tonight's batch, checked rather
than assumed.

Deliberately NOT pre-flipping these on import (unlike the earlier facing-flip fix for the rest of
Cayde's batch) -- that fix was applied only after live-testing PROVED the earlier batch was
reversed; this is new, previously-untested art, so it gets imported as-is and the facing gets
tested live afterward (see ue_test_fancy_cayde_facing.py) rather than assumed either way.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_fancy_cayde.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Allies"
TEX_DEST = "/Game/Characters/DeathMetalCat/Textures"
SPRITE_DEST_BASE = "/Game/Characters/DeathMetalCat/Sprites"
FLIPBOOK_DEST = "/Game/Characters/DeathMetalCat/Flipbooks"

GRID_COLS = 5
CELL_SIZE = 256

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_texture(src_filename, tex_name):
    tex_full_path = TEX_DEST + "/" + tex_name
    task = unreal.AssetImportTask()
    task.filename = RAW_DIR + "\\" + src_filename
    task.destination_path = TEX_DEST
    task.destination_name = tex_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.factory = unreal.TextureFactory()
    asset_tools.import_asset_tasks([task])

    texture = unreal.EditorAssetLibrary.load_asset(tex_full_path)
    if texture is None:
        raise RuntimeError(f"texture import failed for {tex_full_path}")

    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)

    tex_w, tex_h = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    unreal.log(f"  texture imported: {tex_w}x{tex_h}")
    return texture


def import_full_sheet(src_filename, tex_name, sprite_subfolder, sprite_prefix, flipbook_name, fps):
    unreal.log(f"=== {src_filename} -> {flipbook_name} ===")
    sprite_dest = f"{SPRITE_DEST_BASE}/{sprite_subfolder}"

    texture = import_texture(src_filename, tex_name)

    sprite_paths = []
    for i in range(25):
        row, col = i // GRID_COLS, i % GRID_COLS
        uv_x, uv_y = col * CELL_SIZE, row * CELL_SIZE
        sprite_name = f"{sprite_prefix}_{i + 1:02d}"
        sprite_full_path = f"{sprite_dest}/{sprite_name}"

        sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
        if sprite is None:
            sprite = asset_tools.create_asset(sprite_name, sprite_dest, unreal.PaperSprite, unreal.PaperSpriteFactory())
        sprite.set_editor_property("source_texture", texture)
        sprite.set_editor_property("source_uv", unreal.Vector2D(uv_x, uv_y))
        sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
        sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
        sprite.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(sprite)
        sprite_paths.append(sprite_full_path)

    unreal.log(f"  created {len(sprite_paths)} sprite(s)")

    flipbook_full_path = f"{FLIPBOOK_DEST}/{flipbook_name}"
    flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
    if flipbook is None:
        flipbook = asset_tools.create_asset(flipbook_name, FLIPBOOK_DEST, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())

    key_frames = []
    for sp_path in sprite_paths:
        sp_asset = unreal.EditorAssetLibrary.load_asset(sp_path)
        kf = unreal.PaperFlipbookKeyFrame()
        kf.set_editor_property("sprite", sp_asset)
        kf.set_editor_property("frame_run", 1)
        key_frames.append(kf)
    flipbook.set_editor_property("key_frames", key_frames)
    flipbook.set_editor_property("frames_per_second", fps)
    flipbook.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
    unreal.log(f"  {flipbook_full_path}: {len(key_frames)} frames @ {fps}fps")


import_full_sheet("Cayde_Riding_Fancy_Pants-fancy_cayde_idle-v1.png", "T_DeathMetalCat_FancyIdle",
                   "FancyIdle", "SP_DeathMetalCat_FancyIdle", "FB_DeathMetalCat_FancyIdle", fps=10.0)

import_full_sheet("Cayde_Riding_Fancy_Pants-fancy_cayde_gallop-v1.png", "T_DeathMetalCat_FancyGallop",
                   "FancyGallop", "SP_DeathMetalCat_FancyGallop", "FB_DeathMetalCat_FancyGallop", fps=15.0)

import_full_sheet("Cayde_Riding_Fancy_Pants-fancy_cayde_attack-v1.png", "T_DeathMetalCat_FancyAttack",
                   "FancyAttack", "SP_DeathMetalCat_FancyAttack", "FB_DeathMetalCat_FancyAttack", fps=20.0)

unreal.log("=== FANCY CAYDE IMPORT COMPLETE ===")
