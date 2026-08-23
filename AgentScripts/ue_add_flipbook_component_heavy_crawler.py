"""
ue_add_flipbook_component_heavy_crawler.py

Fixes DeathBotHeavy/DeathBotCrawler rendering as a completely blank/empty actor in the level.
Root cause: DeathMetalCatEnemyBase has NO native UPaperFlipbookComponent -- Tick's flipbook-
selection logic only ever touches whatever PaperFlipbookComponent it finds via
FindComponentByClass, and BP_EnemyDeathBotWalking/BP_EnemyDeathBotFlying only have one because each
of THOSE Blueprints was manually given one (confirmed: their CDOs show zero PaperFlipbookComponents
too, exactly like Heavy/Crawler -- so whatever adds it isn't part of the C++ class or a visible
Python-inspectable default, it's Blueprint-side setup this Python-only creation flow never
replicated). ue_create_deathbot_heavy_crawler_bp.py wired Walk/Attack flipbook *properties* onto
Heavy/Crawler, but never gave either Blueprint an actual component to display them with.

Adds one PaperFlipbookComponent to each Blueprint via SubobjectDataSubsystem (attached to the
existing root), matching what Walking/Flying already have at runtime.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_add_flipbook_component_heavy_crawler.py').read())"
"""

import unreal

subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)

BP_PATHS = [
    "/Game/Characters/Enemies/DeathBotHeavy/Blueprints/BP_EnemyDeathBotHeavy",
    "/Game/Characters/Enemies/DeathBotCrawler/Blueprints/BP_EnemyDeathBotCrawler",
]

for bp_path in BP_PATHS:
    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(bp)
    unreal.log(f"{bp_path}: {len(handles)} existing subobject handles")

    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", handles[0])
    params.set_editor_property("new_class", unreal.PaperFlipbookComponent)
    params.set_editor_property("blueprint_context", bp)
    params.set_editor_property("conform_transform_to_parent", True)

    new_handle, fail_reason = subsystem.add_new_subobject(params)
    unreal.log(f"  add_new_subobject -> handle valid, fail_reason='{fail_reason}'")

    subsystem.rename_subobject(new_handle, "FlipbookComponent")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)

    cdo = unreal.get_default_object(bp.generated_class())
    comps = cdo.get_components_by_class(unreal.PaperFlipbookComponent)
    unreal.log(f"  verify: {len(comps)} PaperFlipbookComponent(s) on CDO after add")

unreal.log("=== FLIPBOOK COMPONENT ADD COMPLETE ===")
