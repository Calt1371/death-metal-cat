import unreal

bp = unreal.load_object(None, "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat.BP_DeathMetalCat")
gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)

mapping = {
    "CatNipWalkFlipbook": "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_CatNipWalk.FB_DeathMetalCat_CatNipWalk",
    "CatNipAttackFlipbook": "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_CatNipAttack.FB_DeathMetalCat_CatNipAttack",
    "CatNipJumpFlipbook": "/Game/Characters/DeathMetalCat/Flipbooks/FB_DeathMetalCat_CatNipJump.FB_DeathMetalCat_CatNipJump",
}

for prop, path in mapping.items():
    fb = unreal.load_object(None, path)
    if fb is None:
        raise RuntimeError(f"Failed to load {path}")
    cdo.set_editor_property(prop, fb)
    print(f"{prop} set to: {cdo.get_editor_property(prop)}")

unreal.EditorAssetLibrary.save_loaded_asset(bp)
print("saved BP_DeathMetalCat")
