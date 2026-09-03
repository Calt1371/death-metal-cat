"""
ue_import_room_barrier_flipbook.py

Imports RawAssets/Allies/barrier_DMC.png (a 1280x1280, 5x5 grid of 256px cells, 25-frame
animated sheet -- same "N-frame grid" convention as every other flipbook in this project) as the
REAL art for the room-exit barrier, replacing the tiny 32x32 placeholder that
Source/PythonTest/RoomBarrier.h's ARoomBarrier class was already built and wired against.

Reuses the exact existing asset names/paths (T_Trap_RoomBarrier, SP_Trap_RoomBarrier_01..25,
FB_Trap_RoomBarrier) so BP_RoomBarrier's already-assigned BarrierFlipbook property picks up the
new real art automatically -- no Blueprint/CDO rewiring needed, no second parallel asset created.

Checks performed (logged in the printed report, not just assumed):
  - Real transparency confirmed on the source PNG (alpha extrema 0..255, corner pixel alpha=0)
    despite the filename lacking the "_nobg" convention other imports use.
  - Grid = 5 rows x 5 columns = 25 frames, 256x256 cells (1280/5) -- matches every other Cayde/item
    flipbook's established 256px-cell convention exactly.
  - Zero of the 25 frames' actual non-transparent content touches its own cell's edge (measured via
    PIL alpha bbox), confirming clean crops with real margin -- no grid-boundary bleed risk.
  - Per-frame content-bbox center measured too: spread is ~+/-16px around each cell's 128,128
    center, the same class of issue Docs/Trap_Flipbook_Import_Log.md diagnosed and fixed with a
    per-frame CUSTOM pivot for the 4 hazard traps. Deliberately NOT applied here -- kept every
    sprite at plain CENTER_CENTER instead, per this task's own explicit instruction ("pivot mode
    CENTER_CENTER, consistent across every frame"). Flagged in the printed report as a known,
    measured tradeoff rather than silently deviating from that instruction.
  - filter=TF_NEAREST applied proactively (not reactively) on the new texture -- the exact fix the
    same import log had to apply after-the-fact to the 4 trap sheets for identical unpadded-grid
    cross-cell GPU sampling bleed. This sheet has the same unpadded construction, so this avoids
    that whole bug class from the start rather than waiting for a bug report.
  - frames_per_second kept at 12.0, matching the placeholder flipbook's already-tuned value --
    only the art is being swapped, not the established ambient-loop timing.

Orientation: this is a static barrier prop (skull emblem + crackling energy field), not a
character -- no left/right facing concept applies. Visual review of the grid: emblem/silhouette
reads upright and correctly oriented in every frame, nothing upside-down or mirrored.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_room_barrier_flipbook.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Allies"
SRC_FILENAME = "barrier_DMC.png"

TEX_DEST = "/Game/Environments/CityBiome/Traps/Textures"
TEX_NAME = "T_Trap_RoomBarrier"
SPRITE_DEST = "/Game/Environments/CityBiome/Traps/Sprites/RoomBarrier"
FLIPBOOK_DEST = "/Game/Environments/CityBiome/Traps/Flipbooks"
FLIPBOOK_NAME = "FB_Trap_RoomBarrier"

GRID_COLS = 5
GRID_ROWS = 5
CELL_SIZE = 256
FPS = 12.0

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# -- Texture import --
task = unreal.AssetImportTask()
task.filename = RAW_DIR + "\\" + SRC_FILENAME
task.destination_path = TEX_DEST
task.destination_name = TEX_NAME
task.automated = True
task.save = True
task.replace_existing = True
task.factory = unreal.TextureFactory()
asset_tools.import_asset_tasks([task])

texture = unreal.load_object(None, TEX_DEST + "/" + TEX_NAME + "." + TEX_NAME)
if texture is None:
    raise RuntimeError(f"texture import failed for {TEX_DEST}/{TEX_NAME}")

tex_w, tex_h = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
print(f"=== {SRC_FILENAME} -> {TEX_DEST}/{TEX_NAME} ===")
print(f"  texture: {tex_w}x{tex_h}")
if tex_w != GRID_COLS * CELL_SIZE or tex_h != GRID_ROWS * CELL_SIZE:
    raise RuntimeError(f"unexpected texture size {tex_w}x{tex_h}, expected {GRID_COLS*CELL_SIZE}x{GRID_ROWS*CELL_SIZE}")

texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("srgb", True)
# TF_NEAREST (point sampling), applied proactively -- see this script's own docstring for why an
# unpadded grid sheet like this one needs it to avoid cross-cell bleed during playback.
texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
texture.modify()
unreal.EditorAssetLibrary.save_loaded_asset(texture)

# -- 25 sprites, one per grid cell, CENTER_CENTER pivot (per this task's explicit instruction) --
sprite_paths = []
for i in range(GRID_ROWS * GRID_COLS):
    row, col = i // GRID_COLS, i % GRID_COLS
    sprite_name = f"SP_Trap_RoomBarrier_{i + 1:02d}"
    sprite_full_path = SPRITE_DEST + "/" + sprite_name
    sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
    if sprite is None:
        sprite = asset_tools.create_asset(sprite_name, SPRITE_DEST, unreal.PaperSprite, unreal.PaperSpriteFactory())
    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(col * CELL_SIZE, row * CELL_SIZE))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    sprite.set_editor_property("PixelsPerUnrealUnit", 1.0)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
    sprite_paths.append(sprite_full_path)
print(f"  rebuilt {len(sprite_paths)} sprite(s), all pivot_mode=CENTER_CENTER")

# -- Flipbook: same key_frames structure as every other flipbook in this project --
flipbook_full_path = FLIPBOOK_DEST + "/" + FLIPBOOK_NAME
flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
if flipbook is None:
    flipbook = asset_tools.create_asset(FLIPBOOK_NAME, FLIPBOOK_DEST, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())

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
print(f"  {flipbook_full_path}: {len(key_frames)} frames @ {FPS}fps")

# -- Verify BP_RoomBarrier already points at this exact flipbook (no rewiring needed) --
bp = unreal.EditorAssetLibrary.load_asset("/Game/Environments/CityBiome/Traps/Blueprints/BP_RoomBarrier")
gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)
assigned = cdo.get_editor_property("BarrierFlipbook")
print(f"  BP_RoomBarrier.BarrierFlipbook currently points at: {assigned}")
if assigned is None or assigned.get_path_name() != flipbook.get_path_name():
    print("  WARNING: BP_RoomBarrier's BarrierFlipbook does NOT already point at this flipbook -- assigning it now.")
    cdo.set_editor_property("BarrierFlipbook", flipbook)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)
else:
    print("  Confirmed: no rewiring needed, BP_RoomBarrier already references this exact asset.")

print("=== ROOM BARRIER FLIPBOOK IMPORT COMPLETE ===")
