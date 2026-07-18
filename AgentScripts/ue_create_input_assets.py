"""
ue_create_input_assets.py

Runs INSIDE the UE5 editor's Python environment (via remote execution).
Creates the permanent Enhanced Input assets for Death Metal Cat's left/right movement:

  /Game/Input/IA_Move            (InputAction, Axis1D)
  /Game/Input/IMC_PlayerControls (InputMappingContext)
      D          -> IA_Move  +1.0
      A          -> IA_Move  -1.0 (via Negate modifier)
      Right      -> IA_Move  +1.0
      Left       -> IA_Move  -1.0 (via Negate modifier)

Invoke from outside via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_input_assets.py').read())"
"""

import unreal

INPUT_DEST = "/Game/Input"
IA_NAME = "IA_Move"
IMC_NAME = "IMC_PlayerControls"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def make_key(key_name):
    k = unreal.Key()
    k.set_editor_property("key_name", key_name)
    return k


def add_negate_modifier(imc, mapping_index):
    """Attaches an InputModifierNegate to the mapping at mapping_index in imc's DefaultKeyMappings.
    Struct array elements are returned by value in the UE Python API, so the mutated element has
    to be written all the way back up: element -> mappings list -> DefaultKeyMappings -> imc.

    Two things matter here, both learned the hard way from a live bug (see git history):
    1. The modifier object MUST be constructed with outer=imc (unreal.new_object, not a bare
       unreal.InputModifierNegate() call). Without a proper Outer pointing into imc's own
       package, the object isn't included when the package is serialized -- it works fine for
       the rest of that same editor session (still a valid in-memory object) but resolves to a
       null reference after any real reload (editor restart, or EditorLoadingAndSavingUtils.
       reload_packages), which makes the modifier silently no-op at runtime.
    2. This deep a property mutation (asset -> struct -> array -> struct -> array -> object)
       does not reliably mark the package dirty on its own -- call imc.modify() before mutating,
       and save with only_if_is_dirty=False, or the save can silently no-op.
    """
    negate = unreal.new_object(unreal.InputModifierNegate, outer=imc)
    negate.set_editor_property("x", True)
    negate.set_editor_property("y", True)
    negate.set_editor_property("z", True)

    dkm = imc.get_editor_property("default_key_mappings")
    mappings_list = dkm.get_editor_property("mappings")
    target = mappings_list[mapping_index]
    target.set_editor_property("modifiers", [negate])
    mappings_list[mapping_index] = target
    dkm.set_editor_property("mappings", mappings_list)
    imc.set_editor_property("default_key_mappings", dkm)


def main():
    # -- IA_Move --
    ia_path = f"{INPUT_DEST}/{IA_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(ia_path):
        ia_move = unreal.EditorAssetLibrary.load_asset(ia_path)
        unreal.log(f"[input] IA_Move already exists, reusing: {ia_path}")
    else:
        ia_move = asset_tools.create_asset(IA_NAME, INPUT_DEST, unreal.InputAction, unreal.InputAction_Factory())
        unreal.log(f"[input] created: {ia_path}")

    ia_move.set_editor_property("value_type", unreal.InputActionValueType.AXIS1D)
    unreal.EditorAssetLibrary.save_loaded_asset(ia_move)

    # -- IMC_PlayerControls --
    imc_path = f"{INPUT_DEST}/{IMC_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(imc_path):
        imc = unreal.EditorAssetLibrary.load_asset(imc_path)
        imc.unmap_all()
        unreal.log(f"[input] IMC_PlayerControls already exists, clearing existing mappings: {imc_path}")
    else:
        imc = asset_tools.create_asset(IMC_NAME, INPUT_DEST, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
        unreal.log(f"[input] created: {imc_path}")

    # D -> +1 (no modifier needed, base axis value is already +1 when pressed)
    imc.map_key(ia_move, make_key("D"))
    # A -> -1 (Negate modifier flips the base +1 to -1)
    imc.map_key(ia_move, make_key("A"))
    add_negate_modifier(imc, -1)
    # Right -> +1
    imc.map_key(ia_move, make_key("Right"))
    # Left -> -1
    imc.map_key(ia_move, make_key("Left"))
    add_negate_modifier(imc, -1)

    imc.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(imc, only_if_is_dirty=False)

    # -- verify --
    dkm = imc.get_editor_property("default_key_mappings")
    unreal.log("=== IMC_PlayerControls final mappings ===")
    for m in dkm.get_editor_property("mappings"):
        key = m.get_editor_property("key").get_editor_property("key_name")
        mods = m.get_editor_property("modifiers")
        unreal.log(f"  key={key}  modifiers={[type(mm).__name__ for mm in mods]}")

    unreal.log(f"IA_Move value_type = {ia_move.get_editor_property('value_type')}")
    unreal.log("=== DONE ===")


main()
