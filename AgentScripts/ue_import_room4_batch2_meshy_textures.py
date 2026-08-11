"""
ue_import_room4_batch2_meshy_textures.py

Imports the 8 files currently in RawAssets/Meshy_Room4/ after that folder was cleared and
repopulated with new assets. Deliberately a SEPARATE script from
ue_import_room4_meshy_textures.py rather than adding to its shared PNGS list and re-running the
whole thing -- re-running that script would re-import (touch/re-save) all 8 previously-imported
Room4 assets too, even though their content is unchanged, which risks violating the "pure
additive, never touch pre-existing assets" constraint this batch was run under. This script only
ever imports the 8 new files below.

Confirmed via asset registry query before this was written: none of these derived names collide
with anything already in /Game/Environments/CityBiome/Room4/ (BuildingLeftNosniper,
BuildingRightNosniper, FloatingPlatformPropulsion, GroundFloor, GroundFloorTopdown,
PlatformsBoxesFloors, Stairwell, Stairwell_Mirrored).

Visually confirmed (not assumed) that room4-platform-02-A through -06-A are five DISTINCT
modular pieces (a wide hanging platform, a vertical hanging chain, a long cable-beam, etc. --
different dimensions on every file), not sequential animation frames -- no PaperFlipbook needed.

Does NOT place anything in the level -- import only.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_room4_batch2_meshy_textures.py').read())"
"""

import unreal

SRC_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Meshy_Room4"
# room4-platform-A-farleft-fixed.png_nobg___ has a mangled extension (bare "___", no ".png" at
# all) -- imported from a renamed, byte-identical copy instead, same handling as prior Room4
# batches. The original in RawAssets/ is untouched.
FIXED_EXT_DIR = r"C:\Users\calvi\.claude\jobs\78379236\tmp\room4_fixed_ext"
DEST = "/Game/Environments/CityBiome/Room4"

# (source dir, source filename, clean asset base name)
PNGS = [
    (SRC_DIR, "room4-building-left-A.png_nobg.png", "BuildingLeftA"),
    (SRC_DIR, "room4-building-right-A.png_nobg.png", "BuildingRightA"),
    (SRC_DIR, "room4-platform-02-A.png_nobg.png", "Platform02A"),
    (SRC_DIR, "room4-platform-03-A.png_nobg.png", "Platform03A"),
    (SRC_DIR, "room4-platform-04-A.png_nobg.png", "Platform04A"),
    (SRC_DIR, "room4-platform-05-A.png_nobg.png", "Platform05A"),
    (SRC_DIR, "room4-platform-06-A.png_nobg.png", "Platform06A"),
    (FIXED_EXT_DIR, "room4-platform-A-farleft-fixed.png", "PlatformAFarleftFixed"),
]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
results = []

for src_dir, src_filename, clean_name in PNGS:
    src_path = src_dir + "\\" + src_filename
    tex_name = "T_Room4_" + clean_name
    tex_full_path = DEST + "/" + tex_name

    # Safety check, per this batch's explicit no-overwrite constraint: refuse rather than import
    # if this exact name somehow already exists (shouldn't happen -- confirmed clear above -- but
    # this makes the script itself refuse to silently overwrite if that assumption is ever wrong).
    if unreal.EditorAssetLibrary.does_asset_exist(tex_full_path):
        unreal.log_error("[IMPORT] REFUSING to overwrite pre-existing asset at " + tex_full_path)
        results.append((clean_name, "SKIPPED - ALREADY EXISTS", None, None))
        continue

    task = unreal.AssetImportTask()
    task.filename = src_path
    task.destination_path = DEST
    task.destination_name = tex_name
    task.automated = True
    task.save = True
    task.replace_existing = False
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
        unreal.log_error("[IMPORT] REFUSING to overwrite pre-existing sprite at " + sprite_full_path)
        results.append((clean_name, "SPRITE ALREADY EXISTS", f"{width}x{height}", None))
        continue
    sprite = asset_tools.create_asset(sprite_name, DEST, unreal.PaperSprite, unreal.PaperSpriteFactory())

    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(0, 0))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(width, height))
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)

    unreal.log("[IMPORT] " + tex_full_path + " (" + str(width) + "x" + str(height) + ") -> " + sprite_full_path)
    results.append((clean_name, "OK", f"{width}x{height}", sprite_full_path))

unreal.log_warning("=== ROOM4 BATCH 2 MESHY TEXTURE IMPORT SUMMARY ===")
for clean_name, status, dims, sprite_path in results:
    unreal.log_warning(f"  {clean_name}: {status}  dims={dims}  sprite={sprite_path}")
