"""
ue_import_catnip_moveset.py

Imports the 3 remaining "Super Cayde" Cat Nip flipbooks -- Walk (used for both Walk and Run per
the design ask), Attack, and Jump -- matching FB_DeathMetalCat_CatNipIdle's own existing import
convention exactly (25 frames, 5x5 grid, 256px cells; same raw canvas size, 1280x1280, confirmed
via PIL before writing this script).

Also re-applies PPU=0.75 to all 4 Cat Nip flipbooks (Idle included) -- half of regular Idle's PPU
(1.5), i.e. double the rendered size, per the design ask ("about double the size", "bigger than
pre-catnip Cayde so it's convincing he got bigger and stronger"). Supersedes
ue_fix_catnip_ppu.py's earlier PPU=1.5 fix (same-size-as-Idle), which the design ask overrides.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_catnip_moveset.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Allies"
TEX_DEST = "/Game/Characters/DeathMetalCat/Textures"
FLIPBOOK_DEST = "/Game/Characters/DeathMetalCat/Flipbooks"
GRID_COLS = 5
CELL_SIZE = 256
CATNIP_PPU = 0.75

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_texture(src_filename, dest_path, tex_name):
    tex_full_path = dest_path + "/" + tex_name
    task = unreal.AssetImportTask()
    task.filename = RAW_DIR + "\\" + src_filename
    task.destination_path = dest_path
    task.destination_name = tex_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.factory = unreal.TextureFactory()
    asset_tools.import_asset_tasks([task])

    texture = unreal.load_object(None, tex_full_path + "." + tex_name)
    if texture is None:
        raise RuntimeError(f"texture import failed for {tex_full_path}")

    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    return texture


def import_flipbook(src_filename, tex_name, sprite_subdir, flipbook_name, fps):
    print(f"=== {src_filename} -> {flipbook_name} ===")
    texture = import_texture(src_filename, TEX_DEST, tex_name)
    tex_w, tex_h = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    print(f"  texture: {tex_w}x{tex_h}")

    sprite_dest = f"/Game/Characters/DeathMetalCat/Sprites/{sprite_subdir}"
    sprite_paths = []
    for i in range(25):
        row, col = i // GRID_COLS, i % GRID_COLS
        sprite_name = f"SP_DeathMetalCat_{sprite_subdir}_{i + 1:02d}"
        sprite_full_path = sprite_dest + "/" + sprite_name
        sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
        if sprite is None:
            sprite = asset_tools.create_asset(sprite_name, sprite_dest, unreal.PaperSprite, unreal.PaperSpriteFactory())
        sprite.set_editor_property("source_texture", texture)
        sprite.set_editor_property("source_uv", unreal.Vector2D(col * CELL_SIZE, row * CELL_SIZE))
        sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
        sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
        sprite.set_editor_property("PixelsPerUnrealUnit", CATNIP_PPU)
        sprite.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(sprite)
        sprite_paths.append(sprite_full_path)
    print(f"  rebuilt {len(sprite_paths)} sprite(s) @ ppu={CATNIP_PPU}")

    flipbook_full_path = FLIPBOOK_DEST + "/" + flipbook_name
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
    print(f"  {flipbook_full_path}: {len(key_frames)} frames @ {fps}fps")
    return flipbook_full_path


import_flipbook("Cayde-hero_catnip_super_walk-v2.png", "T_DeathMetalCat_CatNipWalk", "CatNipWalk", "FB_DeathMetalCat_CatNipWalk", fps=12)
import_flipbook("Cayde-catnip_super_attack-v4.png", "T_DeathMetalCat_CatNipAttack", "CatNipAttack", "FB_DeathMetalCat_CatNipAttack", fps=30)
import_flipbook("Cayde-catnip_super_mode_jump-v5.png", "T_DeathMetalCat_CatNipJump", "CatNipJump", "FB_DeathMetalCat_CatNipJump", fps=15)

# Re-apply PPU=0.75 (double size vs regular Idle's 1.5) to the existing CatNipIdle flipbook too --
# it was previously set to match Idle's own PPU (1.5, same size), which is exactly what the design
# ask says reads "way too small".
print("=== resizing existing CatNipIdle to PPU=0.75 ===")
idle_fb = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_CatNipIdle.FB_DeathMetalCat_CatNipIdle")
for kf in idle_fb.get_editor_property("key_frames"):
    sprite = kf.get_editor_property("sprite")
    sprite.set_editor_property("PixelsPerUnrealUnit", CATNIP_PPU)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
print(f"  set PPU={CATNIP_PPU} on CatNipIdle's sprites")

print("=== CATNIP MOVESET IMPORT COMPLETE ===")
