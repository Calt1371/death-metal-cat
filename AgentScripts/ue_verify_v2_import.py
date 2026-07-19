import unreal

TEX_PATH = "/Game/Characters/DeathMetalCat/Textures/T_DeathMetalCat_SpriteSheet"
FLIPBOOKS = ["Idle", "Walk", "Run", "Shoot", "Jump", "Hurt", "SwordAttack", "Dodge"]
EXPECTED_COUNTS = {"Idle": 3, "Walk": 6, "Run": 6, "Shoot": 4, "Jump": 2, "Hurt": 2, "SwordAttack": 4, "Dodge": 4}

tex = unreal.EditorAssetLibrary.load_asset(TEX_PATH)
pkgs = [tex.get_package()]
for name in FLIPBOOKS:
    fb = unreal.EditorAssetLibrary.load_asset(f"/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_{name}")
    pkgs.append(fb.get_package())

reloaded, err = unreal.EditorLoadingAndSavingUtils.reload_packages(
    pkgs, unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE
)
print("reload_packages -> reloaded:", reloaded, " error:", err)

tex_fresh = unreal.EditorAssetLibrary.load_asset(TEX_PATH)
print(f"Texture size: {tex_fresh.blueprint_get_size_x()}x{tex_fresh.blueprint_get_size_y()} (expect 1182x1331)")
print(f"Texture compression={tex_fresh.get_editor_property('compression_settings')} mips={tex_fresh.get_editor_property('mip_gen_settings')} srgb={tex_fresh.get_editor_property('srgb')}")

all_ok = True
for name in FLIPBOOKS:
    path = f"/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_{name}"
    fb = unreal.EditorAssetLibrary.load_asset(path)
    kf = fb.get_editor_property("key_frames")
    fps = fb.get_editor_property("frames_per_second")
    ok = len(kf) == EXPECTED_COUNTS[name]
    all_ok = all_ok and ok
    print(f"{name}: frames={len(kf)} (expected {EXPECTED_COUNTS[name]}) fps={fps}  {'OK' if ok else 'MISMATCH'}")
    for i, k in enumerate(kf):
        sprite = k.get_editor_property("sprite")
        tex_ref = sprite.get_editor_property("source_texture") if sprite else None
        print(f"    [{i}] sprite={sprite.get_name() if sprite else None}  source_texture={tex_ref.get_name() if tex_ref else None}")

print()
print("ALL COUNTS OK" if all_ok else "SOME COUNTS MISMATCHED")
