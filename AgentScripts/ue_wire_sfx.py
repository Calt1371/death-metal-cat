"""
ue_wire_sfx.py

Imports the 10 SFX sourced by sfx_curator_agent.py (real, non-dry-run pass -- see
sfx_output/manifest.json) as USoundWave assets into /Game/Audio/SFX, then assigns them to
the SFX properties added to DeathMetalCatCharacter.h / DeathMetalCatEnemyBase.h and wired
into gameplay via UGameplayStatics::PlaySound2D / PlaySoundAtLocation.

Source files are .wav (converted from the Freesound .mp3 previews -- Unreal's built-in
SoundFactory only imports .wav) at C:\\Users\\calvi\\sfx_output\\wav\\.

Two actions came back NO_MATCH_FOUND from the curator agent (player_damage_impact,
gnarly_rank_tierup -- every candidate failed the brief, e.g. drum loops instead of a short
power-up riser) and are deliberately left unset here. PlayerHurtSound / GnarlyRankTierUpSound
stay null; PlaySound2D/PlaySoundAtLocation are null-safe, so those two moments simply stay
silent until better source material is found -- no crash, no placeholder sound forced in.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\.claude\\worktrees\\coming-soon\\AgentScripts\\ue_wire_sfx.py').read())"
"""

import os
import unreal

WAV_DIR = r"C:\Users\calvi\sfx_output\wav"
IMPORT_DEST = "/Game/Audio/SFX"

PLAYER_BP = "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat"
ENEMY_BPS = [
    "/Game/Characters/Enemies/DeathBotCrawler/Blueprints/BP_EnemyDeathBotCrawler",
    "/Game/Characters/Enemies/DeathBotFlying/Blueprints/BP_EnemyDeathBotFlying",
    "/Game/Characters/Enemies/DeathBotHeavy/Blueprints/BP_EnemyDeathBotHeavy",
    "/Game/Characters/Enemies/DeathBotWalking/Blueprints/BP_EnemyDeathBotWalking",
]

# action_id -> source wav filename (from the real curator-agent run)
SOUND_FILES = {
    "jump_launch": "jump_launch__389590.wav",
    "dodge_handspring": "dodge_handspring__389590.wav",
    "sword_swing": "sword_swing__733889.wav",
    "sword_hit": "sword_hit__160413.wav",
    "gun_fire": "gun_fire__163456.wav",
    "gun_hit": "gun_hit__259563.wav",
    "enemy_hit_reaction": "enemy_hit_reaction__406562.wav",
    "enemy_death": "enemy_death__562198.wav",
    "player_death_sting": "player_death_sting__471190.wav",
    "level_up_chime": "level_up_chime__853286.wav",
}


def import_sound(action_id, filename):
    dest_path = f"{IMPORT_DEST}/{action_id}"
    existing = unreal.EditorAssetLibrary.load_asset(dest_path)
    if existing:
        unreal.log(f"  {action_id}: already imported, reusing {dest_path}")
        return existing

    src = os.path.join(WAV_DIR, filename)
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = IMPORT_DEST
    task.destination_name = action_id
    task.automated = True
    task.save = True
    task.replace_existing = True

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    sound = unreal.EditorAssetLibrary.load_asset(dest_path)
    if sound is None:
        unreal.log_error(f"  {action_id}: IMPORT FAILED ({src})")
    else:
        unreal.log(f"  {action_id}: imported {src} -> {dest_path}")
    return sound


def set_sound(bp_path, prop_name, sound_asset, label):
    if sound_asset is None:
        unreal.log_warning(f"  SKIP {label}: no sound asset for {bp_path}.{prop_name}")
        return False
    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    if bp is None:
        unreal.log_error(f"  Blueprint not found: {bp_path}")
        return False
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property(prop_name, sound_asset)
    cdo.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)
    unreal.log(f"  {label}: {bp_path} .{prop_name} <- {sound_asset.get_name()}")
    return True


unreal.log("=== IMPORTING SFX (10 assets -> /Game/Audio/SFX) ===")
imported = {action_id: import_sound(action_id, filename) for action_id, filename in SOUND_FILES.items()}

unreal.log("=== WIRING PLAYER (BP_DeathMetalCat) ===")
set_sound(PLAYER_BP, "jump_sound", imported["jump_launch"], "Jump")
set_sound(PLAYER_BP, "dodge_sound", imported["dodge_handspring"], "Dodge")
set_sound(PLAYER_BP, "sword_swing_sound", imported["sword_swing"], "SwordSwing")
set_sound(PLAYER_BP, "sword_hit_sound", imported["sword_hit"], "SwordHit")
set_sound(PLAYER_BP, "gun_fire_sound", imported["gun_fire"], "GunFire")
set_sound(PLAYER_BP, "gun_hit_sound", imported["gun_hit"], "GunHit")
set_sound(PLAYER_BP, "player_death_sound", imported["player_death_sting"], "PlayerDeath")
set_sound(PLAYER_BP, "level_up_sound", imported["level_up_chime"], "LevelUp")

unreal.log("=== SKIPPED (curator agent found no license-clean match -- left null, safe no-op) ===")
unreal.log("  PlayerHurtSound  (action: player_damage_impact)")
unreal.log("  GnarlyRankTierUpSound  (action: gnarly_rank_tierup)")

unreal.log("=== WIRING ENEMIES (4 DeathBot variants) ===")
for enemy_bp in ENEMY_BPS:
    set_sound(enemy_bp, "enemy_hit_sound", imported["enemy_hit_reaction"], "EnemyHit")
    set_sound(enemy_bp, "enemy_death_sound", imported["enemy_death"], "EnemyDeath")

unreal.log("=== SFX WIRING COMPLETE (8/10 player+shared sounds assigned, 2 left silent pending better source material) ===")
