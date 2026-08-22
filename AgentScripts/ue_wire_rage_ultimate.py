"""
ue_wire_rage_ultimate.py

Wires the Rage/Ultimate feature into BP_DeathMetalCat:
  1. Creates IA_RageActivate (Boolean, matching Jump/Dodge/Block/InvulnDash's convention) and binds
     it to R -- unused: existing bindings are A/D/Left/Right (Move), Space (Jump), LeftShift
     (Dodge), LMB (Sword), RMB (Shoot), S/Down (AimDown), Q (Block), E (InvulnDash).
  2. Points BP_DeathMetalCat's CDO at IA_RageActivate and the three new FancyIdle/FancyGallop/
     FancyAttack flipbooks.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_wire_rage_ultimate.py').read())"
"""

import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
INPUT_DEST = "/Game/Input"


def make_bool_input_action(name):
    full_path = INPUT_DEST + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        return unreal.EditorAssetLibrary.load_asset(full_path)
    action = asset_tools.create_asset(name, INPUT_DEST, unreal.InputAction, unreal.InputAction_Factory())
    action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    action.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(action)
    unreal.log(f"created {full_path}")
    return action


ia_rage = make_bool_input_action("IA_RageActivate")

imc = unreal.EditorAssetLibrary.load_asset("/Game/Input/IMC_PlayerControls")
data = imc.get_editor_property("default_key_mappings")
mappings = list(data.get_editor_property("mappings"))


def already_bound(action, key_name):
    for m in mappings:
        a = m.get_editor_property("action")
        k = m.get_editor_property("key")
        if a and a.get_name() == action.get_name() and k.get_editor_property("key_name") == key_name:
            return True
    return False


def add_mapping(action, key_name):
    if already_bound(action, key_name):
        unreal.log(f"  already bound: {action.get_name()} -> {key_name}, skipping")
        return
    m = unreal.EnhancedActionKeyMapping()
    m.set_editor_property("action", action)
    key = unreal.Key()
    key.set_editor_property("key_name", key_name)
    m.set_editor_property("key", key)
    mappings.append(m)
    unreal.log(f"  added mapping: {action.get_name()} -> {key_name}")


add_mapping(ia_rage, "R")

data.set_editor_property("mappings", mappings)
imc.set_editor_property("default_key_mappings", data)
imc.modify()
unreal.EditorAssetLibrary.save_loaded_asset(imc)
unreal.log(f"IMC_PlayerControls now has {len(mappings)} total mappings")

BP_PATH = "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat"
FB_DEST = "/Game/Characters/DeathMetalCat/Flipbooks"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

cdo.set_editor_property("rage_activate_action", ia_rage)
unreal.log("wired: RageActivateAction -> IA_RageActivate")

flipbook_props = {
    "fancy_idle_flipbook": "FB_DeathMetalCat_FancyIdle",
    "fancy_gallop_flipbook": "FB_DeathMetalCat_FancyGallop",
    "fancy_attack_flipbook": "FB_DeathMetalCat_FancyAttack",
}

for prop_name, fb_asset_name in flipbook_props.items():
    fb = unreal.EditorAssetLibrary.load_asset(FB_DEST + "/" + fb_asset_name)
    if fb is None:
        raise RuntimeError(f"missing flipbook asset: {fb_asset_name}")
    cdo.set_editor_property(prop_name, fb)
    unreal.log(f"  {prop_name} -> {fb_asset_name}")

cdo.modify()
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log("=== RAGE/ULTIMATE WIRING COMPLETE ===")
