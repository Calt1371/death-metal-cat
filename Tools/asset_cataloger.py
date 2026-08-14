#!/usr/bin/env python3
"""
asset_cataloger.py

Asset Cataloger -- standalone agent from the Death Metal Cat GDD (Section 4.2 agent roster).
Identified as the #1-priority gap by Tools/gdd_gap_agent.py (see Tools/gdd_gap_priority.json and
Docs/Assignment5_README.md for the full reasoning): fully specified in the GDD ("Scoped, not yet
built"), has no unmet dependency on another missing system, and reuses this project's own
established measurement patterns.

ROLE (per GDD Section 4.2): scans a folder of biome assets and outputs footprint, alignment,
layer, and role data per asset, so the (still-unbuilt) Room Variation Generator can draw freely
from a growing asset pool rather than a hand-fixed set. Input: asset folder + biome name. Output:
asset_catalog_<Biome>.json.

WHY THIS IS A DIFFERENT MEASUREMENT PROBLEM THAN FOUNDATION EXTRACTOR, NOT A COPY OF IT: Foundation
Extractor measures PLACED ACTORS in a live room -- it has a real world position for every actor,
so it can bucket background/midground/foreground purely from world-Y depth (see its
GAMEPLAY_PLANE_Y_BAND). Asset Cataloger scans raw, UNPLACED assets sitting in the content browser
-- there is no world position to bucket by. Layer and role here are necessarily inferred from
naming convention instead (see classify_role_and_layer() below) -- a real, disclosed limitation,
not a hidden one: a mis-named or unconventionally-named future asset will be classified
"unclassified", not silently guessed at. What DOES carry over directly from Foundation Extractor
is the *shape* of what gets measured per item (bounding-box/footprint dimensions, PaperSprite
scale/color/pivot data) -- the same fields, applied to a raw asset instead of a placed actor.

Same bridge as every other Tools/ agent: build a template string, send it to the running UE5
editor via RemoteExecution, and have the UE-side script write per-asset RAW measurements (class
name, footprint dimensions, pivot/alignment info -- nothing needing string/regex logic) to a JSON
file. Role/layer CLASSIFICATION from the asset's name is deliberately done back in this file, in
compute_catalog_entry()/classify_role_and_layer(), which have no `unreal` import and no other
dependency -- same "plain, importable, independently testable" discipline
foundation_extractor.compute_gaps() already established, so classify_role_and_layer() can be
tested without a running editor at all.

READ-ONLY: the UE-side template only ever calls load_asset()/get_editor_property()-style query
methods. It never renames, moves, or modifies an asset.

Usage:
    python asset_cataloger.py --folder /Game/Environments/CityBiome/Traps --biome AssassinCity
    python asset_cataloger.py --folder /Game/Environments/CityBiome --biome AssassinCity --output Tools/asset_catalog_AssassinCity.json
"""

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(TOOLS_DIR, "..", "AgentScripts"))

from remote_execution import RemoteExecution

# ================================================================================================
# Role/layer classification -- plain Python, no `unreal` import, independently testable. Naming-
# convention keywords drawn directly from this project's own real, already-in-use asset names
# (SP_Room1_BackgroundSkyline, SP_Room1_ForegroundCables, SP_Room4_GroundFloor, T_Trap_*, BP_Trap_*,
# SP_CityBiome_BuildingWideWarehouse, etc.) -- not invented in the abstract.
# ================================================================================================

LAYER_KEYWORDS = {
    "background": ("background", "skyline", "bg"),
    "foreground": ("foreground", "cable"),
}

ROLE_KEYWORDS = [
    # (role, keywords) -- checked in order, first match wins, so more specific roles (hazard) are
    # listed before more general ones (scenery_prop) to avoid e.g. a hypothetical "trap_platform"
    # asset landing in the wrong bucket.
    ("hazard", ("trap",)),
    ("platform", ("floor", "walkway", "platform", "stairwell", "ground", "plank", "tile")),
    ("scenery_prop", (
        "structure", "building", "warehouse", "shack", "complex", "console", "pipe",
        "reactor", "vent", "tower", "archway", "rubble", "debris",
    )),
]

SUPPORTING_ASSET_CLASSES = {
    "Texture2D": "texture_source",
    "Material": "material",
    "MaterialInstanceConstant": "material",
    "MaterialInstance": "material",
}

_TOKEN_RE = re.compile(r"[A-Z][a-z0-9]*|[a-z0-9]+")


def _tokenize_asset_name(asset_name: str) -> set:
    """Splits a PascalCase/underscore compound asset name into lowercase whole-word tokens --
    e.g. 'SP_Room1_BackgroundSkyline' -> {'sp', 'room1', 'background', 'skyline'}. Matching
    keywords against WHOLE tokens (not raw substrings of the full name) is required, not
    cosmetic: 'ground' is a real substring of the raw string 'background', which would wrongly
    tag every background asset as a walkable 'platform' under naive substring search. Splitting
    into real word tokens first and requiring an exact token match avoids that class of false
    positive entirely."""
    return {tok.lower() for tok in _TOKEN_RE.findall(asset_name)}


def classify_role_and_layer(asset_name: str, ue_class: str) -> tuple:
    """Returns (role, layer). Supporting asset types (textures/materials backing a sprite or mesh)
    get a fixed role and no layer -- they're never independently placed, so 'layer' doesn't apply
    to them. Everything else is classified by exact WHOLE-TOKEN match (see _tokenize_asset_name)
    against LAYER_KEYWORDS/ROLE_KEYWORDS. Deliberately returns 'unclassified' rather than a guess
    when nothing matches -- see the module docstring for why a silent wrong guess would be worse
    than an honest 'unclassified' for whatever consumes this catalog next."""
    if ue_class in SUPPORTING_ASSET_CLASSES:
        return SUPPORTING_ASSET_CLASSES[ue_class], None

    tokens = _tokenize_asset_name(asset_name)
    layer = next((layer for layer, kws in LAYER_KEYWORDS.items() if tokens & set(kws)), "midground")
    role = None
    for role_name, kws in ROLE_KEYWORDS:
        if tokens & set(kws):
            role = role_name
            break
    else:
        role = "unclassified"

    # A hazard (trap) or platform reads as gameplay-plane content, not a depth layer, regardless
    # of what LAYER_KEYWORDS guessed from its name -- matches Foundation Extractor's own
    # gameplay-plane-vs-background/foreground distinction (GAMEPLAY_PLANE_Y_BAND), just inferred
    # from role instead of a live Y-position this unplaced asset doesn't have.
    if role in ("hazard", "platform"):
        layer = "gameplay_plane"
    # Conversely: an asset with no ROLE_KEYWORDS hit but a clear background/foreground LAYER hit
    # (e.g. BackgroundSkyline, ForegroundCables) is real, distinct, non-walkable depth dressing --
    # 'unclassified' would understate what's actually known about it here, so it gets its own role
    # rather than falling into the same bucket as a genuinely unrecognized future asset name.
    elif role == "unclassified" and layer in ("background", "foreground"):
        role = "atmosphere_art"

    return role, layer


def compute_catalog_entry(raw: dict) -> dict:
    """Takes one RAW per-asset measurement dict from the UE-side query (path, class, and whatever
    footprint/alignment fields that class supports) and adds the role/layer classification. Plain
    Python, no `unreal` import -- testable standalone, same discipline as
    foundation_extractor.compute_gaps()."""
    role, layer = classify_role_and_layer(raw["name"], raw["class"])
    return {**raw, "role": role, "layer": layer}


# ================================================================================================
# Live UE query -- same RemoteExecution bridge pattern as every other Tools/ agent.
# ================================================================================================

_CATALOG_TEMPLATE = """
import json
import unreal

folder = "__FOLDER__"
output_path = r"__OUTPUT_PATH__"

asset_paths = unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False)
raw_entries = []

for asset_path in sorted(asset_paths):
    obj = unreal.EditorAssetLibrary.load_asset(asset_path)
    if obj is None:
        raw_entries.append({"path": asset_path, "name": asset_path.rsplit("/", 1)[-1].split(".")[0],
                             "class": "UNLOADABLE", "footprint": None, "alignment": None, "notes": "load_asset returned None"})
        continue

    ue_class = obj.get_class().get_name()
    name = asset_path.rsplit("/", 1)[-1].split(".")[0]
    entry = {"path": asset_path, "name": name, "class": ue_class, "footprint": None, "alignment": None, "notes": None}

    if ue_class == "PaperSprite":
        dim = obj.get_editor_property("source_dimension")
        entry["footprint"] = {"width": dim.x, "height": dim.y}
        pivot_mode = obj.get_editor_property("pivot_mode")
        pivot_info = {"pivot_mode": str(pivot_mode)}
        if str(pivot_mode) == "SpritePivotMode.CUSTOM":
            cp = obj.get_editor_property("custom_pivot_point")
            pivot_info["custom_pivot_point"] = {"x": cp.x, "y": cp.y}
        entry["alignment"] = pivot_info
        sprite_comp_color = obj.get_editor_property("default_material") is not None
        entry["notes"] = "has_default_material=" + str(sprite_comp_color)

    elif ue_class == "Texture2D":
        entry["footprint"] = {"width": obj.blueprint_get_size_x(), "height": obj.blueprint_get_size_y()}
        entry["alignment"] = None
        entry["notes"] = "textures have no placement alignment -- backing asset only"

    elif ue_class == "StaticMesh":
        box = obj.get_bounding_box()
        extent = {"x": box.max.x - box.min.x, "y": box.max.y - box.min.y, "z": box.max.z - box.min.z}
        entry["footprint"] = {"width": extent["x"], "depth": extent["y"], "height": extent["z"]}
        entry["alignment"] = {"bounds_min": {"x": box.min.x, "y": box.min.y, "z": box.min.z},
                               "bounds_max": {"x": box.max.x, "y": box.max.y, "z": box.max.z}}

    elif ue_class == "PaperFlipbook":
        key_frames = obj.get_editor_property("key_frames")
        entry["notes"] = "frame_count=" + str(len(key_frames)) + " fps=" + str(obj.get_editor_property("frames_per_second"))
        if key_frames:
            first_sprite = key_frames[0].get_editor_property("sprite")
            if first_sprite is not None:
                dim = first_sprite.get_editor_property("source_dimension")
                entry["footprint"] = {"width": dim.x, "height": dim.y}
        entry["alignment"] = {"per_frame": "see the flipbook's own sprites for per-frame pivot -- not duplicated here"}

    elif ue_class == "Blueprint":
        gen_class = obj.generated_class()
        entry["notes"] = "generated_class=" + (gen_class.get_path_name() if gen_class else "None")
        entry["footprint"] = None
        entry["alignment"] = None

    elif ue_class in ("Material", "MaterialInstanceConstant", "MaterialInstance"):
        entry["notes"] = "supporting material asset -- no footprint/alignment"

    else:
        entry["notes"] = "unhandled asset class -- recorded with class name only, no footprint/alignment extracted"

    raw_entries.append(entry)

with open(output_path, "w", encoding="utf-8") as f:
    json.dump(raw_entries, f, indent=2)

unreal.log_warning("[ASSET CATALOGER] scanned " + folder + ": " + str(len(raw_entries)) + " asset(s) -> " + output_path)
"""


def build_catalog_command(folder: str, output_path: str) -> str:
    return _CATALOG_TEMPLATE.replace("__FOLDER__", folder).replace("__OUTPUT_PATH__", output_path)


def query_raw_assets(folder: str, timeout: float) -> list:
    """Live-queries every asset under `folder` from the currently-open editor, returning RAW
    per-asset measurement dicts (no role/layer classification yet -- see compute_catalog_entry()).
    Same RemoteExecution bridge pattern as every other Tools/ script."""
    temp_fd, temp_output_path = tempfile.mkstemp(suffix=".json", prefix="asset_cataloger_raw_")
    os.close(temp_fd)
    script_body = build_catalog_command(folder, temp_output_path)

    temp_fd, temp_script_path = tempfile.mkstemp(suffix=".py", prefix="asset_cataloger_")
    with os.fdopen(temp_fd, "w", encoding="utf-8") as f:
        f.write(script_body)

    remote_exec = RemoteExecution()
    remote_exec.start()
    try:
        waited = 0.0
        poll_interval = 0.25
        while not remote_exec.remote_nodes and waited < timeout:
            time.sleep(poll_interval)
            waited += poll_interval
        if not remote_exec.remote_nodes:
            raise RuntimeError("No UE5 editor instance found. Is the editor running with Remote Execution enabled?")

        node_id = remote_exec.remote_nodes[0]["node_id"]
        remote_exec.open_command_connection(node_id)
        command = f"exec(open(r'{temp_script_path}').read())"
        result = remote_exec.run_command(command, unattended=True, exec_mode="ExecuteStatement")
        remote_exec.close_command_connection()
    finally:
        remote_exec.stop()
        os.remove(temp_script_path)

    if not result.get("success"):
        raise RuntimeError(f"editor reported failure cataloging {folder}:\n{result}")
    for entry in result.get("output", []):
        print(f"[UE5] {entry.get('type')}: {entry.get('output')}")
        if entry.get("type") == "Error":
            raise RuntimeError(f"asset catalog query for {folder} reported at least one error above")

    if not os.path.exists(temp_output_path):
        raise RuntimeError(f"{temp_output_path} does not exist after a reported-successful run")
    with open(temp_output_path, "r", encoding="utf-8") as f:
        raw_entries = json.load(f)
    os.remove(temp_output_path)
    return raw_entries


def catalog(folder: str, biome: str, output_path: str, timeout: float) -> bool:
    print(f"Cataloging assets under {folder} ...")
    try:
        raw_entries = query_raw_assets(folder, timeout)
    except RuntimeError as exc:
        print(f"ERROR: {exc}")
        return False

    entries = [compute_catalog_entry(raw) for raw in raw_entries]

    role_counts: dict = {}
    for e in entries:
        role_counts[e["role"]] = role_counts.get(e["role"], 0) + 1

    catalog_data = {
        "biome": biome,
        "source_folder": folder,
        "asset_count": len(entries),
        "role_counts": role_counts,
        "assets": entries,
    }

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(catalog_data, f, indent=2)

    with open(output_path, "rb") as f:
        file_bytes = f.read()
    file_hash = hashlib.sha256(file_bytes).hexdigest()
    print(f"Wrote {output_path}: {len(entries)} asset(s), {len(file_bytes)} bytes, sha256={file_hash}")
    print(f"  role breakdown: {role_counts}")
    unclassified = [e["name"] for e in entries if e["role"] == "unclassified"]
    if unclassified:
        print(f"  {len(unclassified)} asset(s) could not be classified by naming convention (reported honestly, not guessed): {unclassified}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Asset Cataloger -- scans a folder of biome assets and outputs footprint, alignment, layer, and role data per asset."
    )
    parser.add_argument("--folder", required=True, help="Content-browser folder to scan, e.g. /Game/Environments/CityBiome/Traps")
    parser.add_argument("--biome", required=True, help="Biome name to stamp into the output, e.g. AssassinCity")
    parser.add_argument("--output", help="Output JSON path. Defaults to Tools/asset_catalog_<biome>.json")
    parser.add_argument("--timeout", type=float, default=5.0, help="Seconds to wait for the editor to respond to discovery.")
    args = parser.parse_args()

    output_path = args.output or os.path.join(TOOLS_DIR, f"asset_catalog_{args.biome}.json")
    ok = catalog(args.folder, args.biome, output_path, args.timeout)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
