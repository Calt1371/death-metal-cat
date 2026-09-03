"""
ue_reimport_gnarly_rank_textures.py

Reimports T_GnarlyRank_0..4 (used by GnarlyRankHUDWidget to show Cayde's current strike rank --
0=Rank D through 4=Rank... top rank) from the updated source PNGs the user just dropped at
Content/gnarly_rank_0.png..4.png, replacing the stale Aug-22 art baked into the existing .uasset
textures. Reuses each texture's own existing asset path/name (no Blueprint/widget rewiring
needed) -- same "swap the art in place" pattern as ue_import_room_barrier_flipbook.py.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_reimport_gnarly_rank_textures.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\Content"
TEX_DEST = "/Game/UI/GnarlyRank"

RANKS = [0, 1, 2, 3, 4]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

for rank in RANKS:
    src_filename = f"gnarly_rank_{rank}.png"
    tex_name = f"T_GnarlyRank_{rank}"
    tex_full_path = f"{TEX_DEST}/{tex_name}"

    existing = unreal.load_object(None, tex_full_path + "." + tex_name)
    if existing is None:
        raise RuntimeError(f"expected existing texture not found: {tex_full_path}")

    before_props = {
        "compression_settings": existing.get_editor_property("compression_settings"),
        "mip_gen_settings": existing.get_editor_property("mip_gen_settings"),
        "lod_group": existing.get_editor_property("lod_group"),
        "srgb": existing.get_editor_property("srgb"),
        "filter": existing.get_editor_property("filter"),
    }

    task = unreal.AssetImportTask()
    task.filename = RAW_DIR + "\\" + src_filename
    task.destination_path = TEX_DEST
    task.destination_name = tex_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.factory = unreal.TextureFactory()
    asset_tools.import_asset_tasks([task])

    texture = unreal.load_object(None, tex_full_path + "." + tex_name)
    if texture is None:
        raise RuntimeError(f"reimport failed for {tex_full_path}")

    # Preserve the existing texture's own import settings rather than the factory's raw defaults
    # -- same reasoning as every other import script in this project (TF_NEAREST/compression/etc
    # are content-specific tuning, not something a plain reimport should silently reset).
    for prop_name, value in before_props.items():
        texture.set_editor_property(prop_name, value)

    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)

    w, h = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    print(f"  {tex_full_path}: reimported from {src_filename} ({w}x{h})")

print("=== GNARLY RANK TEXTURE REIMPORT COMPLETE ===")
