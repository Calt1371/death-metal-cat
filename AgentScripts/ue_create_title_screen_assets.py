"""
ue_create_title_screen_assets.py

Builds every asset the title screen needs, then wires them together. Idempotent -- existing assets
are reused and re-configured rather than duplicated, so it is safe to re-run.

Creates:
    Content/Movies/DMC_Game_Title.mp4          (copied out of RawAssets so it packages)
    /Game/UI/TitleScreen/MS_TitleVideo         FileMediaSource -> ./Movies/DMC_Game_Title.mp4
    /Game/UI/TitleScreen/MP_TitleVideo         MediaPlayer (PlayOnOpen, Loop off)
    /Game/UI/TitleScreen/MT_TitleVideo         MediaTexture bound to MP_TitleVideo
    /Game/UI/TitleScreen/BP_TitleScreenGameMode  Blueprint child of ATitleScreenGameMode
    /Game/Maps/L_TitleScreen                   the title map, GameMode override set

The C++ side (UTitleScreenWidget) loads MP/MS/MT by these exact paths, so renaming anything here
means editing the path constants at the top of TitleScreenWidget.cpp to match.

Loop/Play is deliberately NOT configured here: the play -> freeze -> 20s hold -> replay cycle is
driven frame by frame in UTitleScreenWidget::UpdateVideoCycle, which needs the player's own looping
switched OFF to work. See that class's comment for why the freeze pauses just short of EOF.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_title_screen_assets.py').read())"
"""

import os
import shutil

import unreal

VIDEO_FILENAME = "DMC_Game_Title.mp4"

TITLE_UI_PATH = "/Game/UI/TitleScreen"
MAPS_PATH = "/Game/Maps"

MEDIA_SOURCE_NAME = "MS_TitleVideo"
MEDIA_PLAYER_NAME = "MP_TitleVideo"
MEDIA_TEXTURE_NAME = "MT_TitleVideo"
GAMEMODE_BP_NAME = "BP_TitleScreenGameMode"
LEVEL_NAME = "L_TitleScreen"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
content_dir = os.path.join(project_dir, "Content")


def log(msg):
    unreal.log("[title-screen] {}".format(msg))


# ---------------------------------------------------------------------------------------------
# 1. Get the .mp4 into Content/Movies so it ships with a packaged build.
#
# A FileMediaSource path beginning "./" resolves relative to the Content directory, which is the
# only form that survives packaging -- an absolute path into RawAssets would work in the editor and
# then break the moment the game is cooked.
# ---------------------------------------------------------------------------------------------
movies_dir = os.path.join(content_dir, "Movies")
dest_video = os.path.join(movies_dir, VIDEO_FILENAME)

if not os.path.isfile(dest_video):
    # This worktree/checkout may not have RawAssets (it is untracked), so fall back to the main
    # working copy before giving up.
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
    """Create (or load) one asset. Factories live in editor-only modules, so resolve by name and
    fail loudly rather than assuming a given factory is exposed to Python in this engine build."""
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
media_source = make_asset(MEDIA_SOURCE_NAME, TITLE_UI_PATH, unreal.FileMediaSource, "FileMediaSourceFactoryNew")
media_source.set_editor_property("file_path", "./Movies/{}".format(VIDEO_FILENAME))
# Keeping the whole 5MB clip in memory removes any chance of a hitch on the replay seek.
media_source.set_editor_property("precache_file", True)
log("media source file_path = {}".format(media_source.get_editor_property("file_path")))

# ---------------------------------------------------------------------------------------------
# 3. MediaPlayer
# ---------------------------------------------------------------------------------------------
media_player = make_asset(MEDIA_PLAYER_NAME, TITLE_UI_PATH, unreal.MediaPlayer, "MediaPlayerFactoryNew")
media_player.set_editor_property("play_on_open", True)
# Must stay False -- UTitleScreenWidget drives the loop by hand so it can hold the frozen frame.
media_player.set_editor_property("loop", False)

# ---------------------------------------------------------------------------------------------
# 4. MediaTexture, bound to the player
# ---------------------------------------------------------------------------------------------
media_texture = make_asset(MEDIA_TEXTURE_NAME, TITLE_UI_PATH, unreal.MediaTexture, "MediaTextureFactoryNew")
media_texture.set_editor_property("media_player", media_player)
# AutoClear would blank the texture the moment playback stops, which is exactly the frozen title
# frame the 20-second hold is meant to be showing.
media_texture.set_editor_property("auto_clear", False)
media_texture.set_editor_property("clear_color", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))

# ---------------------------------------------------------------------------------------------
# 5. GameMode Blueprint (a BP child so IntroCinematicMap can be set in the editor later)
# ---------------------------------------------------------------------------------------------
gm_full_path = "{}/{}".format(TITLE_UI_PATH, GAMEMODE_BP_NAME)
if unreal.EditorAssetLibrary.does_asset_exist(gm_full_path):
    gm_bp = unreal.EditorAssetLibrary.load_asset(gm_full_path)
    log("reusing existing {}".format(gm_full_path))
else:
    gm_factory = unreal.BlueprintFactory()
    gm_factory.set_editor_property("parent_class", unreal.TitleScreenGameMode)
    gm_bp = asset_tools.create_asset(GAMEMODE_BP_NAME, TITLE_UI_PATH, unreal.Blueprint, gm_factory)
    log("created {}".format(gm_full_path))

gm_class = gm_bp.generated_class()

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

# A PlayerStart is not strictly required (ATitleScreenGameMode has a null DefaultPawnClass, so
# nothing is spawned into it) but its absence produces a warning on every play, so place one.
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing_starts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)
if not existing_starts:
    actor_subsystem.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 100.0))
    log("placed a PlayerStart at the origin")

level_editor.save_current_level()
log("DONE -- assets built and level saved.")
