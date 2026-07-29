"""
ue_set_cyberpunk_night_lighting.py

Adjusts the level's existing sky/lighting setup (DirectionalLight, SkyLight, ExponentialHeightFog,
SkyAtmosphere already present -- confirmed via live query, not assumed) for a nighttime cyberpunk
city look:

- DirectionalLight: rotated below the horizon (pitch -16.3 -> +18, positive pitch = below horizon
  for a directional "sun") so SkyAtmosphere renders a genuine dark night sky rather than daytime,
  intensity dropped from 6.0 to a dim moonlight level, color cooled to a pale blue moonlight tint.
  Yaw/roll left untouched -- only the elevation matters for day/night.
- SkyLight: intensity dropped from 1.0 to a dim ambient fill, then explicitly recaptured
  (SLS_CAPTURED_SCENE mode caches the old bright sky otherwise).
- ExponentialHeightFog: inscattering color pushed to a deep blue-violet, volumetric fog enabled
  with a faint magenta emissive tint for that neon-haze look, density nudged up slightly for
  atmosphere.
- New unbound PostProcessVolume: boosted bloom (neon glow), blue-tinted shadow gain, boosted
  saturation and contrast for punchy neon colors, a mild vignette for mood.

All values are a first pass, not a final tuned look -- placeholder numbers, tune freely once
you've seen it in PIE.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_set_cyberpunk_night_lighting.py').read())"
"""

import unreal

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()

# ---- DirectionalLight (the "sun") ----------------------------------------------------------
dir_light = next((a for a in all_actors if a.get_class().get_name() == "DirectionalLight"), None)
old_rot = dir_light.get_actor_rotation()
new_rot = unreal.Rotator(pitch=18.0, yaw=old_rot.yaw, roll=old_rot.roll)
dir_light.set_actor_rotation(new_rot, False)

light_comp = dir_light.get_component_by_class(unreal.DirectionalLightComponent)
light_comp.set_editor_property("intensity", 0.4)
light_comp.set_editor_property("light_color", unreal.Color(180, 200, 255, 255))
unreal.log_warning("[LIGHTING] DirectionalLight: pitch " + str(old_rot.pitch) + " -> " + str(new_rot.pitch) + ", intensity 6.0 -> 0.4, color -> cool moonlight blue")

# ---- SkyLight -------------------------------------------------------------------------------
sky_light = next((a for a in all_actors if a.get_class().get_name() == "SkyLight"), None)
sky_comp = sky_light.get_component_by_class(unreal.SkyLightComponent)
sky_comp.set_editor_property("intensity", 0.15)
sky_comp.recapture_sky()
unreal.log_warning("[LIGHTING] SkyLight: intensity 1.0 -> 0.15, recaptured")

# ---- ExponentialHeightFog ---------------------------------------------------------------------
fog = next((a for a in all_actors if a.get_class().get_name() == "ExponentialHeightFog"), None)
fog_comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
fog_comp.set_fog_inscattering_color(unreal.LinearColor(0.02, 0.01, 0.08, 1.0))
fog_comp.set_editor_property("enable_volumetric_fog", True)
fog_comp.set_volumetric_fog_emissive(unreal.LinearColor(0.15, 0.02, 0.2, 1.0))
fog_comp.set_editor_property("fog_density", 0.06)
unreal.log_warning("[LIGHTING] Fog: inscattering -> deep blue-violet, volumetric fog on with magenta emissive haze, density 0.0436 -> 0.06")

# ---- PostProcessVolume (new, unbound -- applies everywhere) --------------------------------
ppv = actor_subsystem.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
ppv.set_actor_label("PPV_CyberpunkNight")
ppv.set_editor_property("b_unbound", True)

settings = ppv.settings

settings.bloom_intensity = 4.0
settings.override_bloom_intensity = True
settings.bloom_threshold = 0.2
settings.override_bloom_threshold = True

settings.color_saturation = unreal.Vector4(1.3, 1.3, 1.3, 1.3)
settings.override_color_saturation = True
settings.color_contrast = unreal.Vector4(1.1, 1.1, 1.1, 1.1)
settings.override_color_contrast = True
settings.color_gain_shadows = unreal.Vector4(0.6, 0.7, 1.15, 1.0)
settings.override_color_gain_shadows = True

settings.vignette_intensity = 0.4
settings.override_vignette_intensity = True

ppv.settings = settings

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()

unreal.log_warning("[LIGHTING] PostProcessVolume 'PPV_CyberpunkNight' created (unbound): bloom_intensity=4.0, bloom_threshold=0.2, saturation=1.3x, contrast=1.1x, shadow gain tinted blue, vignette=0.4")
unreal.log_warning("[LIGHTING] DONE")
