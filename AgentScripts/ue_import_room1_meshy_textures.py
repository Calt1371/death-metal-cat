"""
ue_import_room1_meshy_textures.py

Imports the 7 Meshy AI-generated 2D/2.5D PNG pieces for Room1's environment art
(RawAssets/Meshy_Room1/*.png) as Texture2D + a matching full-image PaperSprite each -- same
texture settings already established for every other 2D art import in this project (see
ue_import_gnarly_logo.py / ue_import_flipbooks_v2.py: TC_EDITOR_ICON compression, no mipmaps,
TEXTUREGROUP_UI, srgb on), and the same PaperSprite-from-texture pattern ue_import_flipbooks_v2.py
uses for sprite-sheet sub-regions, just with source_uv=(0,0) and source_dimension=the whole
texture since each of these is a single standalone image, not a sheet region.

Does NOT place anything in the level -- these are asset-registry-only imports, per this round's
explicit scope (placement/depth-layering is a separate, later decision).

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_room1_meshy_textures.py').read())"
"""

import unreal

SRC_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Meshy_Room1"
DEST = "/Game/Environments/CityBiome/Room1"

# (source filename, clean asset base name)
PNGS = [
    ("room1_background-midground-city.png", "BackgroundMidgroundCity"),
    ("room1_background-skyline.png", "BackgroundSkyline"),
    ("room1_floating-platform-pilot.png_nobg.png", "FloatingPlatform"),
    ("room1_ground-tile-2d.png_nobg.png", "GroundTile"),
    ("room1_left-structure-pilot.png_nobg.png", "LeftStructure"),
    ("room1_right-structure-pilot.png_nobg.png", "RightStructure"),
    ("room1_walkway-plank-pilot.png_nobg.png", "WalkwayPlank"),
]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
results = []

for src_filename, clean_name in PNGS:
    src_path = SRC_DIR + "\\" + src_filename
    tex_name = "T_Room1_" + clean_name
    tex_full_path = DEST + "/" + tex_name

    task = unreal.AssetImportTask()
    task.filename = src_path
    task.destination_path = DEST
    task.destination_name = tex_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.factory = unreal.TextureFactory()
    asset_tools.import_asset_tasks([task])

    texture = unreal.EditorAssetLibrary.load_asset(tex_full_path)
    if texture is None:
        unreal.log_error("[IMPORT] texture import FAILED, no asset at " + tex_full_path + " (source: " + src_path + ")")
        results.append((clean_name, "TEXTURE IMPORT FAILED", None, None))
        continue

    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)

    width = texture.blueprint_get_size_x()
    height = texture.blueprint_get_size_y()

    sprite_name = "SP_Room1_" + clean_name
    sprite_full_path = DEST + "/" + sprite_name
    if unreal.EditorAssetLibrary.does_asset_exist(sprite_full_path):
        sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
    else:
        sprite = asset_tools.create_asset(sprite_name, DEST, unreal.PaperSprite, unreal.PaperSpriteFactory())

    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(0, 0))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(width, height))
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)

    unreal.log("[IMPORT] " + tex_full_path + " (" + str(width) + "x" + str(height) + ") -> " + sprite_full_path)
    results.append((clean_name, "OK", f"{width}x{height}", sprite_full_path))

unreal.log_warning("=== ROOM1 MESHY TEXTURE IMPORT SUMMARY ===")
for clean_name, status, dims, sprite_path in results:
    unreal.log_warning(f"  {clean_name}: {status}  dims={dims}  sprite={sprite_path}")
