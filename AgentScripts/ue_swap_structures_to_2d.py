"""
ue_swap_structures_to_2d.py

Two things, discovered/decided in this order:

1. BUG FOUND FIRST: Structure_ROOM1_00_LeftStructure / Structure_ROOM1_08_RightStructure (the 3D
   meshes that replaced idx0/idx8's original placeholder floors) had been manually repositioned
   far outside Room1's own footprint (huge scale, negative Y) -- clearly repurposed as background
   set-dressing, not the walkable idx0/idx8 floor anymore. Since the original 3D-swap deleted
   (not hid) idx0/idx8's placeholder collision cubes when they were first replaced, and nothing
   else was ever placed at their original position, Room1 currently has a real hole in the floor
   at both its very start (idx0) and right before the Room1->Room2 exit trigger (idx8) -- confirmed
   via a live query showing NOTHING left at x=-100 or x=1300. Fixed here by recreating the
   original invisible collision cubes at the exact validated position/top_z (same formula as
   import_room_geometry.py's place_floor: floor_center_z = top_z - FLOOR_THICKNESS/2), same
   "hidden but collides" treatment as the GroundTile/WalkwayPlank/FloatingPlatform dressing.

2. THE ACTUAL REQUEST: swap the two now-repositioned 3D structure meshes for their 2D sprite
   equivalents, preserving the CURRENT (user-adjusted) position and real-world visual size --
   not the literal Scale3D number, which doesn't carry over meaningfully between a mesh and a
   sprite with completely different native dimensions. Computed as
   scale = current_visual_full_size / sprite_native_pixel_dimension, independently for X and Z.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_swap_structures_to_2d.py').read())"
"""

import unreal

DEST_ENV = "/Game/Environments/CityBiome/Room1"
CAPSULE_HALF_HEIGHT = 88.0
FLOOR_THICKNESS = 20.0

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()
room_shell = next((a for a in all_actors if isinstance(a, unreal.RoomShell) and a.get_actor_label() == "RoomShell_ROOM1"), None)
if room_shell is None:
    raise RuntimeError("[SWAP] RoomShell_ROOM1 not found")


def attach_to_shell(actor):
    ok = actor.attach_to_actor(room_shell, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    if not ok:
        unreal.log_error("[SWAP] Failed to attach " + actor.get_actor_label())


# =========================================================================================
# STEP 1 -- restore the missing collision at idx0/idx8 (confirmed hole, see docstring)
# =========================================================================================

cube_mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")


def restore_floor_collision(x_center, top_z, width, label):
    floor_center_z = top_z - (FLOOR_THICKNESS / 2.0)  # matches import_room_geometry.py's place_floor formula exactly
    floor = actor_subsystem.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x_center, 0.0, floor_center_z))
    floor.set_actor_label(label)
    floor.static_mesh_component.set_static_mesh(cube_mesh)
    floor.set_actor_scale3d(unreal.Vector(width / 100.0, 3.0, 0.2))
    floor.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    floor.set_actor_hidden_in_game(True)
    attach_to_shell(floor)

    coll_origin, coll_extent = floor.get_actor_bounds(True)
    actual_top = coll_origin.z + coll_extent.z
    unreal.log_warning(
        "[RESTORE COLLISION] " + label + ": expected_top_z=" + str(top_z)
        + " actual_top_z=" + str(actual_top) + " delta=" + str(actual_top - top_z)
    )


restore_floor_collision(-100.0, 4.000099999997474, 200.0, "Floor_ROOM1_00_FlatRun")
restore_floor_collision(1300.0, 84.00009999999747, 150.0, "Floor_ROOM1_08_FlatRun")


# =========================================================================================
# STEP 2 -- swap the 3D structures for 2D sprites at their CURRENT position/visual size
# =========================================================================================

SWAPS = [
    ("Structure_ROOM1_00_LeftStructure", "SP_Room1_LeftStructure"),
    ("Structure_ROOM1_08_RightStructure", "SP_Room1_RightStructure"),
]

for old_label, sprite_name in SWAPS:
    old_actor = next((a for a in all_actors if a.get_actor_label() == old_label), None)
    if old_actor is None:
        unreal.log_error("[SWAP] " + old_label + " not found")
        continue

    loc = old_actor.get_actor_location()
    vis_origin, vis_extent = old_actor.get_actor_bounds(False)
    old_full_size = unreal.Vector(vis_extent.x * 2.0, vis_extent.y * 2.0, vis_extent.z * 2.0)

    sprite = unreal.EditorAssetLibrary.load_asset(DEST_ENV + "/" + sprite_name)
    if sprite is None:
        unreal.log_error("[SWAP] sprite not found: " + sprite_name)
        continue
    dim = sprite.get_editor_property("source_dimension")

    scale_x = old_full_size.x / dim.x
    scale_z = old_full_size.z / dim.y

    actor_subsystem.destroy_actor(old_actor)

    new_actor = actor_subsystem.spawn_actor_from_class(unreal.PaperSpriteActor, loc)
    new_actor.set_actor_label(old_label)
    new_actor.render_component.set_sprite(sprite)
    new_actor.render_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    new_actor.set_actor_scale3d(unreal.Vector(scale_x, 1.0, scale_z))
    attach_to_shell(new_actor)

    new_vis_origin, new_vis_extent = new_actor.get_actor_bounds(False)
    new_full_size = unreal.Vector(new_vis_extent.x * 2.0, new_vis_extent.y * 2.0, new_vis_extent.z * 2.0)
    unreal.log_warning(
        "[SWAP] " + old_label + " -> 2D sprite: old_full_size=(" + str(round(old_full_size.x, 1)) + "," + str(round(old_full_size.y, 1)) + "," + str(round(old_full_size.z, 1)) + ")"
        + " new_full_size=(" + str(round(new_full_size.x, 1)) + "," + str(round(new_full_size.y, 1)) + "," + str(round(new_full_size.z, 1)) + ")"
        + " scale=(" + str(round(scale_x, 4)) + ",1.0," + str(round(scale_z, 4)) + ")"
    )

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()
unreal.log_warning("[SWAP] DONE")
