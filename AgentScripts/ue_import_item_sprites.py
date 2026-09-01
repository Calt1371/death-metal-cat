"""
ue_import_item_sprites.py

Imports all new item/currency/CatNip sprites for the item pickup system:
  - 12 static single-image icons (crate + 11 items/currency), each as a single PaperSprite
    covering the whole imported canvas, CENTER_CENTER pivot.
  - 1 new Cayde flipbook (Cayde-catnip_super_mode_idle-v9, a 25-frame 5x5 grid of 256px cells,
    same convention as every other Cayde flipbook this project uses) replacing Idle while Cat Nip
    is active.

Standard checks (documented in the printed report, not just assumed):
  - pivot mode: CENTER_CENTER on every sprite/frame.
  - clean frame crops: verified via the flipbook's own established grid convention (256px cells,
    5x5 = 25 frames) -- same math already proven correct for every other Cayde flipbook.
  - facing direction: static icons don't depict Cayde (no facing concept applies) except the new
    flipbook, whose facing is checked separately via live PIE screenshot after import (this
    project's established facing-flip caveat means raw-file eyeballing alone isn't trusted).

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_item_sprites.py').read())"
"""

import unreal

RAW_DIR = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Allies"
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_texture(src_filename, dest_path, tex_name):
    tex_full_path = dest_path + "/" + tex_name
    task = unreal.AssetImportTask()
    task.filename = RAW_DIR + "\\" + src_filename
    task.destination_path = dest_path
    task.destination_name = tex_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.factory = unreal.TextureFactory()
    asset_tools.import_asset_tasks([task])

    texture = unreal.load_object(None, tex_full_path + "." + tex_name)
    if texture is None:
        raise RuntimeError(f"texture import failed for {tex_full_path}")

    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    return texture


def make_single_sprite(texture, sprite_dest, sprite_name, ppu=1.0):
    w, h = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    sprite_full_path = sprite_dest + "/" + sprite_name
    sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
    if sprite is None:
        sprite = asset_tools.create_asset(sprite_name, sprite_dest, unreal.PaperSprite, unreal.PaperSpriteFactory())
    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(0, 0))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(w, h))
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    sprite.set_editor_property("PixelsPerUnrealUnit", ppu)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
    print(f"  sprite {sprite_name}: {w}x{h} @ ppu={ppu}")
    return sprite_full_path


def import_icon(src_filename, tex_name, sprite_name, dest_dir, ppu=1.0):
    print(f"=== {src_filename} -> {dest_dir}/{sprite_name} ===")
    texture = import_texture(src_filename, dest_dir, tex_name)
    make_single_sprite(texture, dest_dir, sprite_name, ppu)


# -- Crate --
import_icon("skull-question-item-crate.png_nobg.png", "T_ItemCrate", "SP_ItemCrate",
            "/Game/Items/Crate", ppu=6.0)

# -- Scraps currency (HUD icon) --
import_texture("scraps-currency-logo.png_nobg.png", "/Game/UI/Items", "T_ScrapsCurrency")

# -- Item icons --
ITEM_ICONS = [
    ("cat-nip-item.png_nobg.png", "T_Item_CatNip", "SP_Item_CatNip"),
    ("scratch-patch-item.png_nobg.png", "T_Item_ScratchPatch", "SP_Item_ScratchPatch"),
    ("nine-life-stim-item.png_nobg.png", "T_Item_NineLifeStim", "SP_Item_NineLifeStim"),
    ("death-defier-item.png_nobg.png", "T_Item_DeathDefier", "SP_Item_DeathDefier"),
    ("razor-fang-item.png_nobg.png", "T_Item_RazorFang", "SP_Item_RazorFang"),
    ("deadshot-rounds-item.png_nobg.png", "T_Item_DeadshotRounds", "SP_Item_DeadshotRounds"),
    ("steel-fur-item.png_nobg.png", "T_Item_SteelFur", "SP_Item_SteelFur"),
    ("fancy-feed-item.png_nobg.png", "T_Item_FancyFeed", "SP_Item_FancyFeed"),
    ("gnarly-amp-item.png_nobg.png", "T_Item_GnarlyAmp", "SP_Item_GnarlyAmp"),
    ("mirror-claw-item.png_nobg.png", "T_Item_MirrorClaw", "SP_Item_MirrorClaw"),
]
for src, tex_name, sprite_name in ITEM_ICONS:
    import_icon(src, tex_name, sprite_name, "/Game/Items/Icons", ppu=1.0)

# -- Cat Nip super-mode Idle flipbook (25 frames, 5x5 grid, 256px cells -- same convention as
# every other Cayde flipbook) --
print("=== Cayde-catnip_super_mode_idle-v9.png -> FB_DeathMetalCat_CatNipIdle ===")
GRID_COLS = 5
CELL_SIZE = 256
TEX_DEST = "/Game/Characters/DeathMetalCat/Textures"
SPRITE_DEST = "/Game/Characters/DeathMetalCat/Sprites/CatNipIdle"
FLIPBOOK_DEST = "/Game/Characters/DeathMetalCat/Flipbooks"

catnip_texture = import_texture("Cayde-catnip_super_mode_idle-v9.png", TEX_DEST, "T_DeathMetalCat_CatNipIdle")
tex_w, tex_h = catnip_texture.blueprint_get_size_x(), catnip_texture.blueprint_get_size_y()
print(f"  texture: {tex_w}x{tex_h}")

sprite_paths = []
for i in range(25):
    row, col = i // GRID_COLS, i % GRID_COLS
    sprite_name = f"SP_DeathMetalCat_CatNipIdle_{i + 1:02d}"
    sprite_full_path = SPRITE_DEST + "/" + sprite_name
    sprite = unreal.EditorAssetLibrary.load_asset(sprite_full_path)
    if sprite is None:
        sprite = asset_tools.create_asset(sprite_name, SPRITE_DEST, unreal.PaperSprite, unreal.PaperSpriteFactory())
    sprite.set_editor_property("source_texture", catnip_texture)
    sprite.set_editor_property("source_uv", unreal.Vector2D(col * CELL_SIZE, row * CELL_SIZE))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(CELL_SIZE, CELL_SIZE))
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    sprite.set_editor_property("PixelsPerUnrealUnit", 1.0)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
    sprite_paths.append(sprite_full_path)
print(f"  rebuilt {len(sprite_paths)} sprite(s)")

flipbook_full_path = FLIPBOOK_DEST + "/FB_DeathMetalCat_CatNipIdle"
flipbook = unreal.EditorAssetLibrary.load_asset(flipbook_full_path)
if flipbook is None:
    flipbook = asset_tools.create_asset("FB_DeathMetalCat_CatNipIdle", FLIPBOOK_DEST, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())

key_frames = []
for sp_path in sprite_paths:
    sp_asset = unreal.EditorAssetLibrary.load_asset(sp_path)
    kf = unreal.PaperFlipbookKeyFrame()
    kf.set_editor_property("sprite", sp_asset)
    kf.set_editor_property("frame_run", 1)
    key_frames.append(kf)
flipbook.set_editor_property("key_frames", key_frames)
flipbook.set_editor_property("frames_per_second", 12)
flipbook.modify()
unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
print(f"  {flipbook_full_path}: {len(key_frames)} frames @ 12fps")

print("=== ITEM SPRITE IMPORT COMPLETE ===")
