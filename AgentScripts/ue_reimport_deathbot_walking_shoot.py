"""
ue_reimport_deathbot_walking_shoot.py

Replaces DeathBotWalking's gunfire art with a newly-authored sheet:
  RawAssets/Enemies/DeathBotWalking-deathbotwalking_gunfire-v1.png

1280x1280, 5x5=25-frame grid (256x256/cell). Confirmed via PIL bbox/brightness analysis before
writing this: real alpha transparency, no genuine cross-cell bleed (a few flash frames touch their
own cell's right edge but no neighboring cell shows content starting at column 0, so no actual
overlap). Frames 1-9 are the raise motion (no flash); genuine muzzle-flash frames (bright-pixel
threshold, not eyeballed) are only 10, 11, 16, 21, 22 -- frame 15 has a much weaker blip (26px vs
400-660px for real flashes) and is treated as non-flash.

Old assets (T_Robot_Shoot_01..04, SP_Robot_Shoot_01..04 -- one full-size texture per frame, no
cropping, same now-superseded pattern the old Walk cycle used) are deleted and replaced with the
established shared-texture-plus-cropped-sprites approach (TF_NEAREST filtering, CENTER_CENTER
pivot) used for tonight's other re-imports.

Three flipbooks get rebuilt from the new sprites (paths unchanged so existing references resolve):
  FB_Robot_ShootDraw  -- frames 1-9 (raise), non-looping, 15fps -> 0.6s duration
  FB_Robot_ShootLoop  -- frames [10, 11, 16, 21, 22] (flash-only), looping, 15fps -> 5-shot burst
  FB_Robot_Shoot       -- all 25 frames (spare/reference asset, not directly wired to the
                          Blueprint, but its OLD key_frames pointed at the old sprites being
                          deleted here, so it gets rebuilt too rather than left with dangling refs)

Also sets BP_EnemyDeathBotWalking's ShootDrawDuration = 0.6 (was still the base-class default
0.15s, never tuned for Walking's actual draw content) so the raise motion isn't cut short.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_reimport_deathbot_walking_shoot.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Enemies"
SRC_FILENAME = "DeathBotWalking-deathbotwalking_gunfire-v1.png"

TEX_DEST = "/Game/Characters/Enemies/DeathBotWalking/Textures"
SPRITE_DEST = "/Game/Characters/Enemies/DeathBotWalking/Sprites"
FLIPBOOK_DEST = "/Game/Characters/Enemies/DeathBotWalking/Flipbooks"
BP_PATH = "/Game/Characters/Enemies/DeathBotWalking/Blueprints/BP_EnemyDeathBotWalking"

NEW_TEX_NAME = "T_Robot_Shoot"
SPRITE_PREFIX = "SP_Robot_Shoot"
GRID_ROWS = 5
GRID_COLS = 5
CELL_SIZE = 256
FPS = 15.0

DRAW_FRAMES = list(range(1, 10))          # 1-9
LOOP_FRAMES = [10, 11, 16, 21, 22]         # genuine flash frames only

OLD_TEXTURES = [f"T_Robot_Shoot_{i:02d}" for i in range(1, 5)]
OLD_SPRITES = [f"SP_Robot_Shoot_{i:02d}" for i in range(1, 5)]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# --- Delete the old per-frame sprites and textures ---
removed_sprites = 0
for name in OLD_SPRITES:
    p = SPRITE_DEST + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        unreal.EditorAssetLibrary.delete_asset(p)
        removed_sprites += 1
removed_textures = 0
for name in OLD_TEXTURES:
    p = TEX_DEST + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        unreal.EditorAssetLibrary.delete_asset(p)
        removed_textures += 1
unreal.log(f"removed {removed_sprites} old sprite(s), {removed_textures} old texture(s)")

# --- Import the new shared sheet ---
tex_full_path = TEX_DEST + "/" + NEW_TEX_NAME
task = unreal.AssetImportTask()
task.filename = RAW_DIR + "\\" + SRC_FILENAME
task.destination_path = TEX_DEST
task.destination_name = NEW_TEX_NAME
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

# --- Create the 25 new sprites ---
sprite_paths = {}
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
    sprite_paths[frame_num] = sprite_full_path

unreal.log(f"created {len(sprite_paths)} new sprites in {SPRITE_DEST}")


def rebuild_flipbook(fb_path, frame_numbers):
    flipbook = unreal.EditorAssetLibrary.load_asset(fb_path)
    key_frames = []
    for frame_num in frame_numbers:
        sp_asset = unreal.EditorAssetLibrary.load_asset(sprite_paths[frame_num])
        kf = unreal.PaperFlipbookKeyFrame()
        kf.set_editor_property("sprite", sp_asset)
        kf.set_editor_property("frame_run", 1)
        key_frames.append(kf)
    flipbook.set_editor_property("key_frames", key_frames)
    flipbook.set_editor_property("frames_per_second", FPS)
    flipbook.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
    unreal.log(f"{fb_path}: {len(key_frames)} frames {frame_numbers} @ {FPS}fps")


rebuild_flipbook(FLIPBOOK_DEST + "/FB_Robot_ShootDraw", DRAW_FRAMES)
rebuild_flipbook(FLIPBOOK_DEST + "/FB_Robot_ShootLoop", LOOP_FRAMES)
rebuild_flipbook(FLIPBOOK_DEST + "/FB_Robot_Shoot", list(range(1, 26)))

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())
cdo.set_editor_property("ShootDrawDuration", 0.6)
cdo.modify()
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)
unreal.log(f"ShootDrawDuration -> {cdo.get_editor_property('ShootDrawDuration')}")

unreal.log("=== DEATHBOT WALKING SHOOT RE-IMPORT COMPLETE ===")
