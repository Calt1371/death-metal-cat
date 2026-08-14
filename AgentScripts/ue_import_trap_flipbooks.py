"""
ue_import_trap_flipbooks.py

Imports the 4 trap sprite sheets from RawAssets/Meshy_Traps/ into
Content/Environments/CityBiome/Traps/ as PaperFlipbook assets.

Grid/frame-count per sheet determined per-sheet (not assumed uniform), via a combination of
programmatic row/column content-density analysis (looking for low-variance "gutter" bands at
candidate equal-division boundaries) AND visual inspection -- see Docs/Trap_Flipbook_Import_Log.md
for the full reasoning per trap. All 4 happen to have 8 frames, but on two different grid
orientations (electric/saw/spike_floor are 4 rows x 2 cols; spike_column is 2 rows x 4 cols).

CONVENTION NOTE: follows the real established pattern in this project (confirmed by inspecting
FB_Enemy_DeathBotFlying_Idle / SP_Enemy_DeathBotFlying_Idle_01..05) -- ONE shared Texture2D per
sheet, with each frame being a separate PaperSprite that points at a different source_uv/
source_dimension sub-region of that SAME texture, rather than pre-slicing into separate cropped
image files. This is simpler (no pixel-cropping step needed) and matches what's already in the
project, rather than a literal-but-wasteful "slice into N image files" reading.

Does NOT place anything in the level -- import and flipbook-assembly only.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_trap_flipbooks.py').read())"
"""

import unreal

SRC_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Meshy_Traps"
DEST = "/Game/Environments/CityBiome/Traps"

# (source filename, clean name, rows, cols, cell_w, cell_h, fps)
TRAPS = [
    ("electric_trap_flipbook.png", "Electric", 4, 2, 768, 256, 15.0),
    ("saw_trap_flip_book.png", "Saw", 4, 2, 768, 256, 12.0),
    ("spike_column_flip_book.png", "SpikeColumn", 2, 4, 384, 512, 10.0),
    ("spike_floor_flipbook.png", "SpikeFloor", 4, 2, 768, 256, 10.0),
]

# Per-trap, per-frame measured local content-center-x (PIL alpha-bbox analysis of the raw source
# PNGs -- see Docs/Trap_Flipbook_Import_Log.md's first post-import fix). The source art's content
# isn't drawn perfectly re-centered in every grid cell (a per-COLUMN systematic drift, not random
# per-frame jitter), so the default CENTER_CENTER pivot makes frames visibly wobble left/right in
# playback. Each sprite's pivot is set to its OWN measured value on X; Y is left at the raw cell's
# geometric center for every frame, since the Y bbox range is mostly the effect's own height
# genuinely changing (arcs/spikes growing), not misalignment -- correcting Y the same way would
# introduce an artificial vertical bob that isn't in the source art.
MEASURED_CENTER_X = {
    "Electric": [390, 374, 388, 374, 388, 374, 387, 376],
    "Saw": [382, 381, 384, 384, 384, 384, 383, 383],
    "SpikeColumn": [237, 228, 180, 152, 234, 228, 182, 152],
    "SpikeFloor": [409, 356, 404, 356, 404, 360, 402, 358],
}

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
summary = []

for src_filename, clean_name, rows, cols, cell_w, cell_h, fps in TRAPS:
    src_path = SRC_DIR + "\\" + src_filename
    tex_name = "T_Trap_" + clean_name
    tex_full_path = DEST + "/" + tex_name

    if unreal.EditorAssetLibrary.does_asset_exist(tex_full_path):
        unreal.log_error("[TRAP IMPORT] REFUSING to overwrite pre-existing " + tex_full_path)
        summary.append((clean_name, "SKIPPED - ALREADY EXISTS", None, None))
        continue

    task = unreal.AssetImportTask()
    task.filename = src_path
    task.destination_path = DEST
    task.destination_name = tex_name
    task.automated = True
    task.save = True
    task.replace_existing = False
    task.factory = unreal.TextureFactory()
    asset_tools.import_asset_tasks([task])

    texture = unreal.EditorAssetLibrary.load_asset(tex_full_path)
    if texture is None:
        unreal.log_error("[TRAP IMPORT] texture import FAILED for " + tex_full_path)
        summary.append((clean_name, "TEXTURE IMPORT FAILED", None, None))
        continue

    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    # Point/nearest filtering, not the default bilinear -- these sheets have no padding/gutter
    # between grid cells, so bilinear sampling near a frame's UV edge blends in texels from the
    # NEXT cell over, visible in playback as a fragment of a neighboring frame bleeding in. See
    # Docs/Trap_Flipbook_Import_Log.md's second post-import fix for the full diagnosis.
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)

    tex_w = texture.blueprint_get_size_x()
    tex_h = texture.blueprint_get_size_y()

    sprite_paths = []
    for i in range(rows * cols):
        row = i // cols
        col = i % cols
        frame_num = i + 1
        sprite_name = f"SP_Trap_{clean_name}_{frame_num:02d}"
        sprite_full_path = DEST + "/" + sprite_name

        if unreal.EditorAssetLibrary.does_asset_exist(sprite_full_path):
            unreal.log_error("[TRAP IMPORT] REFUSING to overwrite pre-existing " + sprite_full_path)
            continue

        uv_x, uv_y = col * cell_w, row * cell_h
        sprite = asset_tools.create_asset(sprite_name, DEST, unreal.PaperSprite, unreal.PaperSpriteFactory())
        sprite.set_editor_property("source_texture", texture)
        sprite.set_editor_property("source_uv", unreal.Vector2D(uv_x, uv_y))
        sprite.set_editor_property("source_dimension", unreal.Vector2D(cell_w, cell_h))
        # Corrected pivot -- see MEASURED_CENTER_X comment above.
        pivot_x = uv_x + MEASURED_CENTER_X[clean_name][i]
        pivot_y = uv_y + cell_h / 2.0
        sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CUSTOM)
        sprite.set_editor_property("custom_pivot_point", unreal.Vector2D(pivot_x, pivot_y))
        sprite.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(sprite)
        sprite_paths.append(sprite_full_path)
        unreal.log(f"[TRAP IMPORT] {sprite_full_path} uv=({uv_x},{uv_y}) dim=({cell_w},{cell_h}) pivot=({pivot_x},{pivot_y})")

    fb_name = "FB_Trap_" + clean_name
    fb_full_path = DEST + "/" + fb_name
    if unreal.EditorAssetLibrary.does_asset_exist(fb_full_path):
        unreal.log_error("[TRAP IMPORT] REFUSING to overwrite pre-existing " + fb_full_path)
        summary.append((clean_name, "FLIPBOOK ALREADY EXISTS", f"{tex_w}x{tex_h}", None))
        continue

    flipbook = asset_tools.create_asset(fb_name, DEST, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())
    flipbook.set_editor_property("frames_per_second", fps)
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

    unreal.log_warning(f"[TRAP IMPORT] {fb_full_path}: {len(sprite_paths)} frames @ {fps} fps")
    summary.append((clean_name, "OK", f"{tex_w}x{tex_h}", fb_full_path))

unreal.log_warning("=== TRAP FLIPBOOK IMPORT SUMMARY ===")
for clean_name, status, dims, fb_path in summary:
    unreal.log_warning(f"  {clean_name}: {status}  sheet_dims={dims}  flipbook={fb_path}")
