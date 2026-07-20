"""
ue_import_gnarly_portraits.py

Imports the 5 Gnarly Rank face-portrait PNGs (ranks 0-4, direct 1:1 mapping) as standalone UI
textures (standard texture import, not the sprite/flipbook pipeline -- these are static UI images,
not animated character frames). Blue background intentionally NOT removed per instructions -- kept
as simple square images shown with a border/frame on the HUD.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_gnarly_portraits.py').read())"
"""

import unreal

DEST = "/Game/UI/GnarlyRank"
SRC_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\Content"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

for rank in range(5):
    src_path = fr"{SRC_DIR}\gnarly_rank_{rank}.png"
    tex_name = f"T_GnarlyRank_{rank}"
    tex_full_path = f"{DEST}/{tex_name}"

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
        raise RuntimeError(f"texture import failed, no asset at {tex_full_path}")

    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)

    unreal.log(f"[import] {tex_full_path}  size={texture.blueprint_get_size_x()}x{texture.blueprint_get_size_y()}")

unreal.log("=== GNARLY PORTRAIT IMPORT COMPLETE ===")
