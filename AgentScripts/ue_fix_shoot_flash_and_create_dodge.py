"""
ue_fix_shoot_flash_and_create_dodge.py

1. Bumps FrameRun on the muzzle-flash keyframe (index 1, SP_DeathMetalCat_Shoot_02) of
   FB_DeathMetalCat_Shoot so it holds long enough to actually read.
2. Creates /Game/Input/IA_Dodge (InputAction, Digital/Boolean) and maps LeftShift -> IA_Dodge
   on the existing /Game/Input/IMC_PlayerControls.

No InputModifier is needed for this mapping (a plain digital press, like Jump's Space mapping),
but the same lesson about the save step from the earlier Negate bug still applies: mutating
DefaultKeyMappings this deep doesn't reliably mark the package dirty on its own, so we call
imc.modify() before mutating and save with only_if_is_dirty=False.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_fix_shoot_flash_and_create_dodge.py').read())"
"""

import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def make_key(key_name):
    k = unreal.Key()
    k.set_editor_property("key_name", key_name)
    return k


def fix_shoot_muzzle_flash():
    SHOOT_PATH = "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_Shoot"
    MUZZLE_FLASH_INDEX = 1  # SP_DeathMetalCat_Shoot_02
    NEW_FRAME_RUN = 5  # at fps=13, ~0.38s hold vs. the current ~0.077s

    fb = unreal.EditorAssetLibrary.load_asset(SHOOT_PATH)
    key_frames = fb.get_editor_property("key_frames")

    sprite_name = key_frames[MUZZLE_FLASH_INDEX].get_editor_property("sprite").get_name()
    unreal.log(f"[shoot] keyframe[{MUZZLE_FLASH_INDEX}] sprite={sprite_name} (expected SP_DeathMetalCat_Shoot_02)")

    target = key_frames[MUZZLE_FLASH_INDEX]
    target.set_editor_property("frame_run", NEW_FRAME_RUN)
    key_frames[MUZZLE_FLASH_INDEX] = target
    fb.set_editor_property("key_frames", key_frames)

    fb.modify()
    saved = unreal.EditorAssetLibrary.save_loaded_asset(fb, only_if_is_dirty=False)
    unreal.log(f"[shoot] set frame_run={NEW_FRAME_RUN} on muzzle-flash keyframe, save returned {saved}")


def create_dodge_input():
    IA_PATH = "/Game/Input/IA_Dodge"
    IMC_PATH = "/Game/Input/IMC_PlayerControls"

    if unreal.EditorAssetLibrary.does_asset_exist(IA_PATH):
        ia_dodge = unreal.EditorAssetLibrary.load_asset(IA_PATH)
        unreal.log(f"[input] IA_Dodge already exists, reusing: {IA_PATH}")
    else:
        ia_dodge = asset_tools.create_asset("IA_Dodge", "/Game/Input", unreal.InputAction, unreal.InputAction_Factory())
        unreal.log(f"[input] created: {IA_PATH}")

    ia_dodge.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    unreal.EditorAssetLibrary.save_loaded_asset(ia_dodge, only_if_is_dirty=False)

    imc = unreal.EditorAssetLibrary.load_asset(IMC_PATH)
    imc.modify()

    dkm = imc.get_editor_property("default_key_mappings")
    existing = dkm.get_editor_property("mappings")
    already_mapped = any(
        m.get_editor_property("key").get_editor_property("key_name") == "LeftShift"
        and m.get_editor_property("action") == ia_dodge
        for m in existing
    )
    if already_mapped:
        unreal.log("[input] LeftShift -> IA_Dodge already mapped, skipping map_key")
    else:
        imc.map_key(ia_dodge, make_key("LeftShift"))
        unreal.log("[input] mapped LeftShift -> IA_Dodge")

    saved = unreal.EditorAssetLibrary.save_loaded_asset(imc, only_if_is_dirty=False)
    unreal.log(f"[input] IMC_PlayerControls save returned: {saved}")


fix_shoot_muzzle_flash()
create_dodge_input()
