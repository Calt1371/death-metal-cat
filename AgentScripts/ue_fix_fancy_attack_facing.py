"""
ue_fix_fancy_attack_facing.py

FancyAttackFlipbook specifically needed its own facing fix, independent of FancyIdle/FancyGallop --
confirmed live in PIE (forcing the flipbook at Scale.X=+1 with a reference enemy placed to the
right): the sprite's own baked-in rainbow beam art fired LEFT while the reference enemy sat to the
right, the opposite of Idle/Gallop's already-confirmed-correct convention. This is exactly why each
flipbook in a batch has to be checked independently rather than assumed uniform (same lesson as
Gallop needing its own flip, and as DeathBotHeavy/Crawler needing independently-checked facing).

Re-imports FancyAttack from the source PNG resized 1.5x (matching the earlier size fix) AND
horizontally mirrored this time, replacing the un-flipped 1.5x version from
ue_resize_and_fix_fancy_cayde.py. Cell size/PPU/pivot unchanged (384px cells, PPU=1.0,
CENTER_CENTER) -- only the source image's mirroring differs.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_fancy_attack_facing.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\_resized_cache"
SRC_FILENAME = "FancyAttack_resized_flipped.png"
TEX_DEST = "/Game/Characters/DeathMetalCat/Textures"
TEX_NAME = "T_DeathMetalCat_FancyAttack"
SPRITE_DEST = "/Game/Characters/DeathMetalCat/Sprites/FancyAttack"
SPRITE_PREFIX = "SP_DeathMetalCat_FancyAttack"
FLIPBOOK_PATH = "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_FancyAttack"
GRID_ROWS = 5
GRID_COLS = 5
CELL_SIZE = 384

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

tex_full_path = TEX_DEST + "/" + TEX_NAME
task = unreal.AssetImportTask()
task.filename = RAW_DIR + "\\" + SRC_FILENAME
task.destination_path = TEX_DEST
task.destination_name = TEX_NAME
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

tex_w = texture.blueprint_get_size_x()
unreal.log(f"texture re-imported (flipped): {tex_full_path} ({tex_w}x{tex_w})")

sprite_paths = []
for i in range(GRID_ROWS * GRID_COLS):
    row = i // GRID_COLS
    col = i % GRID_COLS
    frame_num = i + 1
    sprite_name = f"{SPRITE_PREFIX}_{frame_num:02d}"
    sprite_full_path = SPRITE_DEST + "/" + sprite_name

    uv_x, uv_y = col * CELL_SIZE, row * CELL_SIZE
    sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(uv_x, uv_y))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
    sprite_paths.append(sprite_full_path)

unreal.log(f"{len(sprite_paths)} sprites rebuilt (flipped) at cell_size={CELL_SIZE}")

flipbook = unreal.EditorAssetLibrary.load_asset(FLIPBOOK_PATH)
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

unreal.log(f"{FLIPBOOK_PATH}: {len(key_frames)} frames @ {old_fps}fps (unchanged)")
unreal.log("=== FANCY ATTACK FACING FIX COMPLETE ===")
