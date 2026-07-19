"""
ue_create_enemy_material.py

Creates M_EnemyPlaceholder: a minimal material exposing a single VectorParameter "Color", wired
to both Base Color and Emissive Color. ADeathMetalCatEnemyBase's placeholder mesh uses a Dynamic
Material Instance of this material so C++ can reliably set/flash its color at runtime (a color
param on an arbitrary/default engine material can't be assumed to exist -- this guarantees it does).

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_enemy_material.py').read())"
"""

import unreal

DEST = "/Game/Characters/EnemyBase"
NAME = "M_EnemyPlaceholder"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
full_path = f"{DEST}/{NAME}"

if unreal.EditorAssetLibrary.does_asset_exist(full_path):
    material = unreal.EditorAssetLibrary.load_asset(full_path)
    unreal.log(f"[material] already exists, reusing: {full_path}")
else:
    material = asset_tools.create_asset(NAME, DEST, unreal.Material, unreal.MaterialFactoryNew())
    unreal.log(f"[material] created: {full_path}")

vec_param = unreal.MaterialEditingLibrary.create_material_expression(
    material, unreal.MaterialExpressionVectorParameter, -300, 0
)
vec_param.set_editor_property("parameter_name", "Color")
vec_param.set_editor_property("default_value", unreal.LinearColor(0.6, 0.1, 0.1, 1.0))

unreal.MaterialEditingLibrary.connect_material_property(vec_param, "", unreal.MaterialProperty.MP_BASE_COLOR)
unreal.MaterialEditingLibrary.connect_material_property(vec_param, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

unreal.MaterialEditingLibrary.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material)

unreal.log("=== M_EnemyPlaceholder MATERIAL COMPLETE ===")
unreal.log(full_path)
