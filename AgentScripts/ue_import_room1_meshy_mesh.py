"""
ue_import_room1_meshy_mesh.py

Imports one of Room1's 3D building meshes (RawAssets/Meshy_Room1/room1_<side>_structure_3d_....glb,
Meshy's image-to-3D output) as a Static Mesh, then checks whether it came in with any usable
collision. If not, generates convex-hull collision so Cayde can actually walk on it later rather
than just visually overlapping it.

GLB IMPORT: the legacy unreal.AssetImportTask()/AssetTools route has no registered factory for
.glb in this project (confirmed -- import silently fails, no asset created) because
unreal.InterchangeManager.is_interchange_active() is False here (Interchange is not this
project's default/active import pipeline -- a project-wide settings toggle, not something this
one-off import should flip). However, the engine's built-in InterchangeGLTFTranslator is still
directly invokable: can_translate_source_data()/get_translator_for_source_data() both confirm it
resolves correctly regardless of the global "active" flag. So this imports via
unreal.InterchangeManager.import_asset() directly (synchronous, confirmed signature via
import_asset.__doc__ in the running editor) rather than the legacy AssetImportTask path.

Interchange also nests the result under <DEST>/<source_scene_name>/<AssetTypeFolder>/<Name>
rather than the flat <DEST>/<Name> passed in -- confirmed against the actual returned path for
LeftStructure, not assumed -- so this finds the StaticMesh among whatever import_asset returns
instead of guessing a fixed path.

COLLISION: checked LeftStructure came in with 1 usable convex hull already (Meshy/glTF's own
export apparently carries collision sometimes) -- confirmed via BodySetup.agg_geom's
convex/box/sphere/sphyl element counts, not assumed. If a mesh has none, generates convex-hull
collision via the non-deprecated unreal.StaticMeshEditorSubsystem.set_convex_decomposition_collisions
(confirmed exact name/signature live in the running editor, in favor of the deprecated
EditorStaticMeshLibrary equivalent) -- hull_count=4, max_hull_verts=16, hull_precision=100000,
placeholder values, tune freely.

Does NOT place anything in the level -- asset-registry-only import.

Invoke via (edit SRC/NAME at the bottom for each new structure):
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_room1_meshy_mesh.py').read())"
"""

import unreal


def import_and_check_mesh(src: str, dest: str, name: str):
    full_path_hint = dest + "/" + name

    mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
    source_data = unreal.InterchangeManager.create_source_data(src)

    params = unreal.ImportAssetParameters()
    params.is_automated = True
    params.replace_existing = True
    params.destination_name = name

    imported_objects = mgr.import_asset(dest, source_data, params)
    unreal.log_warning("[MESH IMPORT] " + name + ": import_asset returned " + str(len(imported_objects)) + " object(s):")
    for obj in imported_objects:
        unreal.log_warning("  " + obj.get_path_name() + "  class=" + obj.get_class().get_name())

    mesh = next((obj for obj in imported_objects if isinstance(obj, unreal.StaticMesh)), None)
    if mesh is None:
        unreal.log_error("[MESH IMPORT] " + name + ": FAILED -- import_asset returned no StaticMesh object.")
        return

    real_path = mesh.get_path_name()
    unreal.log("[MESH IMPORT] " + name + ": OK -- " + real_path)

    body_setup = mesh.get_editor_property("body_setup")
    if body_setup is None:
        unreal.log_warning("[MESH IMPORT] " + name + ": No BodySetup at all on this mesh.")
        return

    agg_geom = body_setup.get_editor_property("agg_geom")
    convex_elems = agg_geom.get_editor_property("convex_elems")
    box_elems = agg_geom.get_editor_property("box_elems")
    sphere_elems = agg_geom.get_editor_property("sphere_elems")
    sphyl_elems = agg_geom.get_editor_property("sphyl_elems")
    collision_trace_flag = body_setup.get_editor_property("collision_trace_flag")

    total_simple = len(convex_elems) + len(box_elems) + len(sphere_elems) + len(sphyl_elems)
    unreal.log_warning(
        "[MESH IMPORT] " + name + ": Collision check (as imported): convex=" + str(len(convex_elems))
        + " box=" + str(len(box_elems))
        + " sphere=" + str(len(sphere_elems))
        + " sphyl=" + str(len(sphyl_elems))
        + " collision_trace_flag=" + str(collision_trace_flag)
    )

    if total_simple == 0:
        unreal.log_warning("[MESH IMPORT] " + name + ": No usable simple collision came in -- generating convex hull collision now.")
        subsys = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        ok = subsys.set_convex_decomposition_collisions(mesh, hull_count=4, max_hull_verts=16, hull_precision=100000)
        new_convex_count = subsys.get_convex_collision_count(mesh)
        unreal.log_warning("[MESH IMPORT] " + name + ": set_convex_decomposition_collisions returned " + str(ok) + ", new convex hull count=" + str(new_convex_count))
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    else:
        unreal.log_warning("[MESH IMPORT] " + name + ": Usable simple collision already present -- no generation needed.")

    num_triangles = mesh.get_num_triangles(0) if hasattr(mesh, "get_num_triangles") else "unknown"
    unreal.log_warning("[MESH IMPORT] " + name + ": LOD0 triangle count: " + str(num_triangles))

    # Report the actual material/texture set Interchange pulled in alongside the mesh, for the summary.
    materials = mesh.get_editor_property("static_materials") if mesh.get_class().get_name() != "StaticMesh" else None
    unreal.log_warning("[MESH IMPORT] " + name + ": DONE")


DEST = "/Game/Environments/CityBiome/Room1"

# Edit these two lines per structure being imported.
SRC = r"C:\Users\calvi\Desktop\Projects\PythonTest\RawAssets\Meshy_Room1\room1_right_structure_3d_0727204446_image-to-3d-texture.glb"
NAME = "SM_Room1_RightStructure"

import_and_check_mesh(SRC, DEST, NAME)
