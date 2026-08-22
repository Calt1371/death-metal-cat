"""
ue_fix_cayde_facing_flip.py

Fixes Task 2 findings #1 (Run facing backward) and #2 (Backflip moving forward instead of
backward) -- confirmed to share ONE root cause: every flipbook imported from RawAssets/Allies/
this session rendered mirrored relative to the project's established convention (confirmed live:
old ShootFlipbook renders facing right at Scale.X=+1; new Idle/Run rendered facing left at the
same Scale.X=+1; pre-flipping the source PNG before import corrected it).

Re-imports all 17 affected flipbooks in place (same destination paths, same hand-picked frame
indices for Dodge/Jump where applicable -- flipping the whole sheet doesn't change which grid cell
holds which pose, only mirrors its content) from AgentScripts/_flipped_cache/ (horizontally-flipped
copies of the originals, generated via PIL -- RawAssets/Allies/ itself is left untouched).

No C++ changes needed. Movement code (HandleDodge's -FacingSign impulse, etc.) was already correct
relative to Scale.X's convention -- it just needed the ART to finally match that convention.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_cayde_facing_flip.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\_flipped_cache"
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


def make_sprite(texture, sprite_dest, sprite_name, cell_index, ppu):
    row, col = cell_index // GRID_COLS, cell_index % GRID_COLS
    uv_x, uv_y = col * CELL_SIZE, row * CELL_SIZE
    sprite_full_path = sprite_dest + "/" + sprite_name
    # Load-and-mutate in place rather than delete-then-create: the previous sprite asset can still
    # be referenced elsewhere in memory (e.g. by the currently-loaded flipbook), which makes
    # delete_asset silently no-op and leaves create_asset unable to claim the same path.
    sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
    if sprite is None:
        sprite = asset_tools.create_asset(sprite_name, sprite_dest, unreal.PaperSprite, unreal.PaperSpriteFactory())
    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(uv_x, uv_y))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    sprite.set_editor_property("PixelsPerUnrealUnit", ppu)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
    return sprite_full_path


def reimport_flipbook(src_filename, tex_name, sprite_subfolder, sprite_prefix, flipbook_full_path,
                       frame_indices=None, ppu=1.0):
    unreal.log(f"=== {src_filename} -> {flipbook_full_path} ===")
    sprite_dest = f"{SPRITE_DEST_BASE}/{sprite_subfolder}"

    texture = import_texture(src_filename, tex_name)
    tex_w, tex_h = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    unreal.log(f"  texture: {tex_w}x{tex_h}")

    indices = frame_indices if frame_indices is not None else list(range(25))
    sprite_paths = []
    for out_i, cell_index in enumerate(indices):
        sprite_name = f"{sprite_prefix}_{out_i + 1:02d}"
        sprite_paths.append(make_sprite(texture, sprite_dest, sprite_name, cell_index, ppu))
    unreal.log(f"  rebuilt {len(sprite_paths)} sprite(s), PPU={ppu}")

    flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
    if flipbook is None:
        raise RuntimeError(f"expected existing flipbook not found: {flipbook_full_path}")

    old_fps = flipbook.get_editor_property("frames_per_second")
    key_frames = []
    for sp_path in sprite_paths:
        sp_asset = unreal.EditorAssetLibrary.load_asset(sp_path)
        kf = unreal.PaperFlipbookKeyFrame()
        kf.set_editor_property("sprite", sp_asset)
        kf.set_editor_property("frame_run", 1)
        key_frames.append(kf)
    flipbook.set_editor_property("key_frames", key_frames)
    # fps intentionally left unchanged -- this pass only fixes orientation, not timing.
    flipbook.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
    unreal.log(f"  {flipbook_full_path}: {len(key_frames)} frames @ {old_fps}fps (unchanged)")


# Idle keeps its established PPU=1.5 baseline (Task 1) -- everything else stays at the current
# PPU=1.0 default for now; Task 1's normalization pass (separate script) adjusts those afterward.
reimport_flipbook("Cayde-hero_aggressive_idle-v1.png", "T_DeathMetalCat_Idle", "Idle", "SP_DeathMetalCat_Idle",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_Idle", ppu=1.5)

reimport_flipbook("Cayde-hero_air_shot-v1.png", "T_DeathMetalCat_AirDownShot", "AirDownShot", "SP_DeathMetalCat_AirDownShot",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_AirDownShot")

reimport_flipbook("Cayde-hero_wall_slide-v1.png", "T_DeathMetalCat_WallSlide", "WallSlide", "SP_DeathMetalCat_WallSlide",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_WallSlide")

reimport_flipbook("Cayde-hero_gun_fire-v1.png", "T_DeathMetalCat_HoldFire", "HoldFire", "SP_DeathMetalCat_HoldFire",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_HoldFire")

# Dodge/Backflip: same hand-picked sheet indices as before (flipping doesn't move grid cells).
reimport_flipbook("Cayde-hero_backflip-v1.png", "T_DeathMetalCat_Dodge", "Dodge", "SP_DeathMetalCat_Dodge",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_Dodge", frame_indices=[7, 8, 9, 12, 13])

# Jump: same hand-picked sheet indices as before.
reimport_flipbook("Cayde-hero_jump-v1.png", "T_DeathMetalCat_Jump", "Jump", "SP_DeathMetalCat_Jump",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_Jump", frame_indices=[13, 20])

reimport_flipbook("Cayde-hero_sword-v2.png", "T_DeathMetalCat_SwordAttack", "SwordAttack", "SP_DeathMetalCat_SwordAttack",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_SwordAttack")

reimport_flipbook("Cayde-hero_sword_2-v1.png", "T_DeathMetalCat_SwordCombo2", "SwordCombo2", "SP_DeathMetalCat_SwordCombo2",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_SwordCombo2")

reimport_flipbook("Cayde-hero_sword_3-v1.png", "T_DeathMetalCat_SwordCombo3", "SwordCombo3", "SP_DeathMetalCat_SwordCombo3",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_SwordCombo3")

reimport_flipbook("Cayde-hero_uppy-v1.png", "T_DeathMetalCat_Uppy", "Uppy", "SP_DeathMetalCat_Uppy",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_Uppy")

reimport_flipbook("Cayde-hero_double_whammy-v1.png", "T_DeathMetalCat_DoubleWhammy", "DoubleWhammy", "SP_DeathMetalCat_DoubleWhammy",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_DoubleWhammy")

reimport_flipbook("Cayde-hero_the_one_spinny_down_thing-v1.png", "T_DeathMetalCat_SpinnyDown", "SpinnyDown", "SP_DeathMetalCat_SpinnyDown",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_SpinnyDown")

reimport_flipbook("Cayde-hero_air_shot_angled-v1.png", "T_DeathMetalCat_AirShotAngled", "AirShotAngled", "SP_DeathMetalCat_AirShotAngled",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_AirShotAngled")

reimport_flipbook("Cayde-hero_block-v3.png", "T_DeathMetalCat_Block", "Block", "SP_DeathMetalCat_Block",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_Block")

reimport_flipbook("Cayde-hero_invuln_dash-v1.png", "T_DeathMetalCat_InvulnDash", "InvulnDash", "SP_DeathMetalCat_InvulnDash",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_InvulnDash")

reimport_flipbook("Cayde-hero_hurt.png", "T_DeathMetalCat_Hurt", "Hurt", "SP_DeathMetalCat_Hurt",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_Hurt")

reimport_flipbook("Cayde-hero_run.png", "T_DeathMetalCat_Run", "Run", "SP_DeathMetalCat_Run",
                   FLIPBOOK_DEST + "/FB_DeathMetalCat_Run")

unreal.log("=== CAYDE FACING FLIP FIX COMPLETE (17 flipbooks) ===")
