"""
update_room_spacing.py

Recomputes each RoomShell's X origin dynamically from the ACTUAL footprint of its generated room
geometry (Tools/room_geometry_<RoomID>.json, produced by Tools/room_geometry_designer.py),
replacing the original room-progression build's fixed 1500-unit placeholder spacing. Chain rule:
each room's origin = previous room's origin + previous room's footprint + BUFFER. Room4A/Room4B
both start at the same X (right after Room3's footprint); Room5's origin is anchored to
whichever branch's footprint is LARGER (+ BUFFER), so there's no overlap regardless of which
branch a given playthrough took. Room1 keeps its existing origin (matches PlayerStart) as the
anchor -- there's no "previous room" to compute it from.

The footprint math itself runs in this repo's normal Tools/ Python environment beforehand (NOT
inside this script) via room_geometry_designer.compute_room_footprint, since the editor's
embedded Python doesn't have the `anthropic` package installed and importing that module here
would fail -- this script only ever receives the already-computed origin numbers as literals.

Only X changes -- Y/Z stay exactly as originally set (same gameplay plane as before). Moving
each RoomShell's world location drags its already-attached children (floor, exit triggers) along
with it automatically, since Unreal attachment preserves each child's RELATIVE offset to its
parent once attached.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\update_room_spacing.py').read())"
"""

import unreal

# Computed from Tools/room_geometry_<RoomID>.json footprints, BUFFER=300 -- see
# Tools/room_geometry_designer.py's compute_room_footprint for the exact formula these came from.
NEW_ORIGINS = {
    'ROOM1': -200.0,
    'ROOM2': 1630.0,
    'ROOM3': 3270.0,
    'ROOM4A': 5200.0,
    'ROOM4B': 5200.0,
    'ROOM5': 7430.0,
    'ROOM6': 9610.0,
    'ROOM7': 12150.0,
    'ROOM8': 14830.0,
}

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()
shells = {a.get_actor_label(): a for a in all_actors if isinstance(a, unreal.RoomShell)}

for room_name, new_x in NEW_ORIGINS.items():
    label = f'RoomShell_{room_name}'
    shell = shells.get(label)
    if shell is None:
        unreal.log_error(f'[ROOM SPACING] {label} not found in the level -- skipped')
        continue

    old_loc = shell.get_actor_location()
    # modify() BEFORE the transform change is required -- set_actor_location() alone does not
    # mark this actor's external-actor package dirty (confirmed empirically: without this, the
    # in-memory transform updates fine and save_current_level() reports success, but the on-disk
    # package is byte-identical to before, i.e. the change silently never persists).
    shell.modify()
    new_loc = unreal.Vector(new_x, old_loc.y, old_loc.z)
    shell.set_actor_location(new_loc, False, True)
    unreal.log_warning(f'[ROOM SPACING] {label}: X {old_loc.x:.1f} -> {new_x:.1f} (Y={old_loc.y:.1f}, Z={old_loc.z:.1f} unchanged)')

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()
unreal.log_warning('[ROOM SPACING] Level saved')
