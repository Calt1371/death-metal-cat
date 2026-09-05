import unreal

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level("/Game/L_ControllerTestRange")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()

manager = None
for a in all_actors:
    if a.get_class().get_name() == "RoomProgressionManager":
        manager = a
        break

if manager is None:
    raise RuntimeError("No RoomProgressionManager found in L_ControllerTestRange")

music = unreal.EditorAssetLibrary.load_asset("/Game/Audio/Music/DMC_Music")
if music is None:
    raise RuntimeError("DMC_Music asset not found -- run ue_import_level_music.py first")

manager.set_editor_property("background_music", music)
unreal.log(f"Assigned BackgroundMusic on {manager.get_actor_label()}: {manager.get_editor_property('background_music')}")

unreal.EditorLevelLibrary.save_current_level()
unreal.log("=== SAVED ===")
