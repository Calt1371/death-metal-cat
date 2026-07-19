"""
ue_create_shoot_input.py

Creates /Game/Input/IA_Shoot (InputAction, Digital/Boolean) and maps RightMouseButton on the
existing /Game/Input/IMC_PlayerControls.

No InputModifier needed (plain digital press, like Jump/Dodge/SwordAttack), but the save-dirty
lesson from the earlier Negate bug still applies: mutating DefaultKeyMappings this deep doesn't
reliably mark the package dirty on its own, so we call imc.modify() before mutating and save with
only_if_is_dirty=False.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_shoot_input.py').read())"
"""

import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def make_key(key_name):
    k = unreal.Key()
    k.set_editor_property("key_name", key_name)
    return k


def main():
    IA_PATH = "/Game/Input/IA_Shoot"
    IMC_PATH = "/Game/Input/IMC_PlayerControls"

    if unreal.EditorAssetLibrary.does_asset_exist(IA_PATH):
        ia_shoot = unreal.EditorAssetLibrary.load_asset(IA_PATH)
        unreal.log(f"[input] IA_Shoot already exists, reusing: {IA_PATH}")
    else:
        ia_shoot = asset_tools.create_asset("IA_Shoot", "/Game/Input", unreal.InputAction, unreal.InputAction_Factory())
        unreal.log(f"[input] created: {IA_PATH}")

    ia_shoot.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    unreal.EditorAssetLibrary.save_loaded_asset(ia_shoot, only_if_is_dirty=False)

    imc = unreal.EditorAssetLibrary.load_asset(IMC_PATH)
    imc.modify()

    dkm = imc.get_editor_property("default_key_mappings")
    existing = dkm.get_editor_property("mappings")
    already_mapped = any(
        m.get_editor_property("key").get_editor_property("key_name") == "RightMouseButton"
        and m.get_editor_property("action") == ia_shoot
        for m in existing
    )
    if already_mapped:
        unreal.log("[input] RightMouseButton -> IA_Shoot already mapped, skipping map_key")
    else:
        imc.map_key(ia_shoot, make_key("RightMouseButton"))
        unreal.log("[input] mapped RightMouseButton -> IA_Shoot")

    saved = unreal.EditorAssetLibrary.save_loaded_asset(imc, only_if_is_dirty=False)
    unreal.log(f"[input] IMC_PlayerControls save returned: {saved}")


main()
