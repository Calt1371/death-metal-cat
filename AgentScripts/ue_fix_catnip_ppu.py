import unreal

fb = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_CatNipIdle.FB_DeathMetalCat_CatNipIdle")
key_frames = fb.get_editor_property("key_frames")
for kf in key_frames:
    sprite = kf.get_editor_property("sprite")
    sprite.set_editor_property("PixelsPerUnrealUnit", 1.5)
    sprite.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(sprite)

print(f"set PPU=1.5 on {len(key_frames)} CatNip Idle sprites (matching regular Idle's own PPU)")
