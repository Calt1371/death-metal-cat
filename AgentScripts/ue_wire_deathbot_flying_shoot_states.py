"""
ue_wire_deathbot_flying_shoot_states.py

Splits the newly-imported 25-frame FB_Enemy_DeathBotFlying_Shoot (raise -> flash-cluster -> return,
from DeathbotFlying-deathbothoverfire-v1.png) into the two sub-flipbooks the C++ Drawing/Firing
state machine actually expects, since it assumes a short non-looping windup + a looping fire cycle
where burst-shot-count == loop frame count -- a 25-frame monolithic asset doesn't fit that (would
fire 25 rapid shots and restart the raise animation mid-motion for the loop phase).

Muzzle-flash frames identified via brightness analysis of the raw sheet (RGB channel thresholding
per cell, not assumed): frames 12, 14, 15, 17, 18, 20 show a real bright flash; the frames between
them (13, 16, 19) don't. Frames 1-11 are the raise motion, 21-25 a return-to-neutral tail the state
machine has no hook to use (same limitation as the old Walking setup -- ShootPhase goes straight
back to None when the burst ends).

Creates two new flipbooks re-using the ALREADY-IMPORTED sprites (no re-import/re-slice needed):
  FB_Enemy_DeathBotFlying_ShootDraw  -- frames 1-11 (raise), non-looping (set by BeginRangedAttack)
  FB_Enemy_DeathBotFlying_ShootLoop  -- frames 12-20 (the 9-frame flash cluster), looping

The existing FB_Enemy_DeathBotFlying_Shoot (all 25 frames) is left untouched as a spare/reference
asset -- nothing currently references it directly by name, so no cleanup needed there.

Also raises BP_EnemyDeathBotFlying's ShootDrawDuration from the base class default (0.15s) to 0.73s
(11 frames @ 15fps) so the raise motion isn't cut off before BeginBurstLoop takes over.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_wire_deathbot_flying_shoot_states.py').read())"
"""

import unreal

SPRITE_DIR = "/Game/Characters/Enemies/DeathBotFlying/Sprites/Shoot"
SPRITE_PREFIX = "SP_Enemy_DeathBotFlying_Shoot"
FLIPBOOK_DEST = "/Game/Characters/Enemies/DeathBotFlying/Flipbooks"
BP_PATH = "/Game/Characters/Enemies/DeathBotFlying/Blueprints/BP_EnemyDeathBotFlying"

FPS = 15.0
DRAW_FRAMES = list(range(1, 12))    # 1-11
LOOP_FRAMES = [12, 14, 15, 17, 18, 20]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def build_flipbook(name, frame_numbers):
    fb_path = FLIPBOOK_DEST + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(fb_path):
        unreal.EditorAssetLibrary.delete_asset(fb_path)

    flipbook = asset_tools.create_asset(name, FLIPBOOK_DEST, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())
    flipbook.set_editor_property("frames_per_second", FPS)

    key_frames = []
    for frame_num in frame_numbers:
        sp_path = f"{SPRITE_DIR}/{SPRITE_PREFIX}_{frame_num:02d}"
        sp_asset = unreal.EditorAssetLibrary.load_asset(sp_path)
        if sp_asset is None:
            raise RuntimeError(f"missing sprite {sp_path}")
        kf = unreal.PaperFlipbookKeyFrame()
        kf.set_editor_property("sprite", sp_asset)
        kf.set_editor_property("frame_run", 1)
        key_frames.append(kf)
    flipbook.set_editor_property("key_frames", key_frames)
    flipbook.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
    unreal.log(f"[WIRE] built {fb_path}: {len(key_frames)} frames {frame_numbers}")
    return flipbook


draw_fb = build_flipbook("FB_Enemy_DeathBotFlying_ShootDraw", DRAW_FRAMES)
loop_fb = build_flipbook("FB_Enemy_DeathBotFlying_ShootLoop", LOOP_FRAMES)

bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())

cdo.set_editor_property("ShootDrawFlipbook", draw_fb)
cdo.set_editor_property("ShootLoopFlipbook", loop_fb)
cdo.set_editor_property("ShootDrawDuration", 0.73)
cdo.modify()

unreal.log(f"ShootDrawFlipbook -> {cdo.get_editor_property('ShootDrawFlipbook')}")
unreal.log(f"ShootLoopFlipbook -> {cdo.get_editor_property('ShootLoopFlipbook')}")
unreal.log(f"ShootDrawDuration -> {cdo.get_editor_property('ShootDrawDuration')}")

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

unreal.log("=== DEATHBOT FLYING SHOOT STATE WIRING COMPLETE ===")
