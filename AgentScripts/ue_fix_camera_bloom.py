"""
ue_fix_camera_bloom.py

Root-causes "regular Cayde is bright too" (and most of Cat Nip's remaining brightness after
CatNipTintColor was already cut twice): SideViewCamera (BP_DeathMetalCat's gameplay camera) has NO
post-process overrides at all -- every character render is going through stock, un-tuned UE5
defaults (bloom_intensity=0.675, bloom_threshold=-1.0, i.e. bloom applies to nearly everything).
Combined with this project's Unlit/Emissive-driven Paper2D sprite material (see CatNipTintColor's
own doc comment), any moderately bright sprite pixel blooms out -- this is why regular,
never-touched-CatNip Cayde was confirmed live (2026-08-25) to ALSO render as a blown-out white blob.

Overrides bloom_intensity/bloom_threshold AND locks auto-exposure (min/max brightness both pinned
to -2.0, live-tuned via screenshot 2026-08-25) on SideViewCamera's CDO PostProcessSettings. Bloom
alone turned out NOT to be the dominant cause -- confirmed live that reducing it had zero visible
effect. The real culprit was auto-exposure: this dark cyberpunk-night level's Histogram auto-exposure
(min/max brightness -10..20, fully dynamic) was compensating for the dark scene average by
brightening the whole image, blowing out anything already light-colored (the mostly-white/light-fur
character) regardless of bloom settings. Locking min/max brightness to the same value pins exposure
to a fixed point instead of letting it hunt based on scene content -- -2.0 was chosen by comparing
several candidates live: it keeps the background's neon-city atmosphere visibly readable while the
character renders with full detail and no blowout. Does NOT disable bloom entirely -- Cat Nip's
glow and other intentional VFX still read as a glow, just without blowing out into a featureless
white shape.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_camera_bloom.py').read())"
"""

import unreal

bp = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat.BP_DeathMetalCat")
gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)
cam = cdo.get_components_by_class(unreal.CameraComponent)[0]
settings = cam.get_editor_property("post_process_settings")

print("BEFORE -- bloom_intensity:", settings.get_editor_property("bloom_intensity"),
      "bloom_threshold:", settings.get_editor_property("bloom_threshold"))

settings.set_editor_property("override_bloom_intensity", True)
settings.set_editor_property("bloom_intensity", 0.2)
settings.set_editor_property("override_bloom_threshold", True)
settings.set_editor_property("bloom_threshold", 1.0)

# Bloom alone (confirmed applied live, zero visible change) wasn't the dominant cause -- this dark
# cyberpunk-night level's auto-exposure (Histogram method, min/max brightness -10..20, wide open)
# was compensating for the dark scene average by brightening the whole image, blowing out anything
# already light-colored (the mostly-white/light-fur character) regardless of bloom settings.
# Locking min/max brightness to the same value pins auto-exposure to a fixed point instead of
# letting it hunt based on scene content.
settings.set_editor_property("override_auto_exposure_min_brightness", True)
settings.set_editor_property("auto_exposure_min_brightness", -2.0)
settings.set_editor_property("override_auto_exposure_max_brightness", True)
settings.set_editor_property("auto_exposure_max_brightness", -2.0)
cam.set_editor_property("post_process_settings", settings)

print("AFTER -- bloom_intensity:", settings.get_editor_property("bloom_intensity"),
      "bloom_threshold:", settings.get_editor_property("bloom_threshold"))

unreal.EditorAssetLibrary.save_loaded_asset(bp)
print("saved BP_DeathMetalCat")
