"""
ue_import_room_barrier.py

Imports RawAssets/Meshy_Traps/Room_barrier.png as FB_Trap_RoomBarrier -- confirmed via direct visual
review (see task investigation) that this IS a genuine 25-frame animated flipbook, not a static
sprite: rows 2-5 show the barrier's crackling energy/lightning effect visibly differing frame to
frame, unlike a plain repeated-25-times static image. Also confirmed there's no distinct "opening"
animation baked in -- the barrier's silhouette and skull emblem stay in the same place across all
25 frames, only the lightning/glow flickers -- so ARoomBarrier hides/shows the actor outright when
toggling open/closed rather than trying to animate toward a frame that isn't there.

1280x1280, clean 5x5=25-frame grid (256x256/cell, exact division). Verified via PIL alpha-bbox
analysis before writing this: no empty frames, no frame's content touches its own cell edge (no
bleed risk) -- same clean-sheet pattern as most sheets this session, no bleed remediation needed.

Same shared-texture-plus-cropped-sprites pattern as every other flipbook import this session: one
texture, 25 sprites cropped from it, CENTER_CENTER pivot on every sprite, TF_NEAREST filtering.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_room_barrier.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Meshy_Traps"
SRC_FILENAME = "Room_barrier.png"

TEX_DEST = "/Game/Environments/CityBiome/Traps/Textures"
SPRITE_DEST = "/Game/Environments/CityBiome/Traps/Sprites/RoomBarrier"
FLIPBOOK_DEST = "/Game/Environments/CityBiome/Traps/Flipbooks"

TEX_NAME = "T_Trap_RoomBarrier"
SPRITE_PREFIX = "SP_Trap_RoomBarrier"
FLIPBOOK_NAME = "FB_Trap_RoomBarrier"
GRID_ROWS = 5
GRID_COLS = 5
CELL_SIZE = 256

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
unreal.log(f"texture imported: {tex_full_path} ({tex_w}x{tex_w})")

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

unreal.log(f"created {len(sprite_paths)} sprites in {SPRITE_DEST}")

flipbook_full_path = FLIPBOOK_DEST + "/" + FLIPBOOK_NAME
if unreal.EditorAssetLibrary.does_asset_exist(flipbook_full_path):
    flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
else:
    flipbook = asset_tools.create_asset(FLIPBOOK_NAME, FLIPBOOK_DEST, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())

key_frames = []
for sp_path in sprite_paths:
    sp_asset = unreal.EditorAssetLibrary.load_asset(sp_path)
    kf = unreal.PaperFlipbookKeyFrame()
    kf.set_editor_property("sprite", sp_asset)
    kf.set_editor_property("frame_run", 1)
    key_frames.append(kf)
flipbook.set_editor_property("key_frames", key_frames)
flipbook.set_editor_property("frames_per_second", 12.0)
flipbook.modify()
unreal.EditorAssetLibrary.save_loaded_asset(flipbook)

unreal.log(f"{flipbook_full_path}: {len(key_frames)} frames @ 12fps")
unreal.log("=== ROOM BARRIER IMPORT COMPLETE ===")
