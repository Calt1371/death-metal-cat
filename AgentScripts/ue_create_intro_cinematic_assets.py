"""
ue_create_intro_cinematic_assets.py

Builds every asset the intro cinematic needs, then wires them together. Idempotent -- existing
assets are reused and re-configured rather than duplicated, so it is safe to re-run. Mirrors
AgentScripts/ue_create_title_screen_assets.py (same asset shapes, same reasoning) -- see that
script's comments for anything not re-explained here.

Creates:
    Content/Movies/DeathMetalCatBackstory.mp4   (copied out of RawAssets so it packages)
    /Game/UI/IntroCinematic/MS_IntroCinematic    FileMediaSource -> ./Movies/DeathMetalCatBackstory.mp4
    /Game/UI/IntroCinematic/MP_IntroCinematic    MediaPlayer (PlayOnOpen, Loop off)
    /Game/UI/IntroCinematic/MT_IntroCinematic    MediaTexture bound to MP_IntroCinematic
    /Game/UI/IntroCinematic/BP_IntroCinematicGameMode  Blueprint child of AIntroCinematicGameMode,
                                                  GameplayMap defaulted to /Game/L_ControllerTestRange
                                                  (the hand-built live level -- see CLAUDE.md)
    /Game/Maps/L_IntroCinematic                  the cinematic map, GameMode override set

Also, once L_IntroCinematic exists, points BP_TitleScreenGameMode's IntroCinematicMap at it --
closing the loop from the title screen task, where that field was deliberately left unset because
this map didn't exist yet.

The C++ side (UIntroCinematicWidget) loads MP/MS/MT by these exact paths, so renaming anything here
means editing the path constants at the top of IntroCinematicWidget.cpp to match.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_intro_cinematic_assets.py').read())"
"""

import os
import shutil

import unreal

VIDEO_FILENAME = "DeathMetalCatBackstory.mp4"

INTRO_UI_PATH = "/Game/UI/IntroCinematic"
MAPS_PATH = "/Game/Maps"

MEDIA_SOURCE_NAME = "MS_IntroCinematic"
MEDIA_PLAYER_NAME = "MP_IntroCinematic"
MEDIA_TEXTURE_NAME = "MT_IntroCinematic"
GAMEMODE_BP_NAME = "BP_IntroCinematicGameMode"
LEVEL_NAME = "L_IntroCinematic"

# The confirmed real gameplay map -- a World Partition level (see its __ExternalActors__ folder),
# hand-built with Room1 etc. per CLAUDE.md's "Room1 in the live level" notes. Not under /Game/Maps
# like the screens this script/ue_create_title_screen_assets.py add -- it predates both and lives
# at the Content root.
GAMEPLAY_MAP_PATH = "/Game/L_ControllerTestRange"

TITLE_GAMEMODE_BP_PATH = "/Game/UI/TitleScreen/BP_TitleScreenGameMode"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
content_dir = os.path.join(project_dir, "Content")


def log(msg):
    unreal.log("[intro-cinematic] {}".format(msg))


# ---------------------------------------------------------------------------------------------
# 1. Get the .mp4 into Content/Movies so it ships with a packaged build. Same reasoning as the
# title screen's copy -- a FileMediaSource path only survives cooking if it is Content-relative.
# ---------------------------------------------------------------------------------------------
movies_dir = os.path.join(content_dir, "Movies")
dest_video = os.path.join(movies_dir, VIDEO_FILENAME)

if not os.path.isfile(dest_video):
    candidates = [
        os.path.join(project_dir, "RawAssets", "DMC_Media", VIDEO_FILENAME),
        os.path.join(r"C:\Users\calvi\Desktop\Projects\PythonTest", "RawAssets", "DMC_Media", VIDEO_FILENAME),
    ]
    source_video = next((c for c in candidates if os.path.isfile(c)), None)
    if source_video is None:
        raise RuntimeError(
            "Could not find {} in any of: {}".format(VIDEO_FILENAME, candidates))

    if not os.path.isdir(movies_dir):
        os.makedirs(movies_dir)
    shutil.copy2(source_video, dest_video)
    log("copied video -> {}".format(dest_video))
else:
    log("video already present at {}".format(dest_video))


def make_asset(name, package_path, asset_class, factory_class_name):
    full_path = "{}/{}".format(package_path, name)
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        log("reusing existing {}".format(full_path))
        return unreal.EditorAssetLibrary.load_asset(full_path)

    factory_class = getattr(unreal, factory_class_name, None)
    if factory_class is None:
        raise RuntimeError(
            "unreal.{} is not available -- is the MediaPlayerEditor plugin enabled?".format(factory_class_name))

    asset = asset_tools.create_asset(name, package_path, asset_class, factory_class())
    if asset is None:
        raise RuntimeError("create_asset returned None for {}".format(full_path))
    log("created {}".format(full_path))
    return asset


# ---------------------------------------------------------------------------------------------
# 2. FileMediaSource -> the .mp4
# ---------------------------------------------------------------------------------------------
media_source = make_asset(MEDIA_SOURCE_NAME, INTRO_UI_PATH, unreal.FileMediaSource, "FileMediaSourceFactoryNew")
media_source.set_editor_property("file_path", "./Movies/{}".format(VIDEO_FILENAME))
media_source.set_editor_property("precache_file", True)
log("media source file_path = {}".format(media_source.get_editor_property("file_path")))

# ---------------------------------------------------------------------------------------------
# 3. MediaPlayer
# ---------------------------------------------------------------------------------------------
media_player = make_asset(MEDIA_PLAYER_NAME, INTRO_UI_PATH, unreal.MediaPlayer, "MediaPlayerFactoryNew")
media_player.set_editor_property("play_on_open", True)
# Must stay False -- the cinematic plays exactly once; looping would fight the skip/completion
# hand-off in AIntroCinematicGameMode::HandleCinematicEnd.
media_player.set_editor_property("loop", False)

# ---------------------------------------------------------------------------------------------
# 4. MediaTexture, bound to the player
# ---------------------------------------------------------------------------------------------
media_texture = make_asset(MEDIA_TEXTURE_NAME, INTRO_UI_PATH, unreal.MediaTexture, "MediaTextureFactoryNew")
media_texture.set_editor_property("media_player", media_player)
media_texture.set_editor_property("auto_clear", False)
media_texture.set_editor_property("clear_color", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))

# ---------------------------------------------------------------------------------------------
# 5. GameMode Blueprint, with GameplayMap defaulted to the real live level
# ---------------------------------------------------------------------------------------------
gm_full_path = "{}/{}".format(INTRO_UI_PATH, GAMEMODE_BP_NAME)
if unreal.EditorAssetLibrary.does_asset_exist(gm_full_path):
    gm_bp = unreal.EditorAssetLibrary.load_asset(gm_full_path)
    log("reusing existing {}".format(gm_full_path))
else:
    gm_factory = unreal.BlueprintFactory()
    gm_factory.set_editor_property("parent_class", unreal.IntroCinematicGameMode)
    gm_bp = asset_tools.create_asset(GAMEMODE_BP_NAME, INTRO_UI_PATH, unreal.Blueprint, gm_factory)
    log("created {}".format(gm_full_path))

gm_class = gm_bp.generated_class()
gm_cdo = unreal.get_default_object(gm_class)

if not unreal.EditorAssetLibrary.does_asset_exist(GAMEPLAY_MAP_PATH):
    raise RuntimeError(
        "{} does not exist -- expected the hand-built live level here. "
        "If it has moved/been renamed, update GAMEPLAY_MAP_PATH at the top of this script.".format(GAMEPLAY_MAP_PATH))

gameplay_map_world = unreal.EditorAssetLibrary.load_asset(GAMEPLAY_MAP_PATH)
if gameplay_map_world is None:
    raise RuntimeError("Failed to load {} as an asset (expected a UWorld).".format(GAMEPLAY_MAP_PATH))
gm_cdo.set_editor_property("gameplay_map", gameplay_map_world)
log("BP_IntroCinematicGameMode GameplayMap = {}".format(gm_cdo.get_editor_property("gameplay_map")))

for asset in (media_source, media_player, media_texture, gm_bp):
    unreal.EditorAssetLibrary.save_loaded_asset(asset)

# ---------------------------------------------------------------------------------------------
# 6. The level itself, with the GameMode override pointed at our BP
# ---------------------------------------------------------------------------------------------
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_full_path = "{}/{}".format(MAPS_PATH, LEVEL_NAME)

if unreal.EditorAssetLibrary.does_asset_exist(level_full_path):
    log("reusing existing level {}".format(level_full_path))
    level_editor.load_level(level_full_path)
else:
    level_editor.new_level(level_full_path)
    log("created level {}".format(level_full_path))

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

world_settings_list = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)
if not world_settings_list:
    raise RuntimeError("Could not find a WorldSettings actor in {}".format(level_full_path))
world_settings = world_settings_list[0]
world_settings.set_editor_property("default_game_mode", gm_class)
log("world settings default_game_mode = {}".format(gm_class.get_path_name()))

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing_starts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)
if not existing_starts:
    actor_subsystem.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 100.0))
    log("placed a PlayerStart at the origin")

level_editor.save_current_level()
log("level saved.")

# ---------------------------------------------------------------------------------------------
# 7. Close the loop: point the title screen's IntroCinematicMap at this map, now that it exists.
# ---------------------------------------------------------------------------------------------
if unreal.EditorAssetLibrary.does_asset_exist(TITLE_GAMEMODE_BP_PATH):
    title_gm_bp = unreal.EditorAssetLibrary.load_asset(TITLE_GAMEMODE_BP_PATH)
    title_gm_cdo = unreal.get_default_object(title_gm_bp.generated_class())
    intro_level_world = unreal.EditorAssetLibrary.load_asset(level_full_path)
    if intro_level_world is None:
        raise RuntimeError("Failed to load {} as an asset (expected a UWorld).".format(level_full_path))
    title_gm_cdo.set_editor_property("intro_cinematic_map", intro_level_world)
    unreal.EditorAssetLibrary.save_loaded_asset(title_gm_bp)
    log("BP_TitleScreenGameMode IntroCinematicMap = {}".format(title_gm_cdo.get_editor_property("intro_cinematic_map")))
else:
    log("WARNING: {} not found -- title screen -> cinematic hand-off not wired. "
        "Re-run ue_create_title_screen_assets.py first.".format(TITLE_GAMEMODE_BP_PATH))

log("DONE.")
