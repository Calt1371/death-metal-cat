"""
ue_import_holdfire.py

Imports the new dedicated Hold Fire art (DMC_HoldFire_transparent.png -- a single cropped row,
processed separately from the main sprite sheet, alpha-keyed off its own blue background) as a
brand new, independent asset chain: its own texture, its own 4 sprites, and a new flipbook
FB_DeathMetalCat_HoldFire.

Deliberately does NOT touch T_DeathMetalCat_SpriteSheet or any of its existing sprites/flipbooks
(Idle/Walk/Run/Shoot/Jump/Hurt/SwordAttack/Dodge) -- those are already imported, verified, and
tuned against the currently-imported sheet; re-importing them from this new generation would
risk silently shifting proportions/positions that existing hitbox offsets, jump thresholds, etc.
depend on.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_holdfire.py').read())"
"""

import json
import unreal

SHEET_PATH = r"C:\Users\calvi\Desktop\Projects\PythonTest\Content\DMC_HoldFire_transparent.png"
FRAMES_JSON = r"C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\sprite_frames_holdfire.json"

DEST_ROOT = "/Game/Characters/DeathMetalCat"
TEX_DEST = DEST_ROOT + "/Textures"
SPRITE_DEST = DEST_ROOT + "/Sprites/HoldFire"
FLIPBOOK_DEST = DEST_ROOT + "/Flipbooks"

FPS = 15

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_texture():
    tex_name = "T_DeathMetalCat_HoldFire"
    tex_full_path = f"{TEX_DEST}/{tex_name}"

    task = unreal.AssetImportTask()
    task.filename = SHEET_PATH
    task.destination_path = TEX_DEST
    task.destination_name = tex_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.factory = unreal.TextureFactory()
    asset_tools.import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(tex_full_path)
    if texture is None:
        raise RuntimeError(f"texture import failed, no asset at {tex_full_path}")
    unreal.log(f"[import] imported Hold Fire texture: {tex_full_path}")

    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    return texture


def create_sprites(texture, frames):
    sprites = []
    for idx, fr in enumerate(frames, start=1):
        sprite_name = f"SP_DeathMetalCat_HoldFire_{idx:02d}"
        sprite_full_path = f"{SPRITE_DEST}/{sprite_name}"

        if unreal.EditorAssetLibrary.does_asset_exist(sprite_full_path):
            sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
        else:
            sprite = asset_tools.create_asset(
                sprite_name, SPRITE_DEST, unreal.PaperSprite, unreal.PaperSpriteFactory()
            )

        sprite.set_editor_property("source_texture", texture)
        sprite.set_editor_property("source_uv", unreal.Vector2D(fr["x"], fr["y"]))
        sprite.set_editor_property("source_dimension", unreal.Vector2D(fr["w"], fr["h"]))
        sprite.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(sprite)

        sprites.append(sprite)
        unreal.log(f"[sprite] {sprite_full_path}  uv=({fr['x']},{fr['y']}) dim=({fr['w']}x{fr['h']})")
    return sprites


def create_flipbook(sprites):
    fb_name = "FB_DeathMetalCat_HoldFire"
    fb_full_path = f"{FLIPBOOK_DEST}/{fb_name}"

    if unreal.EditorAssetLibrary.does_asset_exist(fb_full_path):
        flipbook = unreal.EditorAssetLibrary.load_asset(fb_full_path)
    else:
        flipbook = asset_tools.create_asset(
            fb_name, FLIPBOOK_DEST, unreal.PaperFlipbook, unreal.PaperFlipbookFactory()
        )

    key_frames = []
    for sprite in sprites:
        kf = unreal.PaperFlipbookKeyFrame()
        kf.set_editor_property("sprite", sprite)
        kf.set_editor_property("frame_run", 1)
        key_frames.append(kf)

    flipbook.set_editor_property("key_frames", key_frames)
    flipbook.set_editor_property("frames_per_second", FPS)
    flipbook.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(flipbook)

    unreal.log(f"[flipbook] {fb_full_path}  fps={FPS}  frames={len(sprites)}")
    return fb_full_path


def main():
    with open(FRAMES_JSON, "r") as f:
        frame_data = json.load(f)

    texture = import_texture()
    sprites = create_sprites(texture, frame_data["HoldFire"])
    fb_path = create_flipbook(sprites)

    unreal.log("=== HOLD FIRE IMPORT COMPLETE ===")
    unreal.log(fb_path)


main()
