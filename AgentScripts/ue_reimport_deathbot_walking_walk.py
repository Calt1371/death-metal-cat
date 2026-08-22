"""
ue_reimport_deathbot_walking_walk.py

Replaces DeathBotWalking's walk-cycle art with a newly-authored sheet:
  RawAssets/Enemies/DeathBotWalking-walk-v1.png -> FB_Robot_Walk

1280x1280, a clean 5x5=25-frame grid (256x256/cell, exact division). Confirmed via PIL bbox
analysis before writing this: real alpha transparency, no frame's content touches its own cell
edge (no bleed risk), and body-top position is consistent within a 5px band across all 25 frames
(rows 23-28) -- a real, clean walk cycle, unlike the old sheet's pivot/dimension inconsistencies
that caused the original bounce/climb bug.

The OLD Walk setup used a different (and, per ue_import_trap_flipbooks.py's own convention note,
non-preferred) pattern: 4 separate full-size textures (T_Robot_Walk_01..04), one complete frame
image each, no cropping. This import instead uses the established shared-texture-plus-cropped-
sprites pattern (matches DeathBotFlying, matches this session's DeathBotFlying re-import) --
one new texture (T_Robot_Walk), 25 sprites cropped from it. The 4 old per-frame textures and
sprites are deleted since nothing will reference them after FB_Robot_Walk's key_frames are rebuilt.

TF_NEAREST filtering applied (not the default bilinear) to prevent adjacent-cell sampling bleed.
All 25 sprites get CENTER_CENTER pivot, matching every other correctly-behaving flipbook in the
project. FB_Robot_Walk's own asset path is left unchanged, so BP_EnemyDeathBotWalking's
WalkFlipbook property (already pointing at it) keeps resolving with no Blueprint edit needed.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_reimport_deathbot_walking_walk.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Enemies"
SRC_FILENAME = "DeathBotWalking-walk-v1.png"

TEX_DEST = "/Game/Characters/Enemies/DeathBotWalking/Textures"
SPRITE_DEST = "/Game/Characters/Enemies/DeathBotWalking/Sprites"
FLIPBOOK_PATH = "/Game/Characters/Enemies/DeathBotWalking/Flipbooks/FB_Robot_Walk"

NEW_TEX_NAME = "T_Robot_Walk"
SPRITE_PREFIX = "SP_Robot_Walk"
GRID_ROWS = 5
GRID_COLS = 5
CELL_SIZE = 256

OLD_TEXTURES = [f"T_Robot_Walk_{i:02d}" for i in range(1, 5)]
OLD_SPRITES = [f"SP_Robot_Walk_{i:02d}" for i in range(1, 5)]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# --- Delete the old per-frame sprites and textures ---
removed_sprites = 0
for name in OLD_SPRITES:
    p = SPRITE_DEST + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        unreal.EditorAssetLibrary.delete_asset(p)
        removed_sprites += 1
removed_textures = 0
for name in OLD_TEXTURES:
    p = TEX_DEST + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        unreal.EditorAssetLibrary.delete_asset(p)
        removed_textures += 1
unreal.log(f"removed {removed_sprites} old sprite(s), {removed_textures} old texture(s)")

# --- Import the new shared sheet ---
tex_full_path = TEX_DEST + "/" + NEW_TEX_NAME
task = unreal.AssetImportTask()
task.filename = RAW_DIR + "\\" + SRC_FILENAME
task.destination_path = TEX_DEST
task.destination_name = NEW_TEX_NAME
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
tex_h = texture.blueprint_get_size_y()
unreal.log(f"texture imported: {tex_full_path} ({tex_w}x{tex_h})")

# --- Create the 25 new sprites ---
sprite_paths = []
for i in range(GRID_ROWS * GRID_COLS):
    row = i // GRID_COLS
    col = i % GRID_COLS
    frame_num = i + 1
    sprite_name = f"{SPRITE_PREFIX}_{frame_num:02d}"
    sprite_full_path = SPRITE_DEST + "/" + sprite_name

    uv_x, uv_y = col * CELL_SIZE, row * CELL_SIZE
    sprite = asset_tools.create_asset(sprite_name, SPRITE_DEST, unreal.PaperSprite, unreal.PaperSpriteFactory())
    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(uv_x, uv_y))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
    sprite_paths.append(sprite_full_path)

unreal.log(f"created {len(sprite_paths)} new sprites in {SPRITE_DEST}")

# --- Rebuild the EXISTING flipbook's key_frames (path unchanged) ---
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
unreal.log("=== DEATHBOT WALKING WALK RE-IMPORT COMPLETE ===")
