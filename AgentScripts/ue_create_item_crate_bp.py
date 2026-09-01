import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.load_class(None, "/Script/PythonTest.ItemPickupCrate"))

bp_path = "/Game/Items/Crate/BP_ItemCrate"
bp = unreal.load_object(None, bp_path + ".BP_ItemCrate")
if bp is None:
    bp = asset_tools.create_asset("BP_ItemCrate", "/Game/Items/Crate", unreal.Blueprint, factory)

gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)

sprite_asset = unreal.load_object(None, "/Game/Items/Crate/SP_ItemCrate.SP_ItemCrate")
print("sprite asset:", sprite_asset)

sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib = unreal.SubobjectDataBlueprintFunctionLibrary
handles = sds.k2_gather_subobject_data_for_blueprint(bp)
sprite_comp = None
for h in handles:
    data = sds.k2_find_subobject_data_from_handle(h)
    var_name = str(lib.get_variable_name(data))
    print("component:", var_name)
    if var_name == "SpriteComponent":
        sprite_comp = lib.get_associated_object(data)

if sprite_comp:
    sprite_comp.set_editor_property("source_sprite", sprite_asset)
    print("assigned sprite to SpriteComponent template")
else:
    print("WARNING: SpriteComponent template not found")

unreal.EditorAssetLibrary.save_loaded_asset(bp)
print("saved BP_ItemCrate")
