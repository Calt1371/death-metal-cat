"""
ue_import_flipbooks_v2.py

Re-imports the Death Metal Cat sprite sheet from the new green-screen source
(DMC_New_spritesheet_transparent.png, alpha-matted, cleaner than the original white-background
pass -- see chat history for the before/after alpha quality numbers).

Reuses the SAME texture/sprite/flipbook asset paths as the original import for the 7 pre-existing
actions (Idle, Walk, Run, Shoot, Jump, Hurt, SwordAttack), so BP_DeathMetalCat's existing property
assignments automatically pick up the improved artwork without needing manual reassignment.
Adds a brand new Dodge row/flipbook, which didn't exist before (Dodge previously reused
FB_DeathMetalCat_Jump as a placeholder).

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_flipbooks_v2.py').read())"
"""

import json
import unreal

SHEET_PATH = r"C:\Users\calvi\Desktop\Projects\PythonTest\Content\DMC_New_spritesheet_transparent.png"
FRAMES_JSON = r"C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\sprite_frames_v2.json"

DEST_ROOT = "/Game/Characters/DeathMetalCat"
TEX_DEST = DEST_ROOT + "/Textures"
SPRITE_DEST = DEST_ROOT + "/Sprites"
FLIPBOOK_DEST = DEST_ROOT + "/Flipbooks"

FPS_MAP = {
    "Idle": 10,
    "Walk": 12,
    "Run": 12,
    "Shoot": 15,
    "Jump": 15,
    "Hurt": 15,
    "SwordAttack": 15,
    "Dodge": 15,
}

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_texture():
    tex_name = "T_DeathMetalCat_SpriteSheet"
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
    unreal.log(f"[import] re-imported texture from new sheet: {tex_full_path}")

    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    return texture


def create_sprites(texture, frame_data):
    created = {}
    for row_name, frames in frame_data.items():
        created[row_name] = []
        row_folder = f"{SPRITE_DEST}/{row_name}"
        for idx, fr in enumerate(frames, start=1):
            sprite_name = f"SP_DeathMetalCat_{row_name}_{idx:02d}"
            sprite_full_path = f"{row_folder}/{sprite_name}"

            if unreal.EditorAssetLibrary.does_asset_exist(sprite_full_path):
                sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
            else:
                sprite = asset_tools.create_asset(
                    sprite_name, row_folder, unreal.PaperSprite, unreal.PaperSpriteFactory()
                )

            sprite.set_editor_property("source_texture", texture)
            sprite.set_editor_property("source_uv", unreal.Vector2D(fr["x"], fr["y"]))
            sprite.set_editor_property("source_dimension", unreal.Vector2D(fr["w"], fr["h"]))
            sprite.modify()
            unreal.EditorAssetLibrary.save_loaded_asset(sprite)

            created[row_name].append(sprite)
            unreal.log(f"[sprite] {sprite_full_path}  uv=({fr['x']},{fr['y']}) dim=({fr['w']}x{fr['h']})")
    return created


def create_flipbooks(sprites_by_row):
    created_paths = []
    for row_name, sprites in sprites_by_row.items():
        fb_name = f"FB_DeathMetalCat_{row_name}"
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
        flipbook.set_editor_property("frames_per_second", FPS_MAP[row_name])
        flipbook.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(flipbook)

        created_paths.append(fb_full_path)
        unreal.log(f"[flipbook] {fb_full_path}  fps={FPS_MAP[row_name]}  frames={len(sprites)}")
    return created_paths


def main():
    with open(FRAMES_JSON, "r") as f:
        frame_data = json.load(f)

    texture = import_texture()
    sprites_by_row = create_sprites(texture, frame_data)
    flipbook_paths = create_flipbooks(sprites_by_row)

    unreal.log("=== DEATH METAL CAT V2 IMPORT COMPLETE ===")
    for p in flipbook_paths:
        unreal.log(p)


main()
