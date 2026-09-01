"""
ue_fix_catnip_walk_flip.py

Fixes CatNipWalk facing backward -- Cayde-hero_catnip_super_walk-v2.png's raw art faces LEFT at
Scale.X=+1, unlike every other Cat Nip sheet (Idle/Attack/Jump all face right already) and unlike
the project's established convention (Scale.X=+1 = facing right, see ue_fix_cayde_facing_flip.py).

Re-imports FB_DeathMetalCat_CatNipWalk from a per-CELL-flipped copy of the source sheet (each 256px
grid cell individually mirrored in place via PIL, NOT the whole 1280x1280 canvas flipped as one
image -- a whole-canvas flip would swap column positions [col0<->col4, col1<->col3] and scramble
the walk cycle's frame order, not just fix facing). Same PPU (0.75) and frame/fps as the original
import -- this pass only fixes orientation.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_catnip_walk_flip.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\_flipped_cache"
TEX_DEST = "/Game/Characters/DeathMetalCat/Textures"
SPRITE_DEST = "/Game/Characters/DeathMetalCat/Sprites/CatNipWalk"
FLIPBOOK_PATH = "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_CatNipWalk"
GRID_COLS = 5
CELL_SIZE = 256
PPU = 0.75

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

task = unreal.AssetImportTask()
task.filename = RAW_DIR + "\\Cayde-hero_catnip_super_walk-v2-flipped.png"
task.destination_path = TEX_DEST
task.destination_name = "T_DeathMetalCat_CatNipWalk"
task.automated = True
task.save = True
task.replace_existing = True
task.factory = unreal.TextureFactory()
asset_tools.import_asset_tasks([task])

texture = unreal.load_object(None, TEX_DEST + "/T_DeathMetalCat_CatNipWalk.T_DeathMetalCat_CatNipWalk")
if texture is None:
    raise RuntimeError("texture re-import failed")
texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("srgb", True)
texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
texture.modify()
unreal.EditorAssetLibrary.save_loaded_asset(texture)
print("re-imported texture:", texture.blueprint_get_size_x(), texture.blueprint_get_size_y())

sprite_paths = []
for i in range(25):
    row, col = i // GRID_COLS, i % GRID_COLS
    sprite_name = f"SP_DeathMetalCat_CatNipWalk_{i + 1:02d}"
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
print(f"rebuilt {len(sprite_paths)} sprites @ ppu={PPU}")

flipbook = unreal.EditorAssetLibrary.load_asset(FLIPBOOK_PATH)
if flipbook is None:
    raise RuntimeError(f"expected existing flipbook not found: {FLIPBOOK_PATH}")
old_fps = flipbook.get_editor_property("frames_per_second")
key_frames = []
for sp_path in sprite_paths:
    sp_asset = unreal.EditorAssetLibrary.load_asset(sp_path)
    kf = unreal.PaperFlipbookKeyFrame()
    kf.set_editor_property("sprite", sp_asset)
    kf.set_editor_property("frame_run", 1)
    key_frames.append(kf)
flipbook.set_editor_property("key_frames", key_frames)
flipbook.modify()
unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
print(f"{FLIPBOOK_PATH}: {len(key_frames)} frames @ {old_fps}fps (unchanged) -- facing fixed")

print("=== CATNIP WALK FLIP FIX COMPLETE ===")
