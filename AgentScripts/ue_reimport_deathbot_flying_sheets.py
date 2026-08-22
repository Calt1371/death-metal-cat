"""
ue_reimport_deathbot_flying_sheets.py

Replaces DeathBotFlying's Idle and Shoot art with two newly-authored sheets:
  RawAssets/Enemies/death_bot_flying_transparent.png          -> Idle (static body + thruster flicker)
  RawAssets/Enemies/DeathbotFlying-deathbothoverfire-v1.png   -> Shoot (arm-raise + muzzle-flash + return)

Both are 1280x1280, a clean 5x5=25-frame grid (256x256 per cell, exact division, no gutter) --
a completely different layout from the old 5-frame/397x396 sheet, confirmed via PIL bbox analysis
before writing this (not assumed). Confirmed via the same analysis that no frame's content bleeds
across its own cell boundary into a neighbor.

Follows the established project convention (see ue_import_trap_flipbooks.py): one shared Texture2D
per sheet, each frame a separate PaperSprite pointing at a source_uv/source_dimension sub-region,
TF_NEAREST filtering (not the default bilinear) specifically to prevent adjacent-frame bleed at
crop edges during sampling -- the exact category of symptom this whole investigation started from.
All frames get CENTER_CENTER pivot, matching every other correctly-behaving flipbook in the project
(Cayde, the fixed DeathBotWalking walk cycle, the fixed traps).

The FLIPBOOK asset paths (FB_Enemy_DeathBotFlying_Idle / _Shoot) are left unchanged -- only their
key_frames list and backing texture change -- so BP_EnemyDeathBotFlying's IdleFlipbook property
(already pointing at FB_Enemy_DeathBotFlying_Idle) keeps resolving with no Blueprint edit needed.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_reimport_deathbot_flying_sheets.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Enemies"
TEX_DEST = "/Game/Characters/Enemies/DeathBotFlying/Textures"
FLIPBOOK_DEST = "/Game/Characters/Enemies/DeathBotFlying/Flipbooks"

GRID_ROWS = 5
GRID_COLS = 5
CELL_SIZE = 256

SHEETS = [
    {
        "clean_name": "Idle",
        "src_filename": "death_bot_flying_transparent.png",
        "tex_name": "T_Enemy_DeathBotFlying",
        "sprite_dest": "/Game/Characters/Enemies/DeathBotFlying/Sprites/Idle",
        "sprite_prefix": "SP_Enemy_DeathBotFlying_Idle",
        "flipbook_path": FLIPBOOK_DEST + "/FB_Enemy_DeathBotFlying_Idle",
        "old_sprite_count": 5,
    },
    {
        "clean_name": "Shoot",
        "src_filename": "DeathbotFlying-deathbothoverfire-v1.png",
        "tex_name": "T_Enemy_DeathBotFlying_Shoot",
        "sprite_dest": "/Game/Characters/Enemies/DeathBotFlying/Sprites/Shoot",
        "sprite_prefix": "SP_Enemy_DeathBotFlying_Shoot",
        "flipbook_path": FLIPBOOK_DEST + "/FB_Enemy_DeathBotFlying_Shoot",
        "old_sprite_count": 5,
    },
]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
summary = []

for sheet in SHEETS:
    clean_name = sheet["clean_name"]
    sprite_dest = sheet["sprite_dest"]
    sprite_prefix = sheet["sprite_prefix"]
    fb_path = sheet["flipbook_path"]

    # --- Remove the old (5-frame) sprites for this flipbook before creating the new 25 ---
    removed = 0
    for i in range(1, sheet["old_sprite_count"] + 1):
        old_path = f"{sprite_dest}/{sprite_prefix}_{i:02d}"
        if unreal.EditorAssetLibrary.does_asset_exist(old_path):
            unreal.EditorAssetLibrary.delete_asset(old_path)
            removed += 1
    unreal.log(f"[{clean_name}] removed {removed} old sprite(s)")

    # --- Import (replacing in place if it already exists) the new texture ---
    tex_name = sheet["tex_name"]
    tex_full_path = TEX_DEST + "/" + tex_name
    task = unreal.AssetImportTask()
    task.filename = RAW_DIR + "\\" + sheet["src_filename"]
    task.destination_path = TEX_DEST
    task.destination_name = tex_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.factory = unreal.TextureFactory()
    asset_tools.import_asset_tasks([task])

    texture = unreal.EditorAssetLibrary.load_asset(tex_full_path)
    if texture is None:
        unreal.log_error(f"[{clean_name}] TEXTURE IMPORT FAILED for {tex_full_path}")
        summary.append((clean_name, "TEXTURE IMPORT FAILED"))
        continue

    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    # Nearest, not the default bilinear -- prevents adjacent-cell texel bleed at frame edges during
    # sampling, the exact "pieces of the next frame visible" symptom from before.
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)

    tex_w = texture.blueprint_get_size_x()
    tex_h = texture.blueprint_get_size_y()
    unreal.log(f"[{clean_name}] texture imported: {tex_full_path} ({tex_w}x{tex_h})")

    # --- Create the 25 new sprites ---
    sprite_paths = []
    for i in range(GRID_ROWS * GRID_COLS):
        row = i // GRID_COLS
        col = i % GRID_COLS
        frame_num = i + 1
        sprite_name = f"{sprite_prefix}_{frame_num:02d}"
        sprite_full_path = sprite_dest + "/" + sprite_name

        uv_x, uv_y = col * CELL_SIZE, row * CELL_SIZE
        sprite = asset_tools.create_asset(sprite_name, sprite_dest, unreal.PaperSprite, unreal.PaperSpriteFactory())
        sprite.set_editor_property("source_texture", texture)
        sprite.set_editor_property("source_uv", unreal.Vector2D(uv_x, uv_y))
        sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
        sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
        sprite.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(sprite)
        sprite_paths.append(sprite_full_path)

    unreal.log(f"[{clean_name}] created {len(sprite_paths)} new sprites in {sprite_dest}")

    # --- Rebuild the EXISTING flipbook's key_frames (path unchanged, so Blueprint refs still resolve) ---
    if not unreal.EditorAssetLibrary.does_asset_exist(fb_path):
        unreal.log_error(f"[{clean_name}] flipbook does not exist at {fb_path}, cannot update")
        summary.append((clean_name, "FLIPBOOK MISSING"))
        continue

    flipbook = unreal.EditorAssetLibrary.load_asset(fb_path)
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

    unreal.log(f"[{clean_name}] {fb_path}: {len(key_frames)} frames @ {old_fps}fps (unchanged)")
    summary.append((clean_name, f"OK - {tex_w}x{tex_h}, {len(key_frames)} frames"))

unreal.log("=== DEATHBOT FLYING SHEET RE-IMPORT SUMMARY ===")
for clean_name, status in summary:
    unreal.log(f"  {clean_name}: {status}")
