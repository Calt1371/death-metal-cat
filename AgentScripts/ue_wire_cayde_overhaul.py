"""
ue_wire_cayde_overhaul.py

Blueprint/input wiring for the Cayde animation/combat overhaul (GOAL ASSIGNMENT, 2026-08-21).
Run AFTER ue_import_cayde_overhaul.py and after the C++ rebuild that added the new
AimDownAction/BlockAction/InvulnDashAction properties and all the new *Flipbook properties to
ADeathMetalCatCharacter.

1. Creates three new Enhanced Input actions (all Boolean, matching Jump/Dodge/SwordAttack/Shoot's
   existing convention): IA_AimDown, IA_Block, IA_InvulnDash.
2. Adds new key mappings to the existing IMC_PlayerControls:
     IA_AimDown   -> S, Down (redundant pair, matching how Move already binds both WASD and arrows)
     IA_Block     -> Q
     IA_InvulnDash -> E
   Chosen because none of Q/E/S/Down were already bound to anything in this project (existing
   mappings: A/D/Left/Right = Move, SpaceBar = Jump, LeftShift = Dodge, LeftMouseButton =
   SwordAttack, RightMouseButton = Shoot).
3. Points BP_DeathMetalCat's CDO at the three new input actions and every new/replaced flipbook
   property. The six straightforward-reimport flipbooks (Idle/AirDownShot/WallSlide/HoldFire/
   Dodge/Jump/SwordAttack) already resolve correctly with no Blueprint edit, since
   ue_import_cayde_overhaul.py rebuilt those same existing asset paths in place.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_wire_cayde_overhaul.py').read())"
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


ia_aim_down = make_bool_input_action("IA_AimDown")
ia_block = make_bool_input_action("IA_Block")
ia_invuln_dash = make_bool_input_action("IA_InvulnDash")

# --- Add key mappings to IMC_PlayerControls ---
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


add_mapping(ia_aim_down, "S")
add_mapping(ia_aim_down, "Down")
add_mapping(ia_block, "Q")
add_mapping(ia_invuln_dash, "E")

data.set_editor_property("mappings", mappings)
imc.set_editor_property("default_key_mappings", data)
imc.modify()
unreal.EditorAssetLibrary.save_loaded_asset(imc)
unreal.log(f"IMC_PlayerControls now has {len(mappings)} total mappings")

# --- Wire BP_DeathMetalCat's CDO ---
BP_PATH = "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat"
FB_DEST = "/Game/Characters/DeathMetalCat/Flipbooks"

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

cdo.set_editor_property("aim_down_action", ia_aim_down)
cdo.set_editor_property("block_action", ia_block)
cdo.set_editor_property("invuln_dash_action", ia_invuln_dash)
unreal.log("input actions wired: AimDownAction, BlockAction, InvulnDashAction")

flipbook_props = {
    "air_shot_angled_flipbook": "FB_DeathMetalCat_AirShotAngled",
    "sword_combo2_flipbook": "FB_DeathMetalCat_SwordCombo2",
    "sword_combo3_flipbook": "FB_DeathMetalCat_SwordCombo3",
    "uppy_flipbook": "FB_DeathMetalCat_Uppy",
    "double_whammy_flipbook": "FB_DeathMetalCat_DoubleWhammy",
    "spinny_down_flipbook": "FB_DeathMetalCat_SpinnyDown",
    "block_flipbook": "FB_DeathMetalCat_Block",
    "invuln_dash_flipbook": "FB_DeathMetalCat_InvulnDash",
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

unreal.log("=== CAYDE OVERHAUL WIRING COMPLETE ===")
