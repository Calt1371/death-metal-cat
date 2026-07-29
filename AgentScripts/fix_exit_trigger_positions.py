"""
fix_exit_trigger_positions.py

Repositions every ARoomExitTrigger and the ABiomeEndMarker to sit at the TRUE far edge of each
room's real, Room-Geometry-Designer-generated footprint, minus a 100-unit buffer -- fixing a bug
where every trigger was still sitting at its original hardcoded offset from
build_room_progression.py (shell_x + 500, or +450/+650 for Room3's pair), calibrated to the old
flat 1200-unit placeholder floors and never updated when the dynamic footprint-based spacing
replaced them. That left every trigger sitting well inside its room's real geometry instead of
at the actual exit point -- worse in bigger/later rooms, confirmed via direct position query
before writing this fix (see conversation).

New positions computed from Tools/room_geometry_<RoomID>.json footprints (same data source as
update_room_spacing.py) + the already-corrected RoomShell origins:
    geometry_far_edge = room_origin + footprint
    new_trigger_x     = geometry_far_edge - 100   (the same 100-unit buffer the original
                                                    triggers had relative to the OLD flat floors:
                                                    floor spanned [origin-600, origin+600],
                                                    trigger sat at origin+500)

Room3's two branch triggers are the one exception -- NOT collapsed to the same X, so walking
forward still reaches the 4A trigger first, same as before:
    ExitTrigger_ROOM3_to_ROOM4A -> Room3_far_edge - 100
    ExitTrigger_ROOM3_to_ROOM4B -> Room3_far_edge - 100 + 200  (200-unit stagger preserved,
                                                                 computed off the real far edge)

Only X changes -- Y/Z stay exactly as originally set. modify() is called before each transform
change (set_actor_location() alone does not mark an OFPA external-actor package dirty -- see
update_room_spacing.py's own docstring for the same lesson, confirmed the hard way there).

UPDATED 2026-07-23 (second pass): after the jump-tuning rescale (17 gap/ledge pieces resized in
Tools/room_geometry_<RoomID>.json, see conversation) and re-running import_room_geometry.py
--all-rooms, several rooms' real far edges moved because some GAP widths grew (gaps count toward
compute_room_footprint(); ledge_step height_up changes don't, since only length feeds the
footprint sum). RoomShell origins were NOT recomputed (import_room_geometry.py rebuilds geometry
from each shell's EXISTING location, it doesn't reposition shells), so 6 of the 10
triggers/markers drifted back inside their room's widened geometry -- confirmed via direct query
of live RoomShell locations + recomputed footprints from the rescaled JSON, not assumed. The 4
that didn't move (ROOM4B_to_ROOM5, ROOM6_to_ROOM7, ROOM7_to_ROOM8, BiomeEndMarker_Room8) belonged to
rooms whose gaps weren't touched by the rescale, so were omitted from that pass.

UPDATED 2026-07-23 (third pass): user manually repositioned all 10 triggers/markers in-editor
(observed platforms disappearing before they'd even jumped off them into the next room), pushing
every one PAST its room's far edge instead of the intended 100-unit-before buffer -- by as little
as +40 (ROOM1_to_ROOM2) up to +670 past far edge for ROOM6_to_ROOM7 specifically (which landed 370
units inside Room7's own RoomShell origin, a clear outlier vs. every other trigger's much smaller
overshoot). Confirmed via direct query against the second-pass values, not assumed. Reverted here
back to the last known-correct geometry_far_edge - 100 values (the second-pass numbers above) for
all 10, since a trigger sitting past a room's far edge risks needing solid ground that may not
exist there if room deactivation ever hides that room's floor -- the actual disappearing-platforms
cause is still unconfirmed and worth investigating separately, this revert just undoes the
symptom-chasing workaround so the geometry math stays correct while that gets looked into.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\fix_exit_trigger_positions.py').read())"
"""

import unreal

# Reverted to geometry_far_edge - 100 for all 10 (see third-pass note above) -- the last
# known-correct values from the second pass, undoing the user's manual overshoot.
NEW_POSITIONS = {
    'ExitTrigger_ROOM1_to_ROOM2': 1275.0,
    'ExitTrigger_ROOM2_to_ROOM3': 2915.0,
    'ExitTrigger_ROOM3_to_ROOM4A': 4920.0,
    'ExitTrigger_ROOM3_to_ROOM4B': 5120.0,
    'ExitTrigger_ROOM4A_to_ROOM5': 7180.0,
    'ExitTrigger_ROOM4B_to_ROOM5': 6340.0,
    'ExitTrigger_ROOM5_to_ROOM6': 9360.0,
    'ExitTrigger_ROOM6_to_ROOM7': 11750.0,
    'ExitTrigger_ROOM7_to_ROOM8': 14430.0,
    'BiomeEndMarker_Room8': 17300.0,
}

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()
by_label = {a.get_actor_label(): a for a in all_actors if isinstance(a, unreal.RoomExitTrigger) or isinstance(a, unreal.BiomeEndMarker)}

for label, new_x in NEW_POSITIONS.items():
    actor = by_label.get(label)
    if actor is None:
        unreal.log_error('[TRIGGER FIX] ' + label + ' not found in the level -- skipped')
        continue

    old_loc = actor.get_actor_location()
    actor.modify()
    new_loc = unreal.Vector(new_x, old_loc.y, old_loc.z)
    actor.set_actor_location(new_loc, False, True)
    unreal.log_warning('[TRIGGER FIX] ' + label + ': X ' + str(old_loc.x) + ' -> ' + str(new_x) + ' (Y=' + str(old_loc.y) + ', Z=' + str(old_loc.z) + ' unchanged)')

dirty_before_save = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
unreal.log_warning('[TRIGGER FIX] dirty map packages before save: ' + str(len(dirty_before_save)))

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()

dirty_after_save = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
unreal.log_warning('[TRIGGER FIX] dirty map packages after save: ' + str(len(dirty_after_save)))
unreal.log_warning('[TRIGGER FIX] DONE')
