"""
ue_import_room4_meshy_textures.py

Imports the 5 single-sprite Room4 Meshy AI-generated PNGs (RawAssets/Meshy_Room4/*.png[_nobg...])
as Texture2D + a matching full-image PaperSprite each -- same texture settings established for
every other 2D art import in this project (TC_EDITOR_ICON compression, no mipmaps,
TEXTUREGROUP_UI, srgb on), and the same PaperSprite-from-texture pattern with source_uv=(0,0)
and source_dimension=the whole texture since each is a single standalone image, not a sheet
region. Does NOT include room4-crates-sprite.png_nobg.png -- that file contains multiple
irregularly-arranged crates and needs separate, non-automated handling (see conversation).

Named T_Room4_<Name>/SP_Room4_<Name> and imported into /Game/Environments/CityBiome/Room4
(created if it doesn't exist -- confirmed empty via asset registry query before this script
was written).

Does NOT place anything in the level -- import only.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_room4_meshy_textures.py').read())"
"""

import unreal

SRC_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Meshy_Room4"
# Three source files have mangled extensions (bare "___", no ".png" at the end at all -- unlike
# the usual ".png_nobg.png" pattern elsewhere in this project, which still ends in ".png").
# TextureFactory picks its import format from the extension, so those three are imported from
# renamed copies (identical bytes, confirmed via `file` magic-byte check) in this fixed-extension
# directory instead -- the originals in RawAssets/ are left untouched.
FIXED_EXT_DIR = r"C:\Users\calvi\.claude\jobs\78379236\tmp\room4_fixed_ext"
DEST = "/Game/Environments/CityBiome/Room4"

# (source dir, source filename, clean asset base name)
PNGS = [
    (FIXED_EXT_DIR, "room4-building-left-nosniper.png", "BuildingLeftNosniper"),
    (FIXED_EXT_DIR, "room4-building-right-nosniper.png", "BuildingRightNosniper"),
    (SRC_DIR, "room4-ground-floor-topdown.png_nobg.png", "GroundFloorTopdown"),
    (SRC_DIR, "room4-ground-floor.png_nobg.png", "GroundFloor"),
    (FIXED_EXT_DIR, "room4-platforms-boxes-floors.png", "PlatformsBoxesFloors"),
    (SRC_DIR, "room4_metal-stairwell.png_nobg.png", "Stairwell"),
    (SRC_DIR, "room4_metal-stairwell-mirrored.png_nobg.png", "Stairwell_Mirrored"),
    (FIXED_EXT_DIR, "room4floating-platform-propulsion.png", "FloatingPlatformPropulsion"),
]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
results = []

for src_dir, src_filename, clean_name in PNGS:
    src_path = src_dir + "\\" + src_filename
    tex_name = "T_Room4_" + clean_name
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

    sprite_name = "SP_Room4_" + clean_name
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

unreal.log_warning("=== ROOM4 MESHY TEXTURE IMPORT SUMMARY ===")
for clean_name, status, dims, sprite_path in results:
    unreal.log_warning(f"  {clean_name}: {status}  dims={dims}  sprite={sprite_path}")
