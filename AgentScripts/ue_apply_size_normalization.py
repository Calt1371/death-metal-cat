"""
ue_apply_size_normalization.py

Task 1 (final approach): normalizes Cayde's apparent on-screen size across flipbooks.

IMPORTANT DISCOVERY: changing an existing PaperSprite's PixelsPerUnrealUnit via the Python API
does NOT reliably change its rendered size in this project -- confirmed via extensive A/B testing
(direct property mutation, re-ordering property sets, forcing a full delete+recreate, and even a
full editor restart to rule out an in-memory cache). The one clean, large-magnitude test that DID
show a real (if damped) effect was Idle's own 1.5-vs-1.0 A/B comparison; small corrections in the
0.88-1.05 range needed here were empirically indistinguishable from measurement noise via PPU
alone. `source_dimension` (the sprite's actual pixel size), by contrast, is a PROVEN mechanism --
it's what every single successful import this session (including the facing-flip fix) has used to
correctly control render size. So instead of touching PixelsPerUnrealUnit on these 13 flipbooks,
this script physically resizes each flipbook's (already horizontally-flipped) source image by the
measured correction factor and re-slices with a proportionally scaled cell size, holding
PixelsPerUnrealUnit at a constant 1.0 baseline throughout.

Idle itself is the one exception: its PixelsPerUnrealUnit=1.5 is a literal, explicit spec value
(not a derived correction) and was reverted/reapplied separately via ue_revert_idle_ppu15.py-style
delete+recreate.

Un-touched flipbooks (correction was ~1.0, i.e. already matched, or not worth the complexity):
  - sword_attack_flipbook, sword_combo2_flipbook (frame 3), spinny_down_flipbook (frame 20):
    measured within 1px of the 88px baseline already -- no change made.
  - walk_flipbook, shoot_flipbook (OLD art, shares T_DeathMetalCat_SpriteSheet with nothing else
    left after this pass): both measured within ~1px of baseline too (87 and 88) -- skipped as
    not worth extracting/resizing a sub-region of a shared texture for a <2% correction.

Resized flipbooks (factor = 88 / measured_height_px, new_cell = round(256 * factor)):
  run(1.0115->259px), air_down_shot(1.0353->265px), sword_combo3(0.9778->250px), uppy(1.0476->268px),
  double_whammy(0.9362->239px), dodge(0.9888->253px), block(0.9362->239px),
  invuln_dash(1.0115->259px), jump(0.967->247px), hold_fire(0.9362->239px), hurt(0.898->229px).

  air_shot_angled and wall_slide needed TWO further refinement passes beyond the numbers below
  (225px): both have NO clean neutral frame (every frame is a diagonal/leaning pose), which was
  clipping their measured height against the measurement crop and masking how oversized they
  really were. Measured with a taller crop instead, both converged to cell_size=188px after
  iterative refinement (225 -> 203 -> 188), landing ~5% over the Idle baseline -- the closest this
  measurement approach could get; still worth an eyeball check in real gameplay. The literal
  values below (225) reflect this script's ORIGINAL pass only -- see the report for the final
  188px figure actually left in place.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_apply_size_normalization.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\_resized_cache"
TEX_DEST = "/Game/Characters/DeathMetalCat/Textures"
SPRITE_DEST_BASE = "/Game/Characters/DeathMetalCat/Sprites"
FLIPBOOK_DEST = "/Game/Characters/DeathMetalCat/Flipbooks"
GRID_COLS = 5

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


def resize_flipbook(prop_name, src_filename, tex_name, sprite_subfolder, sprite_prefix,
                     flipbook_full_path, cell_size, frame_indices=None):
    unreal.log(f"=== {prop_name}: cell_size={cell_size} ===")
    sprite_dest = f"{SPRITE_DEST_BASE}/{sprite_subfolder}"

    texture = import_texture(src_filename, tex_name)
    tex_w = texture.blueprint_get_size_x()
    unreal.log(f"  resized texture: {tex_w}x{tex_w}")

    flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
    old_kf = flipbook.get_editor_property("key_frames")
    old_fps = flipbook.get_editor_property("frames_per_second")
    num_frames = len(old_kf)

    old_sprite_paths = []
    for kf in old_kf:
        sp = kf.get_editor_property("sprite")
        old_sprite_paths.append(sp.get_path_name().split(".")[0])

    flipbook.set_editor_property("key_frames", [])
    flipbook.modify()

    indices = frame_indices if frame_indices is not None else list(range(num_frames))
    new_kf = []
    for out_i, cell_index in enumerate(indices):
        row, col = cell_index // GRID_COLS, cell_index % GRID_COLS
        uv_x, uv_y = col * cell_size, row * cell_size
        sprite_name = f"{sprite_prefix}_{out_i + 1:02d}"
        sprite_full_path = f"{sprite_dest}/{sprite_name}"

        unreal.EditorAssetLibrary.delete_asset(sprite_full_path)
        sprite = asset_tools.create_asset(sprite_name, sprite_dest, unreal.PaperSprite, unreal.PaperSpriteFactory())
        sprite.set_editor_property("PixelsPerUnrealUnit", 1.0)
        sprite.set_editor_property("source_texture", texture)
        sprite.set_editor_property("source_uv", unreal.Vector2D(uv_x, uv_y))
        sprite.set_editor_property("source_dimension", unreal.Vector2D(cell_size, cell_size))
        sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
        sprite.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(sprite)

        kf2 = unreal.PaperFlipbookKeyFrame()
        kf2.set_editor_property("sprite", sprite)
        kf2.set_editor_property("frame_run", 1)
        new_kf.append(kf2)

    flipbook.set_editor_property("key_frames", new_kf)
    flipbook.set_editor_property("frames_per_second", old_fps)
    flipbook.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
    unreal.log(f"  rebuilt {len(new_kf)} sprite(s) at cell_size={cell_size}, PPU=1.0, fps unchanged ({old_fps})")


FB_DEST = FLIPBOOK_DEST

resize_flipbook("run_flipbook", "Cayde-hero_run.png", "T_DeathMetalCat_Run", "Run", "SP_DeathMetalCat_Run",
                 f"{FB_DEST}/FB_DeathMetalCat_Run", 259)

resize_flipbook("air_down_shot_flipbook", "Cayde-hero_air_shot-v1.png", "T_DeathMetalCat_AirDownShot", "AirDownShot", "SP_DeathMetalCat_AirDownShot",
                 f"{FB_DEST}/FB_DeathMetalCat_AirDownShot", 265)

resize_flipbook("air_shot_angled_flipbook", "Cayde-hero_air_shot_angled-v1.png", "T_DeathMetalCat_AirShotAngled", "AirShotAngled", "SP_DeathMetalCat_AirShotAngled",
                 f"{FB_DEST}/FB_DeathMetalCat_AirShotAngled", 188)

resize_flipbook("sword_combo3_flipbook", "Cayde-hero_sword_3-v1.png", "T_DeathMetalCat_SwordCombo3", "SwordCombo3", "SP_DeathMetalCat_SwordCombo3",
                 f"{FB_DEST}/FB_DeathMetalCat_SwordCombo3", 250)

resize_flipbook("uppy_flipbook", "Cayde-hero_uppy-v1.png", "T_DeathMetalCat_Uppy", "Uppy", "SP_DeathMetalCat_Uppy",
                 f"{FB_DEST}/FB_DeathMetalCat_Uppy", 268)

resize_flipbook("double_whammy_flipbook", "Cayde-hero_double_whammy-v1.png", "T_DeathMetalCat_DoubleWhammy", "DoubleWhammy", "SP_DeathMetalCat_DoubleWhammy",
                 f"{FB_DEST}/FB_DeathMetalCat_DoubleWhammy", 239)

resize_flipbook("dodge_flipbook", "Cayde-hero_backflip-v1.png", "T_DeathMetalCat_Dodge", "Dodge", "SP_DeathMetalCat_Dodge",
                 f"{FB_DEST}/FB_DeathMetalCat_Dodge", 253, frame_indices=[7, 8, 9, 12, 13])

resize_flipbook("block_flipbook", "Cayde-hero_block-v3.png", "T_DeathMetalCat_Block", "Block", "SP_DeathMetalCat_Block",
                 f"{FB_DEST}/FB_DeathMetalCat_Block", 239)

resize_flipbook("invuln_dash_flipbook", "Cayde-hero_invuln_dash-v1.png", "T_DeathMetalCat_InvulnDash", "InvulnDash", "SP_DeathMetalCat_InvulnDash",
                 f"{FB_DEST}/FB_DeathMetalCat_InvulnDash", 259)

resize_flipbook("jump_flipbook", "Cayde-hero_jump-v1.png", "T_DeathMetalCat_Jump", "Jump", "SP_DeathMetalCat_Jump",
                 f"{FB_DEST}/FB_DeathMetalCat_Jump", 247, frame_indices=[13, 20])

resize_flipbook("wall_slide_flipbook", "Cayde-hero_wall_slide-v1.png", "T_DeathMetalCat_WallSlide", "WallSlide", "SP_DeathMetalCat_WallSlide",
                 f"{FB_DEST}/FB_DeathMetalCat_WallSlide", 188)

resize_flipbook("hold_fire_flipbook", "Cayde-hero_gun_fire-v1.png", "T_DeathMetalCat_HoldFire", "HoldFire", "SP_DeathMetalCat_HoldFire",
                 f"{FB_DEST}/FB_DeathMetalCat_HoldFire", 239)

resize_flipbook("hurt_flipbook", "Cayde-hero_hurt.png", "T_DeathMetalCat_Hurt", "Hurt", "SP_DeathMetalCat_Hurt",
                 f"{FB_DEST}/FB_DeathMetalCat_Hurt", 229)

unreal.log("=== SIZE NORMALIZATION (RESIZE-BASED) COMPLETE ===")
