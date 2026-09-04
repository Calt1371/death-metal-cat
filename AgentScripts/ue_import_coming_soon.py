"""
ue_import_coming_soon.py

Imports Coming_Soon.png as a standalone UI texture for UGnarlyRankHUDWidget::ShowComingSoonScreen
-- same standard texture import (not the sprite/flipbook pipeline) and same settings as
ue_import_death_screen.py/ue_import_cayde_portrait.py/ue_import_gnarly_logo.py, since this is a
static full-screen UI image.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_coming_soon.py').read())"
"""

import unreal

DEST = "/Game/UI/ComingSoon"
SRC_PATH = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\DMC_Media\Coming_Soon.png"
TEX_NAME = "T_ComingSoon"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

task = unreal.AssetImportTask()
task.filename = SRC_PATH
task.destination_path = DEST
task.destination_name = TEX_NAME
task.automated = True
task.save = True
task.replace_existing = True
task.factory = unreal.TextureFactory()
asset_tools.import_asset_tasks([task])

tex_full_path = f"{DEST}/{TEX_NAME}"
texture = unreal.EditorAssetLibrary.load_asset(tex_full_path)
if texture is None:
    raise RuntimeError(f"texture import failed, no asset at {tex_full_path}")

texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("srgb", True)
texture.modify()
unreal.EditorAssetLibrary.save_loaded_asset(texture)

unreal.log(f"[import] {tex_full_path}  size={texture.blueprint_get_size_x()}x{texture.blueprint_get_size_y()}")
unreal.log("=== COMING SOON IMPORT COMPLETE ===")
