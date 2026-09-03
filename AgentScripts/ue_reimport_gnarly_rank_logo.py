"""
ue_reimport_gnarly_rank_logo.py

Reimports T_GnarlyRank_Logo (the static "GNARLY RANK" wordmark shown above RankText in
GnarlyRankHUDWidget) from the updated source PNG the user just dropped at
Content/gnarly_rank_font.png, replacing the stale Aug-22 plain-text art. Same in-place swap
pattern as ue_reimport_gnarly_rank_textures.py (reuses the existing asset path, preserves the
existing texture's own import settings rather than the factory's raw defaults).

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_reimport_gnarly_rank_logo.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\Content"
SRC_FILENAME = "gnarly_rank_font.png"
TEX_DEST = "/Game/UI/GnarlyRank"
TEX_NAME = "T_GnarlyRank_Logo"
TEX_FULL_PATH = f"{TEX_DEST}/{TEX_NAME}"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

existing = unreal.load_object(None, TEX_FULL_PATH + "." + TEX_NAME)
if existing is None:
    raise RuntimeError(f"expected existing texture not found: {TEX_FULL_PATH}")

before_props = {
    "compression_settings": existing.get_editor_property("compression_settings"),
    "mip_gen_settings": existing.get_editor_property("mip_gen_settings"),
    "lod_group": existing.get_editor_property("lod_group"),
    "srgb": existing.get_editor_property("srgb"),
    "filter": existing.get_editor_property("filter"),
}

task = unreal.AssetImportTask()
task.filename = RAW_DIR + "\\" + SRC_FILENAME
task.destination_path = TEX_DEST
task.destination_name = TEX_NAME
task.automated = True
task.save = True
task.replace_existing = True
task.factory = unreal.TextureFactory()
asset_tools.import_asset_tasks([task])

texture = unreal.load_object(None, TEX_FULL_PATH + "." + TEX_NAME)
if texture is None:
    raise RuntimeError(f"reimport failed for {TEX_FULL_PATH}")

for prop_name, value in before_props.items():
    texture.set_editor_property(prop_name, value)

texture.modify()
unreal.EditorAssetLibrary.save_loaded_asset(texture)

w, h = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
print(f"  {TEX_FULL_PATH}: reimported from {SRC_FILENAME} ({w}x{h})")
print("=== GNARLY RANK LOGO REIMPORT COMPLETE ===")
