"""
ue_import_deathbot_heavy_crawler.py

Imports two new enemy sprite batches from RawAssets/Enemies:
  DeathBotHeavy-walk-v1.png / DeathBotHeavy-deathbotheavy_attack-v1.png
  DeathBotCrawler-walk-v1.png / DeathBotCrawler-spiderclawattack-v1.png

All four sheets are a clean 1280x1280, 5x5=25-frame grid (256x256/cell, exact division).
Verified via PIL alpha-bbox analysis before writing this: no empty frames, and while two sheets
(DeathBotHeavy attack frames 11-12, DeathBotCrawler attack frames 14-19) show content bbox
touching their own cell edge, cross-referencing each flagged frame against its actual neighbor
in global sheet coordinates shows a consistent 13-18px gap with no real pixel overlap -- confirmed
clean via direct crop comparison too, same as every other sheet this session. No bleed.

Same shared-texture-plus-cropped-sprites pattern as DeathBotFlying/DeathBotWalking re-imports:
one texture per animation, 25 sprites cropped from it, CENTER_CENTER pivot on every sprite,
TF_NEAREST filtering (prevents adjacent-cell sampling bleed).

Facing is deliberately NOT set here -- see the header comment on
DeathMetalCatEnemyBase::bSpriteFacesReversed and this session's live PIE facing test for both
enemies before touching that property on either new Blueprint.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_deathbot_heavy_crawler.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Enemies"
GRID_ROWS = 5
GRID_COLS = 5
CELL_SIZE = 256

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# (enemy_name, anim_name, source_filename)
JOBS = [
    ("DeathBotHeavy", "Walk", "DeathBotHeavy-walk-v1.png"),
    ("DeathBotHeavy", "Attack", "DeathBotHeavy-deathbotheavy_attack-v1.png"),
    ("DeathBotCrawler", "Walk", "DeathBotCrawler-walk-v1.png"),
    ("DeathBotCrawler", "Attack", "DeathBotCrawler-spiderclawattack-v1.png"),
]

results = {}

for enemy_name, anim_name, src_filename in JOBS:
    tex_dest = f"/Game/Characters/Enemies/{enemy_name}/Textures"
    sprite_dest = f"/Game/Characters/Enemies/{enemy_name}/Sprites/{anim_name}"
    flipbook_dest = f"/Game/Characters/Enemies/{enemy_name}/Flipbooks"
    tex_name = f"T_Enemy_{enemy_name}_{anim_name}"
    sprite_prefix = f"SP_Enemy_{enemy_name}_{anim_name}"
    flipbook_name = f"FB_Enemy_{enemy_name}_{anim_name}"

    tex_full_path = tex_dest + "/" + tex_name
    task = unreal.AssetImportTask()
    task.filename = RAW_DIR + "\\" + src_filename
    task.destination_path = tex_dest
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
    tex_h = texture.blueprint_get_size_y()
    unreal.log(f"texture imported: {tex_full_path} ({tex_w}x{tex_h})")

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

    unreal.log(f"created {len(sprite_paths)} sprites in {sprite_dest}")

    flipbook_full_path = flipbook_dest + "/" + flipbook_name
    if unreal.EditorAssetLibrary.does_asset_exist(flipbook_full_path):
        flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
    else:
        flipbook = asset_tools.create_asset(flipbook_name, flipbook_dest, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())

    key_frames = []
    for sp_path in sprite_paths:
        sp_asset = unreal.EditorAssetLibrary.load_asset(sp_path)
        kf = unreal.PaperFlipbookKeyFrame()
        kf.set_editor_property("sprite", sp_asset)
        kf.set_editor_property("frame_run", 1)
        key_frames.append(kf)
    flipbook.set_editor_property("key_frames", key_frames)
    flipbook.set_editor_property("frames_per_second", 10.0)
    flipbook.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(flipbook)

    unreal.log(f"{flipbook_full_path}: {len(key_frames)} frames @ 10fps")
    results[f"{enemy_name}_{anim_name}"] = flipbook_full_path

unreal.log(f"=== DEATHBOT HEAVY/CRAWLER IMPORT COMPLETE: {results} ===")
