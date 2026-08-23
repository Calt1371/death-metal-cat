"""
ue_create_room_barrier_bp.py

Creates BP_RoomBarrier, a plain Blueprint subclass of ARoomBarrier with BarrierFlipbook set to
FB_Trap_RoomBarrier -- the reusable asset a level designer places at a room's exit doorway and
attaches to that room's RoomShell (same convention as RoomExitTrigger/EncounterSpawnMarker).

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_room_barrier_bp.py').read())"
"""

import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
base_class = unreal.load_class(None, "/Script/PythonTest.RoomBarrier")

BP_DEST = "/Game/Environments/CityBiome/Traps/Blueprints"
BP_NAME = "BP_RoomBarrier"
BP_PATH = BP_DEST + "/" + BP_NAME

if unreal.EditorAssetLibrary.does_asset_exist(BP_PATH):
    bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
    unreal.log(f"{BP_PATH} already exists, reusing")
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", base_class)
    bp = asset_tools.create_asset(BP_NAME, BP_DEST, unreal.Blueprint, factory)
    unreal.log(f"created {BP_PATH}")

cdo = unreal.get_default_object(bp.generated_class())
flipbook = unreal.EditorAssetLibrary.load_asset("/Game/Environments/CityBiome/Traps/Flipbooks/FB_Trap_RoomBarrier")
if flipbook is None:
    raise RuntimeError("missing FB_Trap_RoomBarrier")
cdo.set_editor_property("barrier_flipbook", flipbook)
cdo.modify()

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log(f"{BP_NAME}: barrier_flipbook -> FB_Trap_RoomBarrier")
unreal.log("=== BP_ROOMBARRIER CREATE COMPLETE ===")
