import unreal

bp = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat.BP_DeathMetalCat")
gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)

fb = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_CatNipJumpMirrored.FB_DeathMetalCat_CatNipJumpMirrored")
if fb is None:
    raise RuntimeError("mirrored jump flipbook not found")
cdo.set_editor_property("CatNipJumpFlipbookMirrored", fb)
print("CatNipJumpFlipbookMirrored set to:", cdo.get_editor_property("CatNipJumpFlipbookMirrored"))

unreal.EditorAssetLibrary.save_loaded_asset(bp)
print("saved BP_DeathMetalCat")
