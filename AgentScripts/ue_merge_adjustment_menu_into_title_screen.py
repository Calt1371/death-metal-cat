"""
ue_merge_adjustment_menu_into_title_screen.py

Undoes the earlier "separate L_AdjustmentMenu level, OpenLevel into L_TitleScreen" draft and merges
the Adjustment Menu into L_TitleScreen's own GameMode instead -- see AAdjustmentMenuGameMode's class
comment for why: a level transition ahead of UTitleIntroCombinedWidget's OpenSource call broke the
title video (IsPlaying=1 but Time stuck at 0.000, 2x2 placeholder texture), confirmed live via two
full -game playthroughs. AAdjustmentMenuGameMode now owns the WHOLE pre-gameplay experience
(Adjustment Menu -> title loop -> intro) in ONE continuous level/World, so this script:

    1. Points L_TitleScreen's World Settings GameMode Override at the raw C++ AAdjustmentMenuGameMode
       (no Blueprint wrapper -- same reasoning as AGameplayPlayerController). BP_TitleScreenGameMode
       (deriving from the now-unused ATitleScreenGameMode) is left in place, unreferenced, per this
       project's established "don't delete still-referenced-by-nothing-but-still-valid classes"
       convention (same as L_IntroCinematic/UIntroCinematicWidget).
    2. Deletes /Game/Maps/L_AdjustmentMenu -- no longer used, was only ever a wrapper level for the
       now-removed separate-level draft.
    3. Reverts Config/DefaultEngine.ini's GameDefaultMap back to /Game/Maps/L_TitleScreen.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\.claude\\worktrees\\adjustment-menu\\AgentScripts\\ue_merge_adjustment_menu_into_title_screen.py').read())"
"""

import os

import unreal

MAPS_PATH = "/Game/Maps"
TITLE_LEVEL_NAME = "L_TitleScreen"
OLD_ADJUSTMENT_LEVEL_NAME = "L_AdjustmentMenu"

project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def log(msg):
    unreal.log("[merge-adjustment-menu] {}".format(msg))


# ---------------------------------------------------------------------------------------------
# 1. L_TitleScreen's GameMode Override -> AAdjustmentMenuGameMode
# ---------------------------------------------------------------------------------------------
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
title_level_full_path = "{}/{}".format(MAPS_PATH, TITLE_LEVEL_NAME)

if not unreal.EditorAssetLibrary.does_asset_exist(title_level_full_path):
    raise RuntimeError("{} does not exist.".format(title_level_full_path))

level_editor.load_level(title_level_full_path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

world_settings_list = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)
if not world_settings_list:
    raise RuntimeError("Could not find a WorldSettings actor in {}".format(title_level_full_path))
world_settings = world_settings_list[0]
world_settings.set_editor_property("default_game_mode", unreal.AdjustmentMenuGameMode)
log("{} default_game_mode = {}".format(title_level_full_path, unreal.AdjustmentMenuGameMode))

level_editor.save_current_level()
log("level saved.")

# ---------------------------------------------------------------------------------------------
# 2. Delete the now-unused separate Adjustment Menu level.
# ---------------------------------------------------------------------------------------------
old_level_full_path = "{}/{}".format(MAPS_PATH, OLD_ADJUSTMENT_LEVEL_NAME)
if unreal.EditorAssetLibrary.does_asset_exist(old_level_full_path):
    if unreal.EditorAssetLibrary.delete_asset(old_level_full_path):
        log("deleted {}".format(old_level_full_path))
    else:
        log("WARNING: failed to delete {} -- delete it manually.".format(old_level_full_path))
else:
    log("{} already gone.".format(old_level_full_path))

# ---------------------------------------------------------------------------------------------
# 3. GameDefaultMap -> back to L_TitleScreen.
# ---------------------------------------------------------------------------------------------
ini_path = os.path.join(project_dir, "Config", "DefaultEngine.ini")
with open(ini_path, "r", encoding="utf-8") as f:
    ini_lines = f.readlines()

OLD_LINE_PREFIX = "GameDefaultMap="
NEW_VALUE = "/Game/Maps/{}".format(TITLE_LEVEL_NAME)
changed = False
for i, line in enumerate(ini_lines):
    if line.strip().startswith(OLD_LINE_PREFIX):
        old_value = line.strip()[len(OLD_LINE_PREFIX):]
        if old_value != NEW_VALUE:
            ini_lines[i] = "{}{}\n".format(OLD_LINE_PREFIX, NEW_VALUE)
            changed = True
            log("GameDefaultMap: {} -> {}".format(old_value, NEW_VALUE))
        else:
            log("GameDefaultMap already {}".format(NEW_VALUE))
        break
else:
    raise RuntimeError("No GameDefaultMap= line found in {}".format(ini_path))

if changed:
    with open(ini_path, "w", encoding="utf-8") as f:
        f.writelines(ini_lines)
    log("wrote {}".format(ini_path))

log("DONE.")
