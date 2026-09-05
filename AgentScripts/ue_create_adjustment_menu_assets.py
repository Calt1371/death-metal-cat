"""
ue_create_adjustment_menu_assets.py

Builds every asset the Adjustment Menu screen needs, then wires them together. Idempotent --
existing assets are reused/re-configured rather than duplicated, so it is safe to re-run.

Creates:
    /Game/UI/AdjustmentMenu/T_AdjustmentMenu   standard UI texture, same settings as
                                                ue_import_coming_soon.py/ue_create_pause_screen_assets.py.
                                                Source is Adjustment_Menu.png.
    /Game/Audio/SC_SFX                         USoundClass -- every SFX SoundWave's Sound Class
                                                Object, see UDMCGameInstance::ApplyStartupSettings.
    /Game/Audio/SC_Music                       USoundClass -- DMC_Music's Sound Class Object.
    /Game/Audio/MIX_SFXVolume                  USoundMix -- SC_SFX's live class-mix override.
    /Game/Audio/MIX_MusicVolume                USoundMix -- SC_Music's live class-mix override.
    /Game/Maps/L_AdjustmentMenu                the new entry-point map, GameMode override set to
                                                the raw C++ AAdjustmentMenuGameMode (no Blueprint
                                                wrapper -- see that class's own comment for why).

Also:
    - Sets every existing SFX SoundWave's (jump_launch, dodge_handspring, sword_swing, sword_hit,
      gun_fire, gun_hit, enemy_hit_reaction, enemy_death, player_death_sting, level_up_chime)
      Sound Class Object to SC_SFX.
    - Sets DMC_Music's Sound Class Object to SC_Music.
    - Points Config/DefaultEngine.ini's GameDefaultMap at L_AdjustmentMenu (was L_TitleScreen) --
      this screen is now the actual first thing that loads.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\.claude\\worktrees\\adjustment-menu\\AgentScripts\\ue_create_adjustment_menu_assets.py').read())"
"""

import os

import unreal

AUDIO_PATH = "/Game/Audio"
UI_PATH = "/Game/UI/AdjustmentMenu"
MAPS_PATH = "/Game/Maps"

TEX_NAME = "T_AdjustmentMenu"
SOURCE_PNG_FILENAME = "Adjustment_Menu.png"

SFX_SOUND_CLASS_NAME = "SC_SFX"
MUSIC_SOUND_CLASS_NAME = "SC_Music"
SFX_MIX_NAME = "MIX_SFXVolume"
MUSIC_MIX_NAME = "MIX_MusicVolume"

LEVEL_NAME = "L_AdjustmentMenu"

SFX_SOUNDWAVE_PATHS = [
    "/Game/Audio/SFX/jump_launch",
    "/Game/Audio/SFX/dodge_handspring",
    "/Game/Audio/SFX/sword_swing",
    "/Game/Audio/SFX/sword_hit",
    "/Game/Audio/SFX/gun_fire",
    "/Game/Audio/SFX/gun_hit",
    "/Game/Audio/SFX/enemy_hit_reaction",
    "/Game/Audio/SFX/enemy_death",
    "/Game/Audio/SFX/player_death_sting",
    "/Game/Audio/SFX/level_up_chime",
]
MUSIC_SOUNDWAVE_PATH = "/Game/Audio/Music/DMC_Music"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
content_dir = os.path.join(project_dir, "Content")


def log(msg):
    unreal.log("[adjustment-menu] {}".format(msg))


# Resolve relative to whichever checkout/worktree the running editor actually has open, falling
# back to the main checkout -- same reasoning as ue_create_pause_screen_assets.py's own source
# lookup: a worktree may not have every loose source file the main checkout does.
_candidates = [
    os.path.join(content_dir, SOURCE_PNG_FILENAME),
    os.path.join(r"C:\Users\calvi\Desktop\Projects\PythonTest", "Content", SOURCE_PNG_FILENAME),
]
TEX_SRC_PATH = next((c for c in _candidates if os.path.isfile(c)), None)
if TEX_SRC_PATH is None:
    raise RuntimeError("Could not find {} in any of: {}".format(SOURCE_PNG_FILENAME, _candidates))
log("using source PNG: {}".format(TEX_SRC_PATH))


# ---------------------------------------------------------------------------------------------
# 1. Background texture
# ---------------------------------------------------------------------------------------------
task = unreal.AssetImportTask()
task.filename = TEX_SRC_PATH
task.destination_path = UI_PATH
task.destination_name = TEX_NAME
task.automated = True
task.save = True
task.replace_existing = True
task.factory = unreal.TextureFactory()
asset_tools.import_asset_tasks([task])

tex_full_path = "{}/{}".format(UI_PATH, TEX_NAME)
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
# 2. SoundClasses + SoundMixes -- created blank, exactly like MIX_MasterVolume in
# ue_create_pause_screen_assets.py; the actual volume override is applied entirely at runtime by
# UDMCGameInstance::ApplyStartupSettings via SetSoundMixClassOverride.
# ---------------------------------------------------------------------------------------------
def make_blank_asset(name, package_path, asset_class):
    full_path = "{}/{}".format(package_path, name)
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        log("reusing existing {}".format(full_path))
        return unreal.EditorAssetLibrary.load_asset(full_path)
    asset = asset_tools.create_asset(name, package_path, asset_class, None)
    if asset is None:
        raise RuntimeError("create_asset returned None for {}".format(full_path))
    log("created {}".format(full_path))
    return asset


sc_sfx = make_blank_asset(SFX_SOUND_CLASS_NAME, AUDIO_PATH, unreal.SoundClass)
sc_music = make_blank_asset(MUSIC_SOUND_CLASS_NAME, AUDIO_PATH, unreal.SoundClass)
mix_sfx = make_blank_asset(SFX_MIX_NAME, AUDIO_PATH, unreal.SoundMix)
mix_music = make_blank_asset(MUSIC_MIX_NAME, AUDIO_PATH, unreal.SoundMix)

for asset in (sc_sfx, sc_music, mix_sfx, mix_music):
    unreal.EditorAssetLibrary.save_loaded_asset(asset)


# ---------------------------------------------------------------------------------------------
# 3. Point every existing SFX SoundWave + DMC_Music at their new SoundClasses.
# ---------------------------------------------------------------------------------------------
for path in SFX_SOUNDWAVE_PATHS:
    wave = unreal.EditorAssetLibrary.load_asset(path)
    if wave is None:
        log("WARNING: SFX SoundWave not found, skipping: {}".format(path))
        continue
    wave.set_editor_property("sound_class_object", sc_sfx)
    wave.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(wave)
    log("{} SoundClassObject <- {}".format(path, SFX_SOUND_CLASS_NAME))

music_wave = unreal.EditorAssetLibrary.load_asset(MUSIC_SOUNDWAVE_PATH)
if music_wave is None:
    log("WARNING: DMC_Music not found at {} -- expected ue_import_level_music.py to have run already.".format(MUSIC_SOUNDWAVE_PATH))
else:
    music_wave.set_editor_property("sound_class_object", sc_music)
    music_wave.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(music_wave)
    log("{} SoundClassObject <- {}".format(MUSIC_SOUNDWAVE_PATH, MUSIC_SOUND_CLASS_NAME))


# ---------------------------------------------------------------------------------------------
# 4. The level itself, GameMode override pointed straight at the raw C++ AAdjustmentMenuGameMode
# (no Blueprint wrapper -- see that class's comment for why: it's assigned by its raw class the
# same way AGameplayPlayerController is assigned directly to BP_DeathMetalCatGameMode).
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
world_settings.set_editor_property("default_game_mode", unreal.AdjustmentMenuGameMode)
log("world settings default_game_mode = {}".format(unreal.AdjustmentMenuGameMode))

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing_starts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)
if not existing_starts:
    actor_subsystem.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 100.0))
    log("placed a PlayerStart at the origin")

level_editor.save_current_level()
log("level saved.")


# ---------------------------------------------------------------------------------------------
# 5. GameDefaultMap -> L_AdjustmentMenu (was L_TitleScreen). This screen is now the actual first
# thing that loads on launch.
# ---------------------------------------------------------------------------------------------
ini_path = os.path.join(project_dir, "Config", "DefaultEngine.ini")
with open(ini_path, "r", encoding="utf-8") as f:
    ini_lines = f.readlines()

OLD_LINE_PREFIX = "GameDefaultMap="
NEW_VALUE = "/Game/Maps/{}".format(LEVEL_NAME)
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
