"""
ue_fix_corrupted_cdo_state.py

Fixes BP_DeathMetalCat's CDO having gotten contaminated with live Cat Nip state -- bIsCatNipActive
was reading True and the SpriteComponent template's sprite_color was reading the Cat Nip tint on a
totally fresh, never-picked-anything-up spawn. Root cause: at some point a Python script targeting
what was believed to be the live PIE pawn (via UnrealEditorSubsystem.get_game_world() +
GameplayStatics.get_player_pawn) actually landed on the CDO instead (most likely while PIE was not
actually running, given get_game_world() has been observed returning inconsistent state around PIE
start/stop in this session), and a later unrelated save_loaded_asset(bp) call (for wiring flipbooks
etc.) baked that contamination into the saved Blueprint asset.

Resets both properties back to their true neutral defaults and re-saves.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_corrupted_cdo_state.py').read())"
"""

import unreal

bp = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat.BP_DeathMetalCat")
gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)

print("BEFORE -- bIsCatNipActive:", cdo.get_editor_property("bIsCatNipActive"))
sprite = cdo.get_editor_property("sprite")
print("BEFORE -- sprite_color:", sprite.get_editor_property("sprite_color"))

cdo.set_editor_property("bIsCatNipActive", False)
sprite.set_editor_property("sprite_color", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

print("AFTER -- bIsCatNipActive:", cdo.get_editor_property("bIsCatNipActive"))
print("AFTER -- sprite_color:", sprite.get_editor_property("sprite_color"))

unreal.EditorAssetLibrary.save_loaded_asset(bp)
print("saved BP_DeathMetalCat")
