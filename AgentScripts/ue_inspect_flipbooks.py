import unreal

for path in [
    "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_Jump",
    "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_Shoot",
    "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_SwordAttack",
]:
    fb = unreal.EditorAssetLibrary.load_asset(path)
    fps = fb.get_editor_property("frames_per_second")
    key_frames = fb.get_editor_property("key_frames")
    print(f"=== {path}  fps={fps}  num_keyframes={len(key_frames)} ===")
    for i, kf in enumerate(key_frames):
        sprite = kf.get_editor_property("sprite")
        frame_run = kf.get_editor_property("frame_run")
        print(f"  [{i}] sprite={sprite.get_name() if sprite else None}  frame_run={frame_run}")
