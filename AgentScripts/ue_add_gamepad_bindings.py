"""
ue_add_gamepad_bindings.py

Adds gamepad key mappings to the existing IMC_PlayerControls, alongside the current
keyboard/mouse mappings (added, not replacing anything -- every existing keyboard/mouse mapping
is left untouched). Enhanced Input supports multiple keys per action within a single
InputMappingContext, and ADeathMetalCatCharacter::NotifyControllerChanged already adds this exact
context to the local player's subsystem -- so this is a pure data change, no C++/Blueprint
rewiring needed for the gamepad inputs to work.

Layout (Xbox naming; see AgentScripts' own report to the user for the full writeup):
  IA_Move          -- Gamepad_LeftX (analog), Gamepad_DPad_Right (+), Gamepad_DPad_Left (Negate)
  IA_Jump          -- Gamepad_FaceButton_Bottom (A)
  IA_SwordAttack   -- Gamepad_FaceButton_Left (X)
  IA_Shoot         -- Gamepad_FaceButton_Right (B)
  IA_AimDown       -- Gamepad_DPad_Down, Gamepad_LeftStick_Down
  IA_Dodge         -- Gamepad_LeftShoulder (LB)
  IA_Block         -- Gamepad_LeftTrigger (LT)
  IA_InvulnDash    -- Gamepad_RightShoulder (RB)
  IA_RageActivate  -- Gamepad_FaceButton_Top (Y)

IA_Move/IA_AimDown are the same two actions HandleSwordAttack/HandleShootStarted already read
(LastMoveRightAxisValue / bIsHoldingDownInput) to pick Uppy/Spinny Down/Air Shot Angled -- adding
gamepad keys to just these two actions is all that's needed for the directional-modifier combos to
work off the d-pad/left stick automatically, no separate combo-specific bindings exist to add.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_add_gamepad_bindings.py').read())"
"""

import unreal

imc = unreal.load_object(None, "/Game/Input/IMC_PlayerControls.IMC_PlayerControls")
data = imc.get_editor_property("default_key_mappings")
mappings = list(data.get_editor_property("mappings"))


def load_action(name):
    return unreal.load_object(None, f"/Game/Input/{name}.{name}")


def already_bound(action, key_name):
    for m in mappings:
        a = m.get_editor_property("action")
        k = m.get_editor_property("key")
        if a and a.get_name() == action.get_name() and k.get_editor_property("key_name") == key_name:
            return True
    return False


def add_mapping(action, key_name, negate=False):
    if already_bound(action, key_name):
        unreal.log(f"  already bound: {action.get_name()} -> {key_name}, skipping")
        return
    m = unreal.EnhancedActionKeyMapping()
    m.set_editor_property("action", action)
    key = unreal.Key()
    key.set_editor_property("key_name", key_name)
    m.set_editor_property("key", key)
    if negate:
        mod = unreal.new_object(unreal.InputModifierNegate, outer=imc)
        m.set_editor_property("modifiers", [mod])
    mappings.append(m)
    unreal.log(f"  added mapping: {action.get_name()} -> {key_name}{' (Negate)' if negate else ''}")


ia_move = load_action("IA_Move")
ia_jump = load_action("IA_Jump")
ia_sword = load_action("IA_SwordAttack")
ia_shoot = load_action("IA_Shoot")
ia_aimdown = load_action("IA_AimDown")
ia_dodge = load_action("IA_Dodge")
ia_block = load_action("IA_Block")
ia_invulndash = load_action("IA_InvulnDash")
ia_rage = load_action("IA_RageActivate")

add_mapping(ia_move, "Gamepad_LeftX")
add_mapping(ia_move, "Gamepad_DPad_Right")
add_mapping(ia_move, "Gamepad_DPad_Left", negate=True)

add_mapping(ia_jump, "Gamepad_FaceButton_Bottom")
add_mapping(ia_sword, "Gamepad_FaceButton_Left")
add_mapping(ia_shoot, "Gamepad_FaceButton_Right")

add_mapping(ia_aimdown, "Gamepad_DPad_Down")
add_mapping(ia_aimdown, "Gamepad_LeftStick_Down")

add_mapping(ia_dodge, "Gamepad_LeftShoulder")
add_mapping(ia_block, "Gamepad_LeftTrigger")
add_mapping(ia_invulndash, "Gamepad_RightShoulder")

add_mapping(ia_rage, "Gamepad_FaceButton_Top")

data.set_editor_property("mappings", mappings)
imc.set_editor_property("default_key_mappings", data)
imc.modify()
unreal.EditorAssetLibrary.save_loaded_asset(imc)
unreal.log(f"IMC_PlayerControls now has {len(mappings)} total mappings")
unreal.log("=== GAMEPAD BINDINGS ADDED ===")
