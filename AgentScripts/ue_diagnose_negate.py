import unreal

IMC_PATH = "/Game/Input/IMC_PlayerControls"

imc = unreal.EditorAssetLibrary.load_asset(IMC_PATH)
pkg = imc.get_package()
reloaded, err = unreal.EditorLoadingAndSavingUtils.reload_packages(
    [pkg], unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE
)
print("reload_packages -> reloaded:", reloaded, " error:", err)

imc_fresh = unreal.EditorAssetLibrary.load_asset(IMC_PATH)
dkm = imc_fresh.get_editor_property("default_key_mappings")
mappings = dkm.get_editor_property("mappings")
print(f"mapping count: {len(mappings)}")

for i, m in enumerate(mappings):
    key = m.get_editor_property("key").get_editor_property("key_name")
    action = m.get_editor_property("action")
    mods = m.get_editor_property("modifiers")
    print(f"--- mapping[{i}] key={key} action={action.get_name() if action else None} modifier_count={len(mods)} ---")
    for j, mod in enumerate(mods):
        if mod is None:
            print(f"    modifier[{j}] = None (NULL REFERENCE)")
            continue
        print(f"    modifier[{j}] class={type(mod).__name__} path={mod.get_path_name()} outer={mod.get_outer()}")
        if isinstance(mod, unreal.InputModifierNegate):
            x = mod.get_editor_property("x")
            y = mod.get_editor_property("y")
            z = mod.get_editor_property("z")
            print(f"        x={x} y={y} z={z}")
