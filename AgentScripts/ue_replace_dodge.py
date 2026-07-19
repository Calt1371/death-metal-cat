"""
ue_replace_dodge.py

Replaces the old 4-frame Dodge flipbook (a more ambiguous narrative from the older sheet) with a
fresh 5-frame back-handspring sequence from the new blue-background sheet
(DMC_Dodge_transparent.png -- a single cropped row, processed independently, own texture).

Deletes the old FB_DeathMetalCat_Dodge and its 4 SP_DeathMetalCat_Dodge_* sprites outright (frame
count changed, so the old assets are simply wrong now), then creates a new independent texture,
5 new sprites, and a fresh flipbook at the same asset paths BP_DeathMetalCat already points at, so
existing Blueprint property assignments keep working without manual reassignment.

Does NOT touch T_DeathMetalCat_SpriteSheet or any of its other rows (Idle/Walk/Run/Shoot/Jump/
Hurt/SwordAttack) or the independent T_DeathMetalCat_HoldFire chain -- only Dodge is affected.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_replace_dodge.py').read())"
"""

import json
import unreal

SHEET_PATH = r"C:\Users\calvi\Desktop\Projects\PythonTest\Content\DMC_Dodge_transparent.png"
FRAMES_JSON = r"C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\sprite_frames_dodge.json"

DEST_ROOT = "/Game/Characters/DeathMetalCat"
TEX_DEST = DEST_ROOT + "/Textures"
SPRITE_DEST = DEST_ROOT + "/Sprites/Dodge"
FLIPBOOK_DEST = DEST_ROOT + "/Flipbooks"

FPS = 15

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def delete_old_dodge_assets():
    fb_path = f"{FLIPBOOK_DEST}/FB_DeathMetalCat_Dodge"
    if unreal.EditorAssetLibrary.does_asset_exist(fb_path):
        unreal.EditorAssetLibrary.delete_asset(fb_path)
        unreal.log(f"[delete] {fb_path}")

    for idx in range(1, 5):  # old version had exactly 4 frames
        sprite_path = f"{SPRITE_DEST}/SP_DeathMetalCat_Dodge_{idx:02d}"
        if unreal.EditorAssetLibrary.does_asset_exist(sprite_path):
            unreal.EditorAssetLibrary.delete_asset(sprite_path)
            unreal.log(f"[delete] {sprite_path}")


def import_texture():
    tex_name = "T_DeathMetalCat_Dodge"
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
    unreal.log(f"[import] imported Dodge texture: {tex_full_path}")

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
        sprite_name = f"SP_DeathMetalCat_Dodge_{idx:02d}"
        sprite_full_path = f"{SPRITE_DEST}/{sprite_name}"

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
    fb_name = "FB_DeathMetalCat_Dodge"
    fb_full_path = f"{FLIPBOOK_DEST}/{fb_name}"

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

    delete_old_dodge_assets()

    texture = import_texture()
    sprites = create_sprites(texture, frame_data["Dodge"])
    fb_path = create_flipbook(sprites)

    unreal.log("=== DODGE REPLACEMENT COMPLETE ===")
    unreal.log(fb_path)


main()
