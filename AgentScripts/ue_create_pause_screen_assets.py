"""
ue_create_pause_screen_assets.py

Builds every asset the pause screen needs, then wires them together. Idempotent -- existing assets
are reused/re-configured rather than duplicated, so it is safe to re-run.

Creates:
    /Game/UI/PauseMenu/T_PauseScreen       standard UI texture, same settings as
                                            ue_import_death_screen.py/ue_import_gnarly_logo.py.
                                            Source is Pause_screen_DMC.png, imported directly with no
                                            processing -- the art itself leaves the whole panel below
                                            the PAUSED title/divider blank, so UPauseMenuWidget draws
                                            all of its content (main list, Options, Controls) straight
                                            into that empty space with nothing to erase first. (An
                                            earlier version of this art baked the menu list in, which
                                            needed a patch-out step -- superseded, no longer used.)
    /Game/UI/PauseMenu/MIX_MasterVolume    USoundMix -- see UDMCGameInstance::ApplyStartupSettings.

Also wires:
    BP_DeathMetalCatGameMode.PlayerControllerClass = AGameplayPlayerController -- previously unset
    (defaulting to the engine's plain APlayerController), so real gameplay had no pause capability
    at all until this.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_create_pause_screen_assets.py').read())"
"""

import os

import unreal

PAUSE_UI_PATH = "/Game/UI/PauseMenu"
TEX_NAME = "T_PauseScreen"
MIX_NAME = "MIX_MasterVolume"
SOURCE_PNG_FILENAME = "Pause_screen_DMC.png"

GAMEPLAY_GAMEMODE_BP_PATH = "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCatGameMode"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
content_dir = os.path.join(project_dir, "Content")


def log(msg):
    unreal.log("[pause-screen] {}".format(msg))


# Resolve relative to whichever checkout/worktree the running editor actually has open (its own
# Content dir), falling back to the main checkout -- same reasoning as
# ue_create_title_screen_assets.py/ue_create_intro_cinematic_assets.py's own video-source lookup:
# a worktree may not have every loose source file the main checkout does.
_candidates = [
    os.path.join(content_dir, SOURCE_PNG_FILENAME),
    os.path.join(r"C:\Users\calvi\Desktop\Projects\PythonTest", "Content", SOURCE_PNG_FILENAME),
]
TEX_SRC_PATH = next((c for c in _candidates if os.path.isfile(c)), None)
if TEX_SRC_PATH is None:
    raise RuntimeError("Could not find {} in any of: {}".format(SOURCE_PNG_FILENAME, _candidates))
log("using source PNG: {}".format(TEX_SRC_PATH))


# ---------------------------------------------------------------------------------------------
# 1. Background texture -- standard UI texture import, same settings as every other static UI
# image in this project (ue_import_death_screen.py/ue_import_gnarly_logo.py).
# ---------------------------------------------------------------------------------------------
task = unreal.AssetImportTask()
task.filename = TEX_SRC_PATH
task.destination_path = PAUSE_UI_PATH
task.destination_name = TEX_NAME
task.automated = True
task.save = True
task.replace_existing = True
task.factory = unreal.TextureFactory()
asset_tools.import_asset_tasks([task])

tex_full_path = "{}/{}".format(PAUSE_UI_PATH, TEX_NAME)
texture = unreal.EditorAssetLibrary.load_asset(tex_full_path)
if texture is None:
    raise RuntimeError("texture import failed, no asset at {}".format(tex_full_path))

texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("srgb", True)
texture.modify()
unreal.EditorAssetLibrary.save_loaded_asset(texture)
log("{}  size={}x{}".format(tex_full_path, texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))

# ---------------------------------------------------------------------------------------------
# 2. MIX_MasterVolume -- a USoundMix whose class override on the engine's stock Master sound class
# is what UDMCGameInstance actually adjusts. No SoundMixFactory needed -- USoundMix has no special
# import requirements, so this is created directly like the title/intro GameModes' Blueprints.
# ---------------------------------------------------------------------------------------------
mix_full_path = "{}/{}".format(PAUSE_UI_PATH, MIX_NAME)
if unreal.EditorAssetLibrary.does_asset_exist(mix_full_path):
    sound_mix = unreal.EditorAssetLibrary.load_asset(mix_full_path)
    log("reusing existing {}".format(mix_full_path))
else:
    sound_mix = asset_tools.create_asset(MIX_NAME, PAUSE_UI_PATH, unreal.SoundMix, None)
    log("created {}".format(mix_full_path))
unreal.EditorAssetLibrary.save_loaded_asset(sound_mix)

# ---------------------------------------------------------------------------------------------
# 3. Wire the real gameplay GameMode to the new pause-capable controller.
# ---------------------------------------------------------------------------------------------
if not unreal.EditorAssetLibrary.does_asset_exist(GAMEPLAY_GAMEMODE_BP_PATH):
    raise RuntimeError(
        "{} does not exist -- expected the real gameplay GameMode here (created by "
        "AgentScripts/ue_create_gamemode.py). If it has moved/been renamed, update "
        "GAMEPLAY_GAMEMODE_BP_PATH at the top of this script.".format(GAMEPLAY_GAMEMODE_BP_PATH))

gameplay_gm_bp = unreal.EditorAssetLibrary.load_asset(GAMEPLAY_GAMEMODE_BP_PATH)
gameplay_gm_cdo = unreal.get_default_object(gameplay_gm_bp.generated_class())
gameplay_gm_cdo.set_editor_property("player_controller_class", unreal.GameplayPlayerController)
unreal.EditorAssetLibrary.save_loaded_asset(gameplay_gm_bp)
log("BP_DeathMetalCatGameMode PlayerControllerClass = {}".format(
    gameplay_gm_cdo.get_editor_property("player_controller_class")))

log("DONE.")
