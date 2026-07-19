import unreal

IMC_PATH = "/Game/Input/IMC_PlayerControls"
IA_DODGE_PATH = "/Game/Input/IA_Dodge"
IA_SWORDATTACK_PATH = "/Game/Input/IA_SwordAttack"
SHOOT_PATH = "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_Shoot"

imc = unreal.EditorAssetLibrary.load_asset(IMC_PATH)
shoot_fb = unreal.EditorAssetLibrary.load_asset(SHOOT_PATH)
pkgs = [imc.get_package(), shoot_fb.get_package()]
reloaded, err = unreal.EditorLoadingAndSavingUtils.reload_packages(
    pkgs, unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE
)
print("reload_packages -> reloaded:", reloaded, " error:", err)

imc_fresh = unreal.EditorAssetLibrary.load_asset(IMC_PATH)
dkm = imc_fresh.get_editor_property("default_key_mappings")
mappings = dkm.get_editor_property("mappings")
print(f"=== IMC_PlayerControls: {len(mappings)} mappings ===")
for i, m in enumerate(mappings):
    key = m.get_editor_property("key").get_editor_property("key_name")
    action = m.get_editor_property("action")
    mods = m.get_editor_property("modifiers")
    mod_desc = []
    for mod in mods:
        if mod is None:
            mod_desc.append("NULL")
        elif isinstance(mod, unreal.InputModifierNegate):
            mod_desc.append(f"Negate(x={mod.get_editor_property('x')},y={mod.get_editor_property('y')},z={mod.get_editor_property('z')})")
        else:
            mod_desc.append(type(mod).__name__)
    print(f"  [{i}] key={key}  action={action.get_name() if action else None}  modifiers={mod_desc}")

ia_dodge_fresh = unreal.EditorAssetLibrary.load_asset(IA_DODGE_PATH)
print(f"IA_Dodge exists={unreal.EditorAssetLibrary.does_asset_exist(IA_DODGE_PATH)}  value_type={ia_dodge_fresh.get_editor_property('value_type') if ia_dodge_fresh else None}")

ia_sword_fresh = unreal.EditorAssetLibrary.load_asset(IA_SWORDATTACK_PATH)
print(f"IA_SwordAttack exists={unreal.EditorAssetLibrary.does_asset_exist(IA_SWORDATTACK_PATH)}  value_type={ia_sword_fresh.get_editor_property('value_type') if ia_sword_fresh else None}")

shoot_fresh = unreal.EditorAssetLibrary.load_asset(SHOOT_PATH)
print("=== FB_DeathMetalCat_Shoot keyframes ===")
for i, kf in enumerate(shoot_fresh.get_editor_property("key_frames")):
    sprite = kf.get_editor_property("sprite")
    print(f"  [{i}] sprite={sprite.get_name() if sprite else None}  frame_run={kf.get_editor_property('frame_run')}")
