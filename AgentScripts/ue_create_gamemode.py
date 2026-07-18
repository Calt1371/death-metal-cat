"""
ue_create_gamemode.py

Creates /Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCatGameMode, a Blueprint
child of AGameModeBase, with DefaultPawnClass set to BP_DeathMetalCat.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_gamemode.py').read())"
"""

import unreal

DEST_PATH = "/Game/Characters/DeathMetalCat/Blueprints"
GM_NAME = "BP_DeathMetalCatGameMode"
PAWN_BP_PATH = "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# -- resolve the pawn class we need to assign --
pawn_bp = unreal.EditorAssetLibrary.load_asset(PAWN_BP_PATH)
if pawn_bp is None:
    raise RuntimeError(f"Could not load {PAWN_BP_PATH} -- does BP_DeathMetalCat actually exist at this path?")
pawn_class = pawn_bp.generated_class()
if pawn_class is None:
    raise RuntimeError(f"{PAWN_BP_PATH} has no generated_class() -- has it been compiled/saved at least once?")
unreal.log(f"[gamemode] resolved pawn class: {pawn_class.get_path_name()}")

# -- create (or reuse) the game mode blueprint --
gm_full_path = f"{DEST_PATH}/{GM_NAME}"
if unreal.EditorAssetLibrary.does_asset_exist(gm_full_path):
    gm_bp = unreal.EditorAssetLibrary.load_asset(gm_full_path)
    unreal.log(f"[gamemode] already exists, reusing: {gm_full_path}")
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.GameModeBase)
    gm_bp = asset_tools.create_asset(GM_NAME, DEST_PATH, unreal.Blueprint, factory)
    unreal.log(f"[gamemode] created: {gm_full_path}")

# -- set DefaultPawnClass on the CDO --
gm_class = gm_bp.generated_class()
cdo = unreal.get_default_object(gm_class)
cdo.set_editor_property("default_pawn_class", pawn_class)

unreal.EditorAssetLibrary.save_loaded_asset(gm_bp)
unreal.log("[gamemode] saved.")

# -- verify from the in-memory object (not yet a from-disk reload, that happens in a separate pass) --
cdo_check = unreal.get_default_object(gm_bp.generated_class())
unreal.log(f"[gamemode] DefaultPawnClass immediately after save: {cdo_check.get_editor_property('default_pawn_class')}")
