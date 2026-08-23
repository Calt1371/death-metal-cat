"""
ue_resize_and_fix_fancy_cayde.py

Two fixes for the "riding Fancy Pants" ultimate, from live playtesting feedback:

1. Sizing: the mount read as barely bigger than solo Cayde on screen -- a direct side-by-side
   screenshot comparison (same crop/scale) confirmed it, matching the report. Re-imports all three
   Fancy flipbooks (Idle/Gallop/Attack) from their source PNGs physically upscaled 1.5x (1280->1920,
   LANCZOS resample) and re-slices with a proportionally scaled cell size (256->384), holding
   PixelsPerUnrealUnit at the existing 1.0 -- same proven mechanism as every other size correction
   this project has used (PPU mutation alone doesn't reliably change render size; physically
   resizing the source + scaling cell_size does). All three scaled by the identical 1.5x factor, so
   they stay consistent with each other exactly as before, just bigger overall.

2. Facing: FancyGallop specifically rendered backwards while moving -- same class of bug as the
   original Run/Backflip mirroring issue earlier this session (an art batch authored facing the
   opposite convention). Confirmed independently for THIS flipbook rather than assumed: FancyIdle
   and FancyAttack were already live-tested correct (no pre-flip needed) during the original import,
   so only Gallop's source was horizontally mirrored here, not the whole batch.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_resize_and_fix_fancy_cayde.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\_resized_cache"
TEX_DEST = "/Game/Characters/DeathMetalCat/Textures"
FLIPBOOK_DEST = "/Game/Characters/DeathMetalCat/Flipbooks"
GRID_ROWS = 5
GRID_COLS = 5
NEW_CELL_SIZE = 384  # 256 * 1.5

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

JOBS = [
    ("FancyIdle_resized.png", "T_DeathMetalCat_FancyIdle", "Sprites/FancyIdle", "SP_DeathMetalCat_FancyIdle", "FB_DeathMetalCat_FancyIdle"),
    ("FancyGallop_resized.png", "T_DeathMetalCat_FancyGallop", "Sprites/FancyGallop", "SP_DeathMetalCat_FancyGallop", "FB_DeathMetalCat_FancyGallop"),
    ("FancyAttack_resized.png", "T_DeathMetalCat_FancyAttack", "Sprites/FancyAttack", "SP_DeathMetalCat_FancyAttack", "FB_DeathMetalCat_FancyAttack"),
]

for src_filename, tex_name, sprite_subfolder, sprite_prefix, flipbook_name in JOBS:
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

    tex_w = texture.blueprint_get_size_x()
    unreal.log(f"texture re-imported: {tex_full_path} ({tex_w}x{tex_w})")

    sprite_dest = f"/Game/Characters/DeathMetalCat/{sprite_subfolder}"
    sprite_paths = []
    for i in range(GRID_ROWS * GRID_COLS):
        row = i // GRID_COLS
        col = i % GRID_COLS
        frame_num = i + 1
        sprite_name = f"{sprite_prefix}_{frame_num:02d}"
        sprite_full_path = sprite_dest + "/" + sprite_name

        uv_x, uv_y = col * NEW_CELL_SIZE, row * NEW_CELL_SIZE
        if unreal.EditorAssetLibrary.does_asset_exist(sprite_full_path):
            sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
        else:
            sprite = asset_tools.create_asset(sprite_name, sprite_dest, unreal.PaperSprite, unreal.PaperSpriteFactory())
        sprite.set_editor_property("source_texture", texture)
        sprite.set_editor_property("source_uv", unreal.Vector2D(uv_x, uv_y))
        sprite.set_editor_property("source_dimension", unreal.Vector2D(NEW_CELL_SIZE, NEW_CELL_SIZE))
        sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
        sprite.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(sprite)
        sprite_paths.append(sprite_full_path)

    unreal.log(f"  {len(sprite_paths)} sprites rebuilt at cell_size={NEW_CELL_SIZE} in {sprite_dest}")

    flipbook_full_path = FLIPBOOK_DEST + "/" + flipbook_name
    flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
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

    unreal.log(f"  {flipbook_full_path}: {len(key_frames)} frames @ {old_fps}fps (unchanged)")

unreal.log("=== FANCY CAYDE RESIZE + GALLOP FACING FIX COMPLETE ===")
