"""
ue_import_gnarly_logo.py

Imports gnarly_rank_font.png (the static "GNARLY RANK" logo graphic) as a standalone UI texture
(standard texture import, not the sprite/flipbook pipeline). Source PNG already carries proper
alpha transparency (checked directly: full 0-255 alpha range, background pixels ~94% transparent
on average) -- no chroma-key/background-removal pass needed, imported as-is.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_gnarly_logo.py').read())"
"""

import unreal

DEST = "/Game/UI/GnarlyRank"
SRC = r"C:\Users\calvi\Desktop\Projects\PythonTest\Content\gnarly_rank_font.png"
NAME = "T_GnarlyRank_Logo"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
full_path = f"{DEST}/{NAME}"

task = unreal.AssetImportTask()
task.filename = SRC
task.destination_path = DEST
task.destination_name = NAME
task.automated = True
task.save = True
task.replace_existing = True
task.factory = unreal.TextureFactory()
asset_tools.import_asset_tasks([task])

texture = unreal.EditorAssetLibrary.load_asset(full_path)
if texture is None:
    raise RuntimeError(f"texture import failed, no asset at {full_path}")

texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("srgb", True)
texture.modify()
unreal.EditorAssetLibrary.save_loaded_asset(texture)

unreal.log(f"[import] {full_path}  size={texture.blueprint_get_size_x()}x{texture.blueprint_get_size_y()}")
unreal.log("=== GNARLY LOGO IMPORT COMPLETE ===")
