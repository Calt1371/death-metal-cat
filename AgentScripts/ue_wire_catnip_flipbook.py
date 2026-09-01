import unreal

bp = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat.BP_DeathMetalCat")
gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)

fb = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_CatNipIdle.FB_DeathMetalCat_CatNipIdle")
cdo.set_editor_property("CatNipIdleFlipbook", fb)
print("CatNipIdleFlipbook set to:", cdo.get_editor_property("CatNipIdleFlipbook"))

unreal.EditorAssetLibrary.save_loaded_asset(bp)
print("saved BP_DeathMetalCat")
