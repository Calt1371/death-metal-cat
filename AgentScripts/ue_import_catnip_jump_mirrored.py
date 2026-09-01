"""
ue_import_catnip_jump_mirrored.py

Cat Nip Jump was found to mirror inconsistently via runtime Scale.X (some frames flip correctly,
others don't -- root cause not fully pinned down despite fixing custom_pivot_point, which turned
out not to even persist as a writable property; the render is inconsistent enough not to trust).
Rather than keep chasing that, this creates a genuinely separate, pre-mirrored left-facing copy of
the Jump flipbook (same per-cell-flip technique as ue_fix_catnip_walk_flip.py), so the game can pick
between two correctly-drawn assets by movement direction instead of relying on runtime mirroring at
all for this one flipbook.

Imports FB_DeathMetalCat_CatNipJumpMirrored from the per-cell-flipped copy in _flipped_cache/. Same
25 frames / 15fps / PPU 0.75 as the original CatNipJump.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_catnip_jump_mirrored.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\_flipped_cache"
TEX_DEST = "/Game/Characters/DeathMetalCat/Textures"
SPRITE_DEST = "/Game/Characters/DeathMetalCat/Sprites/CatNipJumpMirrored"
FLIPBOOK_PATH = "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_CatNipJumpMirrored"
GRID_COLS = 5
CELL_SIZE = 256
PPU = 0.75
FPS = 15

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

task = unreal.AssetImportTask()
task.filename = RAW_DIR + "\\Cayde-catnip_super_mode_jump-v5-flipped.png"
task.destination_path = TEX_DEST
task.destination_name = "T_DeathMetalCat_CatNipJumpMirrored"
task.automated = True
task.save = True
task.replace_existing = True
task.factory = unreal.TextureFactory()
asset_tools.import_asset_tasks([task])

texture = unreal.load_object(None, TEX_DEST + "/T_DeathMetalCat_CatNipJumpMirrored.T_DeathMetalCat_CatNipJumpMirrored")
if texture is None:
    raise RuntimeError("texture import failed")
texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("srgb", True)
texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
texture.modify()
unreal.EditorAssetLibrary.save_loaded_asset(texture)
print("imported texture:", texture.blueprint_get_size_x(), texture.blueprint_get_size_y())

sprite_paths = []
for i in range(25):
    row, col = i // GRID_COLS, i % GRID_COLS
    sprite_name = f"SP_DeathMetalCat_CatNipJumpMirrored_{i + 1:02d}"
    sprite_full_path = SPRITE_DEST + "/" + sprite_name
    sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
    if sprite is None:
        sprite = asset_tools.create_asset(sprite_name, SPRITE_DEST, unreal.PaperSprite, unreal.PaperSpriteFactory())
    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(col * CELL_SIZE, row * CELL_SIZE))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    sprite.set_editor_property("PixelsPerUnrealUnit", PPU)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
    sprite_paths.append(sprite_full_path)
print(f"created {len(sprite_paths)} sprites @ ppu={PPU}")

flipbook = unreal.EditorAssetLibrary.load_asset(FLIPBOOK_PATH)
if flipbook is None:
    flipbook = asset_tools.create_asset("FB_DeathMetalCat_CatNipJumpMirrored", "/Game/Characters/DeathMetalCat/Flipbooks", unreal.PaperFlipbook, unreal.PaperFlipbookFactory())
key_frames = []
for sp_path in sprite_paths:
    sp_asset = unreal.EditorAssetLibrary.load_asset(sp_path)
    kf = unreal.PaperFlipbookKeyFrame()
    kf.set_editor_property("sprite", sp_asset)
    kf.set_editor_property("frame_run", 1)
    key_frames.append(kf)
flipbook.set_editor_property("key_frames", key_frames)
flipbook.set_editor_property("frames_per_second", FPS)
flipbook.modify()
unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
print(f"{FLIPBOOK_PATH}: {len(key_frames)} frames @ {FPS}fps")

print("=== CATNIP JUMP MIRRORED IMPORT COMPLETE ===")
