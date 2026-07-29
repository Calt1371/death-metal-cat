"""
ue_revert_lighting.py

Reverts the cyberpunk-night lighting pass back to the level's original state.

DirectionalLight and SkyLight: restored to the exact values captured via live query BEFORE any
changes were made (pitch=-16.285559, yaw/roll untouched throughout, intensity=6.0, white light;
SkyLight intensity=1.0), not guessed.

ExponentialHeightFog: fog_density restored to its captured original (0.0436). fog_inscattering
color / volumetric fog settings were never queried before being changed (a gap in the original
pass), so rather than guess a specific "original" value, this reads the true engine CLASS DEFAULT
for ExponentialHeightFogComponent (via a throwaway reference actor) and applies that -- the
component was almost certainly left at its default template values before this whole pass, never
custom-tuned.

PostProcessVolume: no PostProcessVolume existed before this pass at all -- PPV_CyberpunkNight is
deleted entirely, not just reset to defaults.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_revert_lighting.py').read())"
"""

import unreal

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()

# ---- DirectionalLight ------------------------------------------------------------------------
dir_light = next((a for a in all_actors if a.get_class().get_name() == "DirectionalLight"), None)
old_rot = dir_light.get_actor_rotation()
restored_rot = unreal.Rotator(pitch=-16.285559, yaw=old_rot.yaw, roll=old_rot.roll)
dir_light.set_actor_rotation(restored_rot, False)

light_comp = dir_light.get_component_by_class(unreal.DirectionalLightComponent)
light_comp.set_editor_property("intensity", 6.0)
light_comp.set_editor_property("light_color", unreal.Color(r=255, g=255, b=255, a=255))
unreal.log_warning("[REVERT] DirectionalLight: pitch -> -16.285559, intensity -> 6.0, color -> white")

# ---- SkyLight --------------------------------------------------------------------------------
sky_light = next((a for a in all_actors if a.get_class().get_name() == "SkyLight"), None)
sky_comp = sky_light.get_component_by_class(unreal.SkyLightComponent)
sky_comp.set_editor_property("intensity", 1.0)
sky_comp.recapture_sky()
unreal.log_warning("[REVERT] SkyLight: intensity -> 1.0, recaptured")

# ---- ExponentialHeightFog ----------------------------------------------------------------------
fog = next((a for a in all_actors if a.get_class().get_name() == "ExponentialHeightFog"), None)
fog_comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
fog_comp.set_editor_property("fog_density", 0.04360000044107437)

# True engine class default, since the original inscattering/volumetric values were never queried
# before being changed -- read from a throwaway reference actor rather than guessed.
ref_actor = actor_subsystem.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -100000))
ref_comp = ref_actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
default_inscattering = ref_comp.get_editor_property("fog_inscattering_luminance")
default_volumetric_enabled = ref_comp.get_editor_property("enable_volumetric_fog")
default_volumetric_emissive = ref_comp.get_editor_property("volumetric_fog_emissive")
actor_subsystem.destroy_actor(ref_actor)

fog_comp.set_fog_inscattering_color(default_inscattering)
fog_comp.set_editor_property("enable_volumetric_fog", default_volumetric_enabled)
fog_comp.set_volumetric_fog_emissive(default_volumetric_emissive)
unreal.log_warning(
    "[REVERT] Fog: density -> 0.0436, inscattering -> engine default " + str(default_inscattering)
    + ", enable_volumetric_fog -> " + str(default_volumetric_enabled)
    + ", volumetric_fog_emissive -> " + str(default_volumetric_emissive)
)

# ---- PostProcessVolume: delete entirely (none existed originally) ----------------------------
ppvs = [a for a in all_actors if a.get_actor_label() == "PPV_CyberpunkNight"]
for p in ppvs:
    actor_subsystem.destroy_actor(p)
unreal.log_warning("[REVERT] Removed " + str(len(ppvs)) + " PPV_CyberpunkNight actor(s) -- none existed originally")

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()
unreal.log_warning("[REVERT] DONE")
