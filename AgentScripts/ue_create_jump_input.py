"""
ue_create_jump_input.py

Creates /Game/Input/IA_Jump (InputAction, Digital/Boolean) and adds a Spacebar mapping to
the existing /Game/Input/IMC_PlayerControls.

No InputModifier is needed for a simple digital press (unlike IA_Move's Negate case), but the
same lesson from that bug still applies to the save step: mutating DefaultKeyMappings this deep
(asset -> struct -> array -> struct) does not reliably mark the package dirty on its own, so we
call imc.modify() before mutating and save with only_if_is_dirty=False.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_jump_input.py').read())"
"""

import unreal

INPUT_DEST = "/Game/Input"
IA_NAME = "IA_Jump"
IMC_PATH = "/Game/Input/IMC_PlayerControls"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def make_key(key_name):
    k = unreal.Key()
    k.set_editor_property("key_name", key_name)
    return k


def main():
    # -- IA_Jump --
    ia_path = f"{INPUT_DEST}/{IA_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(ia_path):
        ia_jump = unreal.EditorAssetLibrary.load_asset(ia_path)
        unreal.log(f"[input] IA_Jump already exists, reusing: {ia_path}")
    else:
        ia_jump = asset_tools.create_asset(IA_NAME, INPUT_DEST, unreal.InputAction, unreal.InputAction_Factory())
        unreal.log(f"[input] created: {ia_path}")

    ia_jump.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    unreal.EditorAssetLibrary.save_loaded_asset(ia_jump, only_if_is_dirty=False)

    # -- add Space -> IA_Jump to IMC_PlayerControls --
    imc = unreal.EditorAssetLibrary.load_asset(IMC_PATH)
    imc.modify()

    # avoid duplicate mappings if this script is rerun
    dkm = imc.get_editor_property("default_key_mappings")
    existing = dkm.get_editor_property("mappings")
    already_mapped = any(
        m.get_editor_property("key").get_editor_property("key_name") == "SpaceBar"
        and m.get_editor_property("action") == ia_jump
        for m in existing
    )
    if already_mapped:
        unreal.log("[input] SpaceBar -> IA_Jump already mapped, skipping map_key")
    else:
        imc.map_key(ia_jump, make_key("SpaceBar"))
        unreal.log("[input] mapped SpaceBar -> IA_Jump")

    saved = unreal.EditorAssetLibrary.save_loaded_asset(imc, only_if_is_dirty=False)
    unreal.log(f"[input] IMC_PlayerControls save returned: {saved}")

    # -- immediate in-session listing --
    dkm2 = imc.get_editor_property("default_key_mappings")
    unreal.log("=== IMC_PlayerControls mappings (in-session) ===")
    for m in dkm2.get_editor_property("mappings"):
        key = m.get_editor_property("key").get_editor_property("key_name")
        action = m.get_editor_property("action")
        unreal.log(f"  key={key}  action={action.get_name() if action else None}")


main()
