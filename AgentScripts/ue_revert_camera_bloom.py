"""
ue_revert_camera_bloom.py

Reverts ue_fix_camera_bloom.py -- that camera-wide post-process change (locked auto-exposure,
reduced bloom) darkened the WHOLE level's rendering, not just the character sprites, per the
design ask's follow-up correction. Clears all four overrides on SideViewCamera's CDO
PostProcessSettings back to "not overridden" (stock engine defaults, matching how the camera was
before ue_fix_camera_bloom.py ever touched it).

Sprite-only brightness is handled separately via SpriteColor tinting (CatNipTintColor for Cat Nip;
a base-Cayde default tint if the regular-Cayde-brightness complaint still needs addressing) --
that's a per-character-material input, not a scene-wide setting, so it can't affect the level.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_revert_camera_bloom.py').read())"
"""

import unreal

bp = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat.BP_DeathMetalCat")
gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)
cam = cdo.get_components_by_class(unreal.CameraComponent)[0]
settings = cam.get_editor_property("post_process_settings")

for prop in ["override_bloom_intensity", "override_bloom_threshold",
             "override_auto_exposure_min_brightness", "override_auto_exposure_max_brightness"]:
    settings.set_editor_property(prop, False)
cam.set_editor_property("post_process_settings", settings)

print("bloom/exposure overrides cleared -- camera back to stock defaults")

unreal.EditorAssetLibrary.save_loaded_asset(bp)
print("saved BP_DeathMetalCat")
