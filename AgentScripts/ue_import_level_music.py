"""
ue_import_level_music.py

Imports DMC_Music.mp3 as a looping USoundWave for L_ControllerTestRange's background music.
UE 5.8's built-in SoundFactory imports .mp3 directly (confirmed live) -- no .wav conversion needed,
unlike the earlier SFX pipeline (ue_wire_sfx.py), which predates this and used Freesound .mp3
previews that needed converting.

Invoke via:
    python send_to_ue.py "exec(open(r'...\\ue_import_level_music.py').read())"
"""

import unreal

DEST = "/Game/Audio/Music"
SRC = r"C:\Users\calvi\Desktop\Projects\PythonTest\Content\Audio\Music\DMC_Music.mp3"
NAME = "DMC_Music"

# Remove the earlier throwaway test import if it's still around.
test_path = f"{DEST}/DMC_Music_Test"
if unreal.EditorAssetLibrary.does_asset_exist(test_path):
    unreal.EditorAssetLibrary.delete_asset(test_path)
    unreal.log(f"Removed test asset: {test_path}")

task = unreal.AssetImportTask()
task.filename = SRC
task.destination_path = DEST
task.destination_name = NAME
task.automated = True
task.save = True
task.replace_existing = True
task.factory = unreal.SoundFactory()

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

dest_path = f"{DEST}/{NAME}"
music = unreal.EditorAssetLibrary.load_asset(dest_path)
if music is None:
    raise RuntimeError(f"music import failed, no asset at {dest_path}")

music.set_editor_property("looping", True)
music.set_editor_property("sound_group", unreal.SoundGroup.SOUNDGROUP_MUSIC)
music.modify()
unreal.EditorAssetLibrary.save_loaded_asset(music)

unreal.log(f"[import] {dest_path}  duration={music.get_editor_property('duration'):.1f}s  looping={music.get_editor_property('looping')}")
unreal.log("=== MUSIC IMPORT COMPLETE ===")
