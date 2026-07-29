"""
ue_place_room1_art.py

Places Room1's imported Meshy art onto its existing greybox geometry, per three treatments:

1. REAL MESH COLLISION (LeftStructure -> idx0, RightStructure -> idx8, confirmed with the user
   before writing this, since neither piece was labeled and getting this wrong would silently
   break validated reachability): deletes the placeholder cube for that piece and spawns the real
   static mesh in its place, scaled in X only to match the piece's width (Y/Z left at native
   scale, per the task's explicit "match X/width" wording), positioned so the mesh's own
   (unscaled) top-of-bounds lands exactly at the piece's existing validated top_z. Reports actual
   post-placement collision-bounds top Z (via get_actor_bounds(only_colliding_components=True),
   the TRUE collision surface, not just visual mesh bounds) against the expected top_z, for the
   requested verification table.

2. 2D SPRITE DRESSING (GroundTile/WalkwayPlank/FloatingPlatform, confirmed piece mapping: idx3
   ledge_step -> FloatingPlatform, idx4 flat_run -> WalkwayPlank, idx1/idx2/idx6/idx7 -> GroundTile
   as the generic remainder): the existing placeholder cube stays as the ACTIVE collision (never
   touched, never deleted) but hidden (SetActorHiddenInGame does not disable collision by
   default, confirmed -- this is exactly the "invisible but still collides" behavior wanted). A
   PaperSpriteActor is placed flush on top: X = piece center, Y = 0 (gameplay plane), scaled
   uniformly to the piece's width (aspect ratio preserved), Z placed so the sprite's BOTTOM edge
   (not its default center pivot) sits at the piece's top_z.

3. BACKGROUND LAYERS (BackgroundSkyline/BackgroundMidgroundCity, pure decoration, no collision):
   large PaperSpriteActors at Y=1500 / Y=600 respectively, centered on Room1's real X midpoint,
   scaled to comfortably exceed the room's real footprint width. This part is a rough first pass,
   not rigorously verified the way the 3D collision swaps are -- flagged as needing visual
   eyeballing in-editor, since there's no way to confirm camera-frustum coverage from the Python
   bridge alone.

Everything is attached to RoomShell_ROOM1, same as the existing floors/triggers.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_place_room1_art.py').read())"
"""

import unreal

DEST_ENV = "/Game/Environments/CityBiome/Room1"
ROOM_ID_NAME = "ROOM1"

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()

room_shell = next((a for a in all_actors if isinstance(a, unreal.RoomShell) and a.get_actor_label() == "RoomShell_" + ROOM_ID_NAME), None)
if room_shell is None:
    raise RuntimeError("[PLACE ART] RoomShell_" + ROOM_ID_NAME + " not found")

# Idempotent re-run for Parts 2/3 ONLY: clean up any dressing/background actors from a previous
# (possibly failed) pass -- these failed-attach actors aren't attached to RoomShell_ROOM1, so
# search by label across ALL level actors, not just the shell's attached list. Deliberately does
# NOT touch "Structure_" actors -- Part 1 consumes/deletes the original placeholder floor as a
# one-way "replace", so once it has succeeded (as it already has, verified delta=0.0 for both),
# there is nothing left to redo it FROM; re-running this cleanup against Structure_ actors would
# destroy an already-correct swap with no way to recreate it in the same pass.
removed_count = 0
for a in list(all_actors):
    label = a.get_actor_label()
    if label.startswith("Dressing_" + ROOM_ID_NAME) or label.startswith("Background_"):
        actor_subsystem.destroy_actor(a)
        removed_count += 1
unreal.log_warning("[PLACE ART] Removed " + str(removed_count) + " actor(s) from a previous pass")

# Re-fetch since destroy_actor above may have invalidated cached actor lists.
all_actors = actor_subsystem.get_all_level_actors()
room_shell = next((a for a in all_actors if isinstance(a, unreal.RoomShell) and a.get_actor_label() == "RoomShell_" + ROOM_ID_NAME), None)
attached = room_shell.get_attached_actors(False, True)
floors_by_label = {a.get_actor_label(): a for a in attached if a.get_actor_label().startswith("Floor_" + ROOM_ID_NAME)}


def attach_to_shell(actor):
    ok = actor.attach_to_actor(room_shell, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    if not ok:
        unreal.log_error("[PLACE ART] Failed to attach " + actor.get_actor_label())


# =========================================================================================
# PART 1 -- REAL MESH COLLISION SWAPS
# =========================================================================================

STRUCTURE_SWAPS = [
    # (floor_label, mesh_path, actor_label)
    ("Floor_ROOM1_00_FlatRun", DEST_ENV + "/room1_left_structure_3d_0727201107_image-to-3d-texture/StaticMeshes/SM_Room1_LeftStructure", "Structure_ROOM1_00_LeftStructure"),
    ("Floor_ROOM1_08_FlatRun", DEST_ENV + "/room1_right_structure_3d_0727204446_image-to-3d-texture/StaticMeshes/SM_Room1_RightStructure", "Structure_ROOM1_08_RightStructure"),
]

verification_rows = []

for floor_label, mesh_path, new_label in STRUCTURE_SWAPS:
    old_floor = floors_by_label.get(floor_label)
    if old_floor is None:
        unreal.log_error("[PLACE ART] " + floor_label + " not found -- skipping this swap")
        continue

    old_loc = old_floor.get_actor_location()
    old_extent = old_floor.get_actor_bounds(False)[1]
    piece_width = old_extent.x * 2.0
    piece_x = old_loc.x
    piece_top_z = old_loc.z + old_extent.z  # expected/validated walkable top, per Room Geometry Designer

    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        unreal.log_error("[PLACE ART] mesh not found: " + mesh_path)
        continue

    bounds = mesh.get_bounds()
    native_width = bounds.box_extent.x * 2.0
    local_top = bounds.origin.z + bounds.box_extent.z  # native (unscaled) top, local space

    scale_x = piece_width / native_width
    target_z = piece_top_z - local_top  # so that (unscaled Z) local_top ends up exactly at piece_top_z

    # Delete the old placeholder cube -- "replace", not duplicate/stack collision.
    actor_subsystem.destroy_actor(old_floor)

    new_actor = actor_subsystem.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(piece_x, old_loc.y, target_z))
    new_actor.set_actor_label(new_label)
    new_actor.static_mesh_component.set_static_mesh(mesh)
    new_actor.set_actor_scale3d(unreal.Vector(scale_x, 1.0, 1.0))
    new_actor.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    attach_to_shell(new_actor)

    # Verification: the ACTUAL collision surface after placement, not the planned number.
    coll_origin, coll_extent = new_actor.get_actor_bounds(True)
    actual_top_z = coll_origin.z + coll_extent.z
    delta = actual_top_z - piece_top_z

    verification_rows.append({
        "piece": new_label,
        "expected_top_z": piece_top_z,
        "actual_top_z": actual_top_z,
        "delta": delta,
        "scale_x": scale_x,
    })
    unreal.log_warning(
        "[PLACE ART] " + new_label + ": scale_x=" + str(round(scale_x, 4))
        + " placed_at_z=" + str(round(target_z, 2))
        + " expected_top_z=" + str(round(piece_top_z, 2))
        + " actual_collision_top_z=" + str(round(actual_top_z, 2))
        + " delta=" + str(round(delta, 2))
    )


# =========================================================================================
# PART 2 -- 2D SPRITE DRESSING
# =========================================================================================

SPRITE_DRESSING = [
    # (floor_label, sprite_name)
    ("Floor_ROOM1_01_EnemyArena", "SP_Room1_GroundTile"),
    ("Floor_ROOM1_02_FlatRun", "SP_Room1_GroundTile"),
    ("Floor_ROOM1_03_LedgeStep", "SP_Room1_FloatingPlatform"),
    ("Floor_ROOM1_04_FlatRun", "SP_Room1_WalkwayPlank"),
    ("Floor_ROOM1_06_FlatRun", "SP_Room1_GroundTile"),
    ("Floor_ROOM1_07_EnemyArena", "SP_Room1_GroundTile"),
]

for floor_label, sprite_name in SPRITE_DRESSING:
    floor_actor = floors_by_label.get(floor_label)
    if floor_actor is None:
        unreal.log_error("[PLACE ART] " + floor_label + " not found -- skipping dressing")
        continue

    # Invisible but still collides -- SetActorHiddenInGame alone does not disable collision.
    floor_actor.set_actor_hidden_in_game(True)

    loc = floor_actor.get_actor_location()
    extent = floor_actor.get_actor_bounds(False)[1]
    piece_width = extent.x * 2.0
    piece_top_z = loc.z + extent.z

    sprite = unreal.EditorAssetLibrary.load_asset(DEST_ENV + "/" + sprite_name)
    if sprite is None:
        unreal.log_error("[PLACE ART] sprite not found: " + sprite_name)
        continue

    dim = sprite.get_editor_property("source_dimension")
    scale = piece_width / dim.x
    scaled_height = dim.y * scale
    sprite_z = piece_top_z + (scaled_height / 2.0)  # default center pivot -> bottom edge flush at top_z

    sprite_actor = actor_subsystem.spawn_actor_from_class(unreal.PaperSpriteActor, unreal.Vector(loc.x, 0.0, sprite_z))
    sprite_actor.set_actor_label("Dressing_" + floor_label.replace("Floor_", "") + "_" + sprite_name.replace("SP_Room1_", ""))
    sprite_actor.render_component.set_sprite(sprite)
    sprite_actor.render_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sprite_actor.set_actor_scale3d(unreal.Vector(scale, 1.0, scale))
    attach_to_shell(sprite_actor)

    unreal.log_warning("[PLACE ART] Dressed " + floor_label + " with " + sprite_name + " (scale=" + str(round(scale, 4)) + ", z=" + str(round(sprite_z, 2)) + "), collision unchanged/hidden")


# =========================================================================================
# PART 3 -- BACKGROUND LAYERS (rough first pass, not rigorously verified -- see docstring)
# =========================================================================================

room_shell_loc = room_shell.get_actor_location()
# Room1's real footprint (see Tools/room_geometry_Room1.json / compute_room_footprint): 1575u.
ROOM1_FOOTPRINT = 1575.0
room_mid_x = room_shell_loc.x + ROOM1_FOOTPRINT / 2.0

BACKGROUNDS = [
    # (sprite_name, y_depth, label)
    ("SP_Room1_BackgroundSkyline", 1500.0, "Background_Skyline"),
    ("SP_Room1_BackgroundMidgroundCity", 600.0, "Background_MidgroundCity"),
]

for sprite_name, y_depth, label in BACKGROUNDS:
    sprite = unreal.EditorAssetLibrary.load_asset(DEST_ENV + "/" + sprite_name)
    if sprite is None:
        unreal.log_error("[PLACE ART] background sprite not found: " + sprite_name)
        continue
    dim = sprite.get_editor_property("source_dimension")
    target_width = ROOM1_FOOTPRINT * 1.3  # comfortable margin beyond the room's own width
    scale = target_width / dim.x

    bg_actor = actor_subsystem.spawn_actor_from_class(unreal.PaperSpriteActor, unreal.Vector(room_mid_x, y_depth, 100.0))
    bg_actor.set_actor_label(label)
    bg_actor.render_component.set_sprite(sprite)
    bg_actor.render_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    bg_actor.set_actor_scale3d(unreal.Vector(scale, 1.0, scale))
    attach_to_shell(bg_actor)
    unreal.log_warning("[PLACE ART] Placed " + label + " at x=" + str(round(room_mid_x, 1)) + " y=" + str(y_depth) + " scale=" + str(round(scale, 3)) + " -- ROUGH PASS, eyeball in-editor")


# =========================================================================================
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.save_current_level()

unreal.log_warning("=== VERIFICATION TABLE (3D structure collision swaps) ===")
for row in verification_rows:
    flag = "FLAG (>5u delta)" if abs(row["delta"]) > 5.0 else "ok"
    unreal.log_warning(
        "  " + row["piece"] + ": expected_top_z=" + str(round(row["expected_top_z"], 2))
        + " actual_top_z=" + str(round(row["actual_top_z"], 2))
        + " delta=" + str(round(row["delta"], 2))
        + " -- " + flag
    )
