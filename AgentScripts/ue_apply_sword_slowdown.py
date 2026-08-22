"""
ue_apply_sword_slowdown.py

Slows all six sword-related flipbooks (base combo chain sword-v2/sword_2/sword_3, plus the
airborne variants Uppy/Double Whammy/The Spinny Down Thing) to 75% speed by scaling each one's
own (already independently hand-tuned) fps by 0.75. Hit-detection timing (AttackDuration/
HitboxActiveDelay/HitboxActiveDuration) is a separate fixed real-time timer, NOT driven by the
flipbooks' own frame/notify progress -- confirmed via code review -- so it's scaled by the inverse
factor (1/0.75) here too, to keep the hitbox window proportionally in sync with the now-slower
swing. Combo buffer window (0.4s) deliberately left untouched per instruction.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_apply_sword_slowdown.py').read())"
"""

import unreal

BP_PATH = "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat"
bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

SPEED_FACTOR = 0.75
TIME_SCALE = 1.0 / SPEED_FACTOR

sword_props = ["sword_attack_flipbook", "sword_combo2_flipbook", "sword_combo3_flipbook",
               "uppy_flipbook", "double_whammy_flipbook", "spinny_down_flipbook"]

for p in sword_props:
    fb = cdo.get_editor_property(p)
    old_fps = fb.get_editor_property("frames_per_second")
    new_fps = old_fps * SPEED_FACTOR
    fb.set_editor_property("frames_per_second", new_fps)
    fb.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(fb)
    frames = len(fb.get_editor_property("key_frames"))
    unreal.log(f"{p}: fps {old_fps} -> {new_fps}  (duration {frames/old_fps:.4f}s -> {frames/new_fps:.4f}s)")

old_attack = cdo.get_editor_property("attack_duration")
old_delay = cdo.get_editor_property("hitbox_active_delay")
old_hbdur = cdo.get_editor_property("hitbox_active_duration")

new_attack = old_attack * TIME_SCALE
new_delay = old_delay * TIME_SCALE
new_hbdur = old_hbdur * TIME_SCALE

cdo.set_editor_property("attack_duration", new_attack)
cdo.set_editor_property("hitbox_active_delay", new_delay)
cdo.set_editor_property("hitbox_active_duration", new_hbdur)
cdo.modify()
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log(f"attack_duration: {old_attack:.4f} -> {new_attack:.4f}")
unreal.log(f"hitbox_active_delay: {old_delay:.4f} -> {new_delay:.4f}")
unreal.log(f"hitbox_active_duration: {old_hbdur:.4f} -> {new_hbdur:.4f}")
unreal.log("=== SWORD SLOWDOWN APPLIED ===")
