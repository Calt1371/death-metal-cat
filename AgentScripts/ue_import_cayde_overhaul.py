"""
ue_import_cayde_overhaul.py

Imports every new Cayde sprite sheet from RawAssets/Allies/ for the full animation/combat
overhaul (GOAL ASSIGNMENT, 2026-08-21): straightforward reimports (Idle/AirShot/Backflip-Dodge/
GunFire-HoldFire/Jump/WallSlide) plus brand-new flipbooks for the combo/Uppy/Double Whammy/Spinny
Down/Block/Invuln Dash system.

All sheets are 1280x1280, a clean 5x5=25-frame grid (256x256/cell) -- confirmed via PIL before
writing this script, matching every other sheet imported this project.

Two categories, handled differently:
  1. ENGINE-AUTOPLAYED flipbooks (loops or single PlayFromStart, no code-side frame indexing) --
     safe to use straight sheet order, all 25 frames. Covers: Idle, AirDownShot, WallSlide,
     HoldFire, and every brand-new combat flipbook (SwordAttack/SwordCombo2/SwordCombo3/Uppy/
     DoubleWhammy/SpinnyDown/AirShotAngled/Block/InvulnDash).
  2. CODE-DRIVEN discrete-frame-index flipbooks (Dodge: SetPlaybackPositionInFrames(0..4), Jump:
     SetPlaybackPositionInFrames(0 or 1)) -- sheet order does NOT matter, only which specific poses
     land at each required index. Frames were hand-picked by visual inspection (see grid/zoom PNGs
     in Saved/Screenshots/WindowsEditor/ from this same session) rather than assumed from sheet
     order:
       Dodge (backflip-v1): sheet frames [7, 8, 9, 12, 13] -> code indices [0,1,2,3,4]
         (7=neutral pre-windup, 8=leaning-back windup, 9=inverted mid-tumble, 12=landing crouch,
          13=standing recovery)
       Jump (jump-v1): sheet frames [13, 20] -> code indices [0,1]
         (13=dynamic forward-leaning leap -> "rising", 20=more compact tucked pose -> "falling".
          This sheet's 25 frames are all a similar gun-ready leap pose with only subtle leg-tuck
          variation -- nowhere near as visually distinct as the old 2-frame rising/falling pair, so
          this pick is a reasonable-effort visual judgment call, not a certain match. Flagged in the
          summary report as worth a live look.)

Cayde-hero_gun_fire-v1 is used ONLY for HoldFireFlipbook (the sustained standing-fire loop) -- it
has no distinct "draw" transition pose (just one consistent full-extension stance with periodic
muzzle flash, same in kind as the old dedicated HoldFire sheet, just higher quality/frame count),
so ShootFlipbook (the brief quick-draw pose sourced from the old shared sprite sheet, frame index 2
via ShootFrame_Draw) is deliberately left untouched -- not in scope of "replaces current standing
gunfire flipbook" and there's no new art for it anyway.

Cayde-hero_zoomy_paw-v1.png exists in RawAssets/Allies/ but is NOT referenced anywhere in the
assignment -- left unused/unimported, flagged in the summary report.

New-flipbook FPS choices (all placeholder, tune freely):
  - Straightforward replacements keep their EXISTING fps unchanged (matches established
    reimport precedent from tonight's DeathBot work) -- Idle=10, AirDownShot=15, WallSlide=10,
    HoldFire=15, Dodge/Jump=15 (moot -- code-driven, ignores engine playback speed).
  - Brand-new one-shot sword variants (SwordAttack/SwordCombo2/SwordCombo3/Uppy/DoubleWhammy/
    SpinnyDown): fps = 25 frames / AttackDuration(0.4s) = 62.5, so the full 25-frame swing
    finishes right as the attack's existing lockout window ends, rather than being cut off
    mid-animation. This is fast for a 2D flipbook -- whether it reads as smooth motion or a blur at
    that framerate needs real playtesting; the straightforward fix if it looks wrong is to raise
    AttackDuration (and HitboxActiveDelay/HitboxActiveDuration proportionally) rather than drop
    frames from the sheet.
  - InvulnDash: fps = 25 / InvulnDashDuration(0.3s) = 83.33, same reasoning.
  - AirShotAngled (loops): matches AirDownShotFlipbook's existing 15fps for consistency.
  - Block (loops): 10fps, matching Idle's held-pose cadence -- placeholder, tune freely.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_cayde_overhaul.py').read())"
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


def build_flipbook(flipbook_full_path, sprite_paths, fps, create_new):
    if create_new:
        fb_name = flipbook_full_path.rsplit("/", 1)[-1]
        fb_dest = flipbook_full_path.rsplit("/", 1)[0]
        if unreal.EditorAssetLibrary.does_asset_exist(flipbook_full_path):
            flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
        else:
            flipbook = asset_tools.create_asset(fb_name, fb_dest, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())
    else:
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


def delete_old_sprites(sprite_dest, prefix, count):
    removed = 0
    for i in range(1, count + 1):
        p = f"{sprite_dest}/{prefix}_{i:02d}"
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            unreal.EditorAssetLibrary.delete_asset(p)
            removed += 1
    return removed


def import_full_sheet(src_filename, tex_name, sprite_subfolder, sprite_prefix, flipbook_full_path,
                       fps, create_new_flipbook, old_sprite_prefix=None, old_sprite_count=25,
                       frame_indices=None):
    """frame_indices: None = all 25 in sheet order; otherwise an explicit list of sheet indices
    (0-24) to use, in the order they should appear as flipbook key_frames."""
    unreal.log(f"=== {src_filename} -> {flipbook_full_path} ===")
    sprite_dest = f"{SPRITE_DEST_BASE}/{sprite_subfolder}"

    if old_sprite_prefix:
        removed = delete_old_sprites(sprite_dest, old_sprite_prefix, old_sprite_count)
        unreal.log(f"  removed {removed} old sprite(s)")

    texture = import_texture(src_filename, tex_name)
    tex_w, tex_h = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    unreal.log(f"  texture: {tex_w}x{tex_h}")

    indices = frame_indices if frame_indices is not None else list(range(25))
    sprite_paths = []
    for out_i, cell_index in enumerate(indices):
        sprite_name = f"{sprite_prefix}_{out_i + 1:02d}"
        sprite_paths.append(make_sprite(texture, sprite_dest, sprite_name, cell_index))
    unreal.log(f"  created {len(sprite_paths)} sprite(s)")

    build_flipbook(flipbook_full_path, sprite_paths, fps, create_new_flipbook)


# --- Step 1: straightforward reimports (all 25 frames, engine-autoplayed, existing FPS kept) ---

import_full_sheet(
    "Cayde-hero_aggressive_idle-v1.png", "T_DeathMetalCat_Idle", "Idle", "SP_DeathMetalCat_Idle",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_Idle", fps=10.0, create_new_flipbook=False,
    old_sprite_prefix="SP_DeathMetalCat_Idle", old_sprite_count=3,
)

import_full_sheet(
    "Cayde-hero_air_shot-v1.png", "T_DeathMetalCat_AirDownShot", "AirDownShot", "SP_DeathMetalCat_AirDownShot",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_AirDownShot", fps=15.0, create_new_flipbook=False,
    old_sprite_prefix="SP_DeathMetalCat_AirDownShot", old_sprite_count=4,
)

import_full_sheet(
    "Cayde-hero_wall_slide-v1.png", "T_DeathMetalCat_WallSlide", "WallSlide", "SP_DeathMetalCat_WallSlide",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_WallSlide", fps=10.0, create_new_flipbook=False,
    old_sprite_prefix="SP_DeathMetalCat_WallSlide", old_sprite_count=4,
)

import_full_sheet(
    "Cayde-hero_gun_fire-v1.png", "T_DeathMetalCat_HoldFire", "HoldFire", "SP_DeathMetalCat_HoldFire",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_HoldFire", fps=15.0, create_new_flipbook=False,
    old_sprite_prefix="SP_DeathMetalCat_HoldFire", old_sprite_count=4,
)

# Dodge/Backflip: code-driven discrete frames -- hand-picked sheet indices, see module docstring.
import_full_sheet(
    "Cayde-hero_backflip-v1.png", "T_DeathMetalCat_Dodge", "Dodge", "SP_DeathMetalCat_Dodge",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_Dodge", fps=15.0, create_new_flipbook=False,
    old_sprite_prefix="SP_DeathMetalCat_Dodge", old_sprite_count=5,
    frame_indices=[7, 8, 9, 12, 13],
)

# Jump: code-driven discrete frames (0=rising,1=falling) -- hand-picked sheet indices, see docstring.
import_full_sheet(
    "Cayde-hero_jump-v1.png", "T_DeathMetalCat_Jump", "Jump", "SP_DeathMetalCat_Jump",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_Jump", fps=15.0, create_new_flipbook=False,
    old_sprite_prefix="SP_DeathMetalCat_Jump", old_sprite_count=2,
    frame_indices=[13, 20],
)

# --- Step 4: sword-v2 replaces the base swing (existing FB_DeathMetalCat_SwordAttack path) ---

import_full_sheet(
    "Cayde-hero_sword-v2.png", "T_DeathMetalCat_SwordAttack", "SwordAttack", "SP_DeathMetalCat_SwordAttack",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_SwordAttack", fps=62.5, create_new_flipbook=False,
    old_sprite_prefix="SP_DeathMetalCat_SwordAttack", old_sprite_count=4,
)

# --- New combat flipbooks (brand new PaperFlipbook assets) ---

import_full_sheet(
    "Cayde-hero_sword_2-v1.png", "T_DeathMetalCat_SwordCombo2", "SwordCombo2", "SP_DeathMetalCat_SwordCombo2",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_SwordCombo2", fps=62.5, create_new_flipbook=True,
)

import_full_sheet(
    "Cayde-hero_sword_3-v1.png", "T_DeathMetalCat_SwordCombo3", "SwordCombo3", "SP_DeathMetalCat_SwordCombo3",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_SwordCombo3", fps=62.5, create_new_flipbook=True,
)

import_full_sheet(
    "Cayde-hero_uppy-v1.png", "T_DeathMetalCat_Uppy", "Uppy", "SP_DeathMetalCat_Uppy",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_Uppy", fps=62.5, create_new_flipbook=True,
)

import_full_sheet(
    "Cayde-hero_double_whammy-v1.png", "T_DeathMetalCat_DoubleWhammy", "DoubleWhammy", "SP_DeathMetalCat_DoubleWhammy",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_DoubleWhammy", fps=62.5, create_new_flipbook=True,
)

import_full_sheet(
    "Cayde-hero_the_one_spinny_down_thing-v1.png", "T_DeathMetalCat_SpinnyDown", "SpinnyDown", "SP_DeathMetalCat_SpinnyDown",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_SpinnyDown", fps=62.5, create_new_flipbook=True,
)

import_full_sheet(
    "Cayde-hero_air_shot_angled-v1.png", "T_DeathMetalCat_AirShotAngled", "AirShotAngled", "SP_DeathMetalCat_AirShotAngled",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_AirShotAngled", fps=15.0, create_new_flipbook=True,
)

import_full_sheet(
    "Cayde-hero_block-v3.png", "T_DeathMetalCat_Block", "Block", "SP_DeathMetalCat_Block",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_Block", fps=10.0, create_new_flipbook=True,
)

import_full_sheet(
    "Cayde-hero_invuln_dash-v1.png", "T_DeathMetalCat_InvulnDash", "InvulnDash", "SP_DeathMetalCat_InvulnDash",
    FLIPBOOK_DEST + "/FB_DeathMetalCat_InvulnDash", fps=83.33, create_new_flipbook=True,
)

unreal.log("=== CAYDE OVERHAUL ASSET IMPORT COMPLETE ===")
