"""
ue_import_death_screen.py

Imports you_died_death_metal_cat.png (the death-screen graphic shown by
UGnarlyRankHUDWidget::ShowDeathScreen) as a standalone UI texture -- standard texture import, not
the sprite/flipbook pipeline, same settings as ue_import_cayde_portrait.py/ue_import_gnarly_logo.py
(no mipmaps, UI texture group, editor-icon compression, since this is a static UI image). Source is
already a clean 1254x1254 square with no alpha channel, so no crop/resize needed.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_death_screen.py').read())"
"""

import unreal

DEST = "/Game/UI/DeathScreen"
SRC_PATH = r"C:\Users\calvi\Desktop\Projects\PythonTest\Content\you_died_death_metal_cat.png"
TEX_NAME = "T_YouDied"

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
unreal.log("=== DEATH SCREEN IMPORT COMPLETE ===")
