"""
ue_create_swordattack_input.py

Creates /Game/Input/IA_SwordAttack (InputAction, Digital/Boolean) and maps LeftMouseButton on
the existing /Game/Input/IMC_PlayerControls.

No InputModifier needed (plain digital press, like Jump's Space and Dodge's LeftShift), but the
save-dirty lesson from the earlier Negate bug still applies: mutating DefaultKeyMappings this
deep doesn't reliably mark the package dirty on its own, so we call imc.modify() before mutating
and save with only_if_is_dirty=False.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_swordattack_input.py').read())"
"""

import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def make_key(key_name):
    k = unreal.Key()
    k.set_editor_property("key_name", key_name)
    return k


def main():
    IA_PATH = "/Game/Input/IA_SwordAttack"
    IMC_PATH = "/Game/Input/IMC_PlayerControls"

    if unreal.EditorAssetLibrary.does_asset_exist(IA_PATH):
        ia_sword = unreal.EditorAssetLibrary.load_asset(IA_PATH)
        unreal.log(f"[input] IA_SwordAttack already exists, reusing: {IA_PATH}")
    else:
        ia_sword = asset_tools.create_asset("IA_SwordAttack", "/Game/Input", unreal.InputAction, unreal.InputAction_Factory())
        unreal.log(f"[input] created: {IA_PATH}")

    ia_sword.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    unreal.EditorAssetLibrary.save_loaded_asset(ia_sword, only_if_is_dirty=False)

    imc = unreal.EditorAssetLibrary.load_asset(IMC_PATH)
    imc.modify()

    dkm = imc.get_editor_property("default_key_mappings")
    existing = dkm.get_editor_property("mappings")
    already_mapped = any(
        m.get_editor_property("key").get_editor_property("key_name") == "LeftMouseButton"
        and m.get_editor_property("action") == ia_sword
        for m in existing
    )
    if already_mapped:
        unreal.log("[input] LeftMouseButton -> IA_SwordAttack already mapped, skipping map_key")
    else:
        imc.map_key(ia_sword, make_key("LeftMouseButton"))
        unreal.log("[input] mapped LeftMouseButton -> IA_SwordAttack")

    saved = unreal.EditorAssetLibrary.save_loaded_asset(imc, only_if_is_dirty=False)
    unreal.log(f"[input] IMC_PlayerControls save returned: {saved}")


main()
