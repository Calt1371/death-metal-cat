"""
ue_import_cayde_hurt_run.py

Two more straightforward Cayde reimports, folded into the same animation-overhaul pass as
ue_import_cayde_overhaul.py (2026-08-22): Cayde-hero_hurt.png -> HurtFlipbook, Cayde-hero_run.png
-> RunFlipbook. Filenames checked directly on disk (RawAssets/Allies/) rather than assumed --
neither has a "-vN" suffix, unlike the rest of the batch. Both 1280x1280, a clean 5x5=25-frame
grid (256x256/cell), confirmed via PIL.

Both are ENGINE-AUTOPLAYED (no code-side discrete frame indexing, unlike Dodge/Jump from the
earlier pass), so straight sheet order for all 25 frames is safe:
  - HurtFlipbook: SetLooping(false) + PlayFromStart() once per hit, in TakeDamage.
  - RunFlipbook: SetLooping(true) + Play(), in UpdateAnimation's movement branch.

FPS choice differs between the two, same reasoning as the sword-combo flipbooks in the earlier
pass -- a pure animation-asset speed setting, not a gameplay mechanic, so within scope of an "art
swap":
  - Hurt: fps = 25 frames / HurtDuration(0.4s) = 62.5, so the full reaction plays out and finishes
    right as the Hurt window ends (HurtDuration/the timing mechanic itself is untouched). Keeping
    the old fps=15 here would have looked far more truncated than before -- the old 2-frame sheet
    fully played twice over within 0.4s (2/15=0.13s), where 25 frames at 15fps take 1.67s and would
    get cut off after showing only ~6 of them.
  - Run: fps=12, UNCHANGED from the existing value -- Run is a continuous loop with no fixed
    window to fit (same reasoning as Idle/AirDownShot/WallSlide/HoldFire from the earlier pass),
    and 12fps matches WalkFlipbook's own fps for a consistent locomotion feel between the two.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_cayde_hurt_run.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Allies"
TEX_DEST = "/Game/Characters/DeathMetalCat/Textures"
SPRITE_DEST_BASE = "/Game/Characters/DeathMetalCat/Sprites"
FLIPBOOK_DEST = "/Game/Characters/DeathMetalCat/Flipbooks"

GRID_COLS = 5
CELL_SIZE = 256

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_texture(src_filename, tex_name):
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
    return texture


def make_sprite(texture, sprite_dest, sprite_name, cell_index):
    row, col = cell_index // GRID_COLS, cell_index % GRID_COLS
    uv_x, uv_y = col * CELL_SIZE, row * CELL_SIZE
    sprite_full_path = sprite_dest + "/" + sprite_name
    if unreal.EditorAssetLibrary.does_asset_exist(sprite_full_path):
        unreal.EditorAssetLibrary.delete_asset(sprite_full_path)
    sprite = asset_tools.create_asset(sprite_name, sprite_dest, unreal.PaperSprite, unreal.PaperSpriteFactory())
    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(uv_x, uv_y))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
    return sprite_full_path


def delete_old_sprites(sprite_dest, prefix, count):
    removed = 0
    for i in range(1, count + 1):
        p = f"{sprite_dest}/{prefix}_{i:02d}"
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            unreal.EditorAssetLibrary.delete_asset(p)
            removed += 1
    return removed


def import_full_sheet(src_filename, tex_name, sprite_subfolder, sprite_prefix, flipbook_full_path,
                       fps, old_sprite_prefix, old_sprite_count):
    unreal.log(f"=== {src_filename} -> {flipbook_full_path} ===")
    sprite_dest = f"{SPRITE_DEST_BASE}/{sprite_subfolder}"

    removed = delete_old_sprites(sprite_dest, old_sprite_prefix, old_sprite_count)
    unreal.log(f"  removed {removed} old sprite(s)")

    texture = import_texture(src_filename, tex_name)
    tex_w, tex_h = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    unreal.log(f"  texture: {tex_w}x{tex_h}")

    sprite_paths = []
    for i in range(25):
        sprite_name = f"{sprite_prefix}_{i + 1:02d}"
        sprite_paths.append(make_sprite(texture, sprite_dest, sprite_name, i))
    unreal.log(f"  created {len(sprite_paths)} sprite(s)")

    flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
    if flipbook is None:
        raise RuntimeError(f"expected existing flipbook not found: {flipbook_full_path}")

    key_frames = []
    for sp_path in sprite_paths:
        sp_asset = unreal.EditorAssetLibrary.load_asset(sp_path)
        kf = unreal.PaperFlipbookKeyFrame()
        kf.set_editor_property("sprite", sp_asset)
        kf.set_editor_property("frame_run", 1)
        key_frames.append(kf)
    flipbook.set_editor_property("key_frames", key_frames)
    flipbook.set_editor_property("frames_per_second", fps)
    flipbook.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
    unreal.log(f"  {flipbook_full_path}: {len(key_frames)} frames @ {fps}fps")


import_full_sheet(
    "Cayde-hero_hurt.png", "T_DeathMetalCat_Hurt", "Hurt", "SP_DeathMetalCat_Hurt",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_Hurt", fps=62.5,
    old_sprite_prefix="SP_DeathMetalCat_Hurt", old_sprite_count=2,
)

import_full_sheet(
    "Cayde-hero_run.png", "T_DeathMetalCat_Run", "Run", "SP_DeathMetalCat_Run",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_Run", fps=12.0,
    old_sprite_prefix="SP_DeathMetalCat_Run", old_sprite_count=6,
)

unreal.log("=== CAYDE HURT/RUN REIMPORT COMPLETE ===")
