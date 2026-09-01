import unreal

bp = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat.BP_DeathMetalCat")
gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)

print("BEFORE -- BaseSpriteTintColor:", cdo.get_editor_property("BaseSpriteTintColor"))
cdo.set_editor_property("BaseSpriteTintColor", unreal.LinearColor(0.02, 0.02, 0.02, 1.0))
print("AFTER -- BaseSpriteTintColor:", cdo.get_editor_property("BaseSpriteTintColor"))

sprite = cdo.get_editor_property("sprite")
sprite.set_editor_property("sprite_color", unreal.LinearColor(0.02, 0.02, 0.02, 1.0))
print("sprite template sprite_color:", sprite.get_editor_property("sprite_color"))

unreal.EditorAssetLibrary.save_loaded_asset(bp)
print("saved BP_DeathMetalCat")
