#!/usr/bin/env python3
"""
foundation_extractor.py

Foundation Extractor -- measures a finished "golden" room and writes a biome_spec_<Biome>.json
contract for downstream room-generation agents to consume (same consumer relationship
room_geometry_designer.py has to its own hand-authored constants, except here every spacing
number is pulled live from the room itself and from the character/enemy Blueprints' CDOs at
run time -- see the CONSTANTS section below for why that distinction matters).

Same bridge as spawn_encounter_actors.py / import_room_geometry.py: build a python template
string, send it to the running UE5 editor via RemoteExecution, and have the UE-side script write
the JSON directly with open()/json.dump() (the editor process has full filesystem access, so
there's no need to round-trip the data back through stdout).

READ-ONLY: the UE-side template only ever calls get_*/is_a-style query methods and
unreal.get_default_object() (CDO property reads). It never calls modify(), set_actor_location(),
spawn_actor_from_class(), or save_current_level() -- this agent measures Room1, it never edits it.

WHY LIVE-QUERY AND NOT REUSE room_geometry_designer.py'S CONSTANTS: a query run in this same
project confirmed BP_DeathMetalCat's CDO no longer matches its own C++ defaults -- live
JumpZVelocity read 773.22, not the 708.652466 room_geometry_designer.py has hardcoded (and that
708.652466 was itself already a corrected value, previously 600 -- i.e. this number has drifted
at least twice via Blueprint-side tuning that never gets pushed back into any Python constant).
Hardcoding it a third time would just be the same staleness bug again. Querying the CDO fresh
every run makes this agent immune to that drift by construction.

SHOOT_RANGE / RANGED-ATTACK PROPERTIES: deliberately not queried. Recorded as
{"not_measured": true} wherever they'd appear, per explicit instruction -- this avoids a whole
class of stale-compiled-Blueprint failure this project has hit more than once (a property that
exists in C++ source but not yet on the actually-running compiled CDO raises an AttributeError-
style exception rather than returning a sane value), and that recompile-state question is out of
scope for a room-measurement tool.

FLOOR CONVENTIONS (depth_scale_y, depth_offset_from_background, dressing): confirmed directly with
the room's designer that a floor's world-Y depth scale and its Y-distance from the background art
are fixed conventions, not incidental placement -- "it always has to be this [value] to keep a
consistent feel." Also captures which sprites dress each floor (e.g. a top-down tile sitting flush
on the floor's top surface, paired with a front-facing sprite at positive Y for the 2D side view),
detected purely by X-span overlap with the floor's bounds -- not by name -- so this generalizes to
any biome's floor+tile convention, not just this one asset pair.

USAGE:
    python foundation_extractor.py --room Room1 --biome AssassinCity
"""

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import time

# NOTE: python-docx is imported lazily inside read_gdd(), not here at module level -- this module
# must stay importable from environments that don't have it installed: UE5's embedded Python
# (which the extraction template below imports this module INTO, to reuse compute_gaps() rather
# than duplicating it) and Tools/reachability_verifier.py's own environment both only ever need
# compute_gaps()/the movement-constants helpers, never read_gdd() itself.

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(TOOLS_DIR, "..", "AgentScripts"))

from remote_execution import RemoteExecution

DOCS_DIR = os.path.join(TOOLS_DIR, "..", "Docs")
GDD_PATH = os.path.join(DOCS_DIR, "Death_Metal_Cat_GDD_v3.docx")
CHARACTER_HEADER_PATH = os.path.join(TOOLS_DIR, "..", "Source", "PythonTest", "DeathMetalCatCharacter.h")
ROOM_TYPES_HEADER_PATH = os.path.join(TOOLS_DIR, "..", "Source", "PythonTest", "RoomTypes.h")

# Tolerance for the live-CDO-vs-C++-header-defaults comparison below. RELATIVE (fraction of the
# C++ default), not absolute units -- an earlier version of this check used a flat absolute
# tolerance (0.01 units), which is meaningless once values live in the hundreds: WallJumpForceVertical
# was flagged as a "mismatch" at a 0.76-unit delta on a 700-unit baseline (0.11% relative -- this
# project has separately, previously characterized this exact property/delta as float round-trip
# noise through the editor's property system, not real tuning), while a genuinely real
# JumpZVelocity retune (708.65 -> 773.22, ~9.1% relative) needs to still trip the halt. 1% sits
# comfortably above observed noise (~0.11%) and well below observed real tuning deltas (~9.1%).
MOVEMENT_CONSTANT_MISMATCH_TOLERANCE_FRACTION = 0.01

# name in the comparison report -> (C++ header member name, live CDO query key already computed
# by the existing extraction template's movement_constants dict). Only the three properties the
# GDD's own text (Section 4.3) names as having drifted before, plus the other CDO-tunable movement
# properties this script already queries live -- NOT max_walk_speed/gravity_z, which live on the
# engine's CharacterMovementComponent/PhysicsSettings rather than as a UPROPERTY default on
# ADeathMetalCatCharacter itself, so there's no single C++ "default" line for those to diff against.
CPP_VS_LIVE_CONSTANT_MAP = {
    "JumpZVelocity": "jump_z_velocity",
    "WallJumpForceHorizontal": "wall_jump_force_horizontal",
    "WallJumpForceVertical": "wall_jump_force_vertical",
    "DodgeImpulseStrength": "dodge_impulse_strength",
    "DodgeDuration": "dodge_duration",
}
# AirControl and WallJumpCommitmentDuration are both real UPROPERTY defaults on the character (and
# both named explicitly in the GDD's drift history), but the existing extraction template does not
# currently query either live -- recorded here as a known gap rather than silently left out.
CPP_ONLY_CONSTANTS_NOT_YET_LIVE_QUERIED = ["AirControl", "WallJumpCommitmentDuration"]

CHARACTER_BP_PATH = "/Game/Characters/DeathMetalCat/Blueprints/BP_DeathMetalCat"

# Layer-bucketing threshold (world Y = the project's depth axis -- gameplay itself happens on the
# X (horizontal) / Z (vertical) plane at Y=0; this is NOT world Z, which is up/down movement, not
# depth). Measured in Room1: gameplay-plane actors (floors/triggers/structures) cluster at
# -70..0uu, the next-nearest real content sits at +215 (foreground) and -458..-860 (background) --
# a band of +-100uu safely contains the near-zero cluster (the two Structure actors are only 20uu
# apart from EACH OTHER at -50/-70, so a band that split them, e.g. +-50, would incorrectly put
# one in background and the other in midground) while staying well clear of the real background/
# foreground content on both sides. Re-check this band against each new room's actual measured
# spread rather than assuming it transfers unchanged.
GAMEPLAY_PLANE_Y_BAND = 100.0

# Reachability tolerance: a gap counts "tight" if it uses more than this fraction of whichever
# traversal method's max range clears it -- i.e. a jump that only just makes a gap, not one with
# comfortable room to spare.
TIGHT_TOLERANCE_FRACTION = 0.85

# Nearest-K-neighbor graph: each platform connects to its N closest OTHER platforms by straight-
# line (X, Z) distance -- an approximation of the room's normal traversal connections, not full
# pathfinding. Exactly this (K=2) and the original distance-only tolerance margin, after a broader
# "all pairs within a generous radius" version was tried and rejected: it surfaced
# OneWayPlatform11->OneWayPlatform15 (a real, deliberately hard reference jump -- see
# REFERENCE_GAP_LABEL_PAIRS below) but ALSO changed/added a large number of other gaps that were
# never meant to be a direct connection, which the room's designer explicitly did not want -- only
# the one confirmed reference jump should be added, nothing else should move.
NUM_NEAREST_NEIGHBORS = 2

# Confirmed directly with the room's designer: this is ONE deliberately hard, low-margin jump,
# included on purpose as a calibration reference for "how tight can a gap be and still be
# reachable" -- NOT the nearest-neighbor pairing for either platform (by construction: a
# deliberately risky long jump is never anyone's closest neighbor), so the nearest-K-neighbor graph
# above would otherwise silently drop it. Explicitly named rather than inferred, and additive only
# -- it must not change any other gap's membership or values.
REFERENCE_GAP_LABEL_PAIRS = [("OneWayPlatform11", "OneWayPlatform15")]


def compute_gaps(
    platform_data,
    max_jump_distance,
    max_jump_height,
    wall_jump_max_distance,
    wall_jump_max_height,
    dodge_distance,
    num_nearest_neighbors=NUM_NEAREST_NEIGHBORS,
    reference_gap_label_pairs=None,
    tight_tolerance_fraction=TIGHT_TOLERANCE_FRACTION,
):
    """The gap-computation + reachability-classification math -- extracted here as a plain,
    importable function (no `unreal` import, no other dependency) so it has exactly ONE
    implementation shared by both Foundation Extractor's own golden-room measurement (via the
    embedded UE template below, which imports this module and calls this function) and
    Tools/reachability_verifier.py (which re-runs this SAME check against a generated room's
    platform list). Neither is allowed to reimplement this math a second time.

    platform_data: list of {"label", "left_x", "right_x", "top_z"} dicts -- exactly what the
    embedded template builds from live actor bounds, and exactly what a caller working from a
    generated room's actor list needs to build the same way.

    Returns the same gap list shape Foundation Extractor has always written into
    biome_spec_<Biome>.json's gameplay_spacing.gaps, plus two additive fields (from_label/to_label)
    that were implicit before (only recoverable by matching coordinates back to actors) -- added
    because Reachability Verifier's whole job is reporting PER-GAP pass/fail, which needs a stable
    way to name which gap failed. Nothing in this project reads the gaps field yet (confirmed by
    search), so this is a safe, non-breaking addition, not a schema change against any real
    consumer.
    """
    if reference_gap_label_pairs is None:
        reference_gap_label_pairs = REFERENCE_GAP_LABEL_PAIRS

    def edge_gap(a, b):
        if a["right_x"] <= b["left_x"]:
            x_gap = b["left_x"] - a["right_x"]
        elif b["right_x"] <= a["left_x"]:
            x_gap = a["left_x"] - b["right_x"]
        else:
            x_gap = 0.0  # footprints overlap in X -- directly above/below, not a horizontal gap
        height_up = b["top_z"] - a["top_z"]
        straight_dist = (x_gap ** 2 + height_up ** 2) ** 0.5
        return x_gap, height_up, straight_dist

    edges = {}  # frozenset({i, j}) -> (x_gap, height_up, i, j) -- keeps the smallest x_gap seen per pair
    for i, p in enumerate(platform_data):
        neighbor_dists = []
        for j, q in enumerate(platform_data):
            if i == j:
                continue
            x_gap, height_up, straight_dist = edge_gap(p, q)
            neighbor_dists.append((straight_dist, j, x_gap, height_up))
        neighbor_dists.sort(key=lambda t: t[0])
        for straight_dist, j, x_gap, height_up in neighbor_dists[:num_nearest_neighbors]:
            key = frozenset((i, j))
            if key not in edges or x_gap < edges[key][0]:
                edges[key] = (x_gap, height_up, i, j)

    label_to_index = {p["label"]: idx for idx, p in enumerate(platform_data)}
    for label_a, label_b in reference_gap_label_pairs:
        if label_a not in label_to_index or label_b not in label_to_index:
            continue  # platform not present in this room/run -- skip rather than error
        i, j = label_to_index[label_a], label_to_index[label_b]
        x_gap, height_up, _ = edge_gap(platform_data[i], platform_data[j])
        edges[frozenset((i, j))] = (x_gap, height_up, i, j)

    gaps = []
    for x_gap, height_up, i, j in edges.values():
        if x_gap <= 0:
            continue  # vertically stacked / overlapping footprints -- not a horizontal traversal gap
        p, q = platform_data[i], platform_data[j]
        # Orient from -> to as lower -> higher so height_up is always the climb (never negative).
        if height_up < 0:
            src, dst, height_up = q, p, -height_up
        else:
            src, dst = p, q

        reachable_by = []
        tightest_margin = None
        for method_name, max_dist, max_height in [
            ("jump", max_jump_distance, max_jump_height),
            ("wall_jump", wall_jump_max_distance, wall_jump_max_height),
            ("dodge", dodge_distance, 0.0),
        ]:
            height_ok = height_up <= max_height if height_up > 0 else True
            if x_gap <= max_dist and height_ok:
                reachable_by.append(method_name)
                margin_fraction = x_gap / max_dist if max_dist > 0 else 1.0
                if tightest_margin is None or margin_fraction < tightest_margin:
                    tightest_margin = margin_fraction

        tolerance = "tight" if (tightest_margin is not None and tightest_margin >= tight_tolerance_fraction) else "comfortable"
        if not reachable_by:
            tolerance = "tight"  # unreachable by any measured method -- flag as tight, not silently comfortable

        gaps.append({
            "from_label": src["label"],
            "to_label": dst["label"],
            "from": [src["right_x"] if src["right_x"] <= dst["left_x"] else src["left_x"], src["top_z"]],
            "to": [dst["left_x"] if src["right_x"] <= dst["left_x"] else dst["right_x"], dst["top_z"]],
            "distance": x_gap,
            "reachable_by": reachable_by,
            "tolerance": tolerance,
        })

    return gaps


# ============================================================================================
# GDD ingestion (new) -- reads Death_Metal_Cat_GDD_v3.docx ONCE, ahead of measuring the golden
# room, and halts the whole run if the GDD's real documented movement-constant baseline
# (DeathMetalCatCharacter.h's C++ defaults -- see note below on why the GDD text itself has no
# numbers to compare against) disagrees with what's live on the CDO right now. Everything in this
# section is plain Python, no `unreal` import -- it never needs the editor running.
# ============================================================================================

def get_cpp_documented_defaults(header_path: str) -> dict:
    """Regex-parses DeathMetalCatCharacter.h's UPROPERTY member-initializer defaults -- e.g.
    `float JumpZVelocity = 708.652466f;` -- for every constant in CPP_VS_LIVE_CONSTANT_MAP plus
    CPP_ONLY_CONSTANTS_NOT_YET_LIVE_QUERIED. This is the real "documented C++ default" the GDD's
    own Section 4.3 refers to (JumpZVelocity/AirControl/WallJumpCommitmentDuration "drifted from
    their documented C++ defaults") -- the GDD text itself states no numbers, so this header file
    is the actual documented baseline, not the GDD prose."""
    with open(header_path, "r", encoding="utf-8") as f:
        header_text = f.read()

    all_names = list(CPP_VS_LIVE_CONSTANT_MAP.keys()) + CPP_ONLY_CONSTANTS_NOT_YET_LIVE_QUERIED
    defaults = {}
    for name in all_names:
        match = re.search(rf"\bfloat\s+{re.escape(name)}\s*=\s*([-\d.]+)f?\s*;", header_text)
        if match is None:
            raise ValueError(f"Could not find a `float {name} = ...;` default in {header_path} -- "
                              f"has this property been renamed or removed?")
        defaults[name] = float(match.group(1))
    return defaults


def compare_constants(cpp_defaults: dict, live_values: dict, tolerance_fraction: float) -> list[dict]:
    """Deterministic, non-LLM comparison -- for every constant in CPP_VS_LIVE_CONSTANT_MAP, checks
    the C++ header default against the corresponding live-CDO-queried value using a RELATIVE
    (percentage) tolerance, not an absolute-unit one -- see MOVEMENT_CONSTANT_MISMATCH_TOLERANCE_FRACTION
    for why. Returns a list of mismatch records (empty list = everything matches within tolerance).
    Never trusts either number as automatically correct -- just reports where they disagree so a
    human decides."""
    mismatches = []
    for cpp_name, live_key in CPP_VS_LIVE_CONSTANT_MAP.items():
        cpp_value = cpp_defaults[cpp_name]
        live_value = live_values[live_key]
        delta = abs(cpp_value - live_value)
        relative_delta = delta / abs(cpp_value) if cpp_value != 0 else (0.0 if delta == 0 else float("inf"))
        if relative_delta > tolerance_fraction:
            mismatches.append({
                "constant": cpp_name,
                "cpp_documented_default": cpp_value,
                "live_cdo_value": live_value,
                "delta": delta,
                "relative_delta": relative_delta,
                "tolerance_fraction": tolerance_fraction,
            })
    return mismatches


def parse_room_id_enum(header_path: str) -> list[str]:
    """Regex-parses RoomTypes.h's ERoomID enum values, in declared order -- the real single source
    of truth for room count/branch structure, used to cross-check the GDD's prose claims about room
    roles rather than trusting the prose alone."""
    with open(header_path, "r", encoding="utf-8") as f:
        header_text = f.read()
    match = re.search(r"enum\s+class\s+ERoomID\s*:\s*\w+\s*\{([^}]*)\}", header_text)
    if match is None:
        raise ValueError(f"Could not find `enum class ERoomID` in {header_path}")
    return [v.strip() for v in match.group(1).split(",") if v.strip()]


def read_gdd(gdd_path: str, room_types_header_path: str) -> dict:
    """Reads Death_Metal_Cat_GDD_v3.docx and extracts structured facts for gdd_reference.json:

    - Agent roster: pulled from whichever table has "Agent"/"Input"/"Status" as column headers
      (Table 3 in the current draft) -- read by header name, not by table index, so this survives
      a table being reordered/added in a future draft.
    - Room count/roles: the GDD states this in prose (Section 2.6), cross-checked here against
      RoomTypes.h's real ERoomID enum rather than trusted on its own.
    - Movement constants: the GDD NAMES these (JumpZVelocity, AirControl,
      WallJumpCommitmentDuration, "jump distance", "wall-jump reach", "dodge distance") but --
      confirmed by reading every paragraph and every table cell in this document -- states no
      actual numeric value for any of them anywhere. Recorded honestly as "not documented" rather
      than inventing a number or silently skipping the field. The real numeric comparison this
      agent performs is cpp_documented_defaults vs live_cdo_values (see compare_constants), since
      the GDD's own text (Section 4.3) points to "documented C++ defaults" as the actual baseline.
    """
    import docx  # lazy -- see the note at the top of this module

    d = docx.Document(gdd_path)

    agent_roster = []
    for table in d.tables:
        header = [c.text.strip() for c in table.rows[0].cells]
        if "Agent" in header and "Input" in header and "Status" in header:
            for row in table.rows[1:]:
                cells = [c.text.strip() for c in row.cells]
                agent_roster.append(dict(zip(header, cells)))
            break

    real_room_sequence = parse_room_id_enum(room_types_header_path)
    gdd_documented_sequence = ["Room1", "Room2", "Room3", "Room4A", "Room4B", "Room5", "Room6", "Room7", "Room8"]
    room_roles = {
        "total_shells_documented_in_gdd": 9,
        "traversed_per_playthrough_documented_in_gdd": 8,
        "sequence_documented_in_gdd": "Room1 -> Room2 -> Room3 -> (Room4A or Room4B) -> Room5 -> Room6 -> Room7 -> Room8",
        "branch_rooms": ["Room4A", "Room4B"],
        "reconvergence_room": "Room5",
        "biome_end_room": "Room8",
        "biome_end_status_documented_in_gdd": "placeholder marker; full boss encounter is a stretch goal, not built",
        "real_ERoomID_enum_sequence": real_room_sequence,
        "matches_real_enum": real_room_sequence == gdd_documented_sequence,
    }

    movement_constants_named_in_gdd = {
        "constants_referenced_by_name": [
            "JumpZVelocity", "AirControl", "WallJumpCommitmentDuration",
            "jump distance", "wall-jump reach", "dodge distance",
        ],
        "numeric_values_documented_in_gdd": False,
        "note": (
            "Confirmed by reading every paragraph and table cell in Death_Metal_Cat_GDD_v3.docx: "
            "Section 4.3 narrates that JumpZVelocity, AirControl, and WallJumpCommitmentDuration "
            "previously drifted from their \"documented C++ defaults\" -- it names these constants "
            "but states no numbers itself anywhere in the document. The numeric comparison this "
            "agent actually performs is cpp_documented_defaults (DeathMetalCatCharacter.h) vs "
            "live_cdo_values (queried below), since that's the real documented baseline the GDD's "
            "own text points to."
        ),
    }

    return {
        "gdd_source_file": os.path.basename(gdd_path),
        "room_roles": room_roles,
        "movement_constants_named_in_gdd": movement_constants_named_in_gdd,
        "agent_roster": agent_roster,
    }


# Small, standalone embedded template -- queries ONLY the movement constants (no room/actor
# measurement at all) so the constants-mismatch check below can run, and potentially HALT,
# strictly BEFORE the full golden-room measurement pass (_EXTRACT_TEMPLATE) ever runs. Kept
# deliberately separate from _EXTRACT_TEMPLATE rather than folded into one pass, since "halt before
# measuring" is only possible if the two are two genuinely separate steps.
_LIVE_CONSTANTS_ONLY_TEMPLATE = """
import json
import unreal

character_bp_path = "__CHARACTER_BP_PATH__"
output_path = r"__OUTPUT_PATH__"

char_class = unreal.EditorAssetLibrary.load_blueprint_class(character_bp_path)
if char_class is None:
    raise RuntimeError("[FOUNDATION EXTRACTOR] Failed to load character Blueprint class at " + character_bp_path)
char_cdo = unreal.get_default_object(char_class)
move_comp = char_cdo.get_component_by_class(unreal.CharacterMovementComponent)

live_values = {
    "gravity_z": abs(move_comp.get_gravity_z()),
    "jump_z_velocity": move_comp.get_editor_property("jump_z_velocity"),
    "max_walk_speed": move_comp.get_editor_property("max_walk_speed"),
    "wall_jump_force_horizontal": char_cdo.get_editor_property("wall_jump_force_horizontal"),
    "wall_jump_force_vertical": char_cdo.get_editor_property("wall_jump_force_vertical"),
    "dodge_impulse_strength": char_cdo.get_editor_property("dodge_impulse_strength"),
    "dodge_duration": char_cdo.get_editor_property("dodge_duration"),
}

with open(output_path, "w", encoding="utf-8") as f:
    json.dump(live_values, f, indent=2)

unreal.log_warning("[FOUNDATION EXTRACTOR] live movement constants (pre-check): " + str(live_values))
"""


def build_live_constants_command(output_path: str) -> str:
    return (
        _LIVE_CONSTANTS_ONLY_TEMPLATE
        .replace("__CHARACTER_BP_PATH__", CHARACTER_BP_PATH)
        .replace("__OUTPUT_PATH__", output_path)
    )


def query_live_movement_constants(timeout: float) -> dict:
    """Runs ONLY the lightweight constants query against the live editor and returns the parsed
    result -- no room measurement, no actor queries, so this is safe to run before confirming a
    RoomShell/level is even the right one."""
    temp_fd, temp_output_path = tempfile.mkstemp(suffix=".json", prefix="live_constants_")
    os.close(temp_fd)
    script_body = build_live_constants_command(temp_output_path)

    temp_fd, temp_script_path = tempfile.mkstemp(suffix=".py", prefix="foundation_extractor_constants_")
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
        raise RuntimeError(f"editor reported failure querying live movement constants:\n{result}")
    for entry in result.get("output", []):
        print(f"[UE5] {entry.get('type')}: {entry.get('output')}")
        if entry.get("type") == "Error":
            raise RuntimeError("live movement constants query reported at least one error above")

    if not os.path.exists(temp_output_path):
        raise RuntimeError(f"{temp_output_path} does not exist after a reported-successful run")
    with open(temp_output_path, "r", encoding="utf-8") as f:
        live_values = json.load(f)
    os.remove(temp_output_path)
    return live_values


_EXTRACT_TEMPLATE = """
import hashlib
import json
import unreal

room_id_name = "__ROOM_ID_NAME__"
biome_name = "__BIOME_NAME__"
output_path = r"__OUTPUT_PATH__"
character_bp_path = "__CHARACTER_BP_PATH__"
gameplay_plane_y_band = __GAMEPLAY_PLANE_Y_BAND__
tight_tolerance_fraction = __TIGHT_TOLERANCE_FRACTION__

# ---- Make sure we're measuring the real level, not a blank /Temp/Untitled_1 world ----
editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world = editor_subsystem.get_editor_world()
if "L_ControllerTestRange" not in world.get_name():
    level_subsystem.load_level("/Game/L_ControllerTestRange")
    world = editor_subsystem.get_editor_world()
unreal.log_warning("[FOUNDATION EXTRACTOR] measuring world: " + world.get_name())

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()
room_shell = next(
    (a for a in all_actors if isinstance(a, unreal.RoomShell) and a.get_actor_label() == "RoomShell_" + room_id_name),
    None,
)
if room_shell is None:
    raise RuntimeError("[FOUNDATION EXTRACTOR] No RoomShell_" + room_id_name + " found in the level")

attached = room_shell.get_attached_actors(False, True)
unreal.log_warning("[FOUNDATION EXTRACTOR] " + str(len(attached)) + " actor(s) attached to RoomShell_" + room_id_name)

# ---- LIVE movement constants -- read straight off BP_DeathMetalCat's CDO, never hardcoded ----
char_class = unreal.EditorAssetLibrary.load_blueprint_class(character_bp_path)
if char_class is None:
    raise RuntimeError("[FOUNDATION EXTRACTOR] Failed to load character Blueprint class at " + character_bp_path)
char_cdo = unreal.get_default_object(char_class)
move_comp = char_cdo.get_component_by_class(unreal.CharacterMovementComponent)

gravity_z = abs(move_comp.get_gravity_z())
jump_z_velocity = move_comp.get_editor_property("jump_z_velocity")
max_walk_speed = move_comp.get_editor_property("max_walk_speed")
wall_jump_force_horizontal = char_cdo.get_editor_property("wall_jump_force_horizontal")
wall_jump_force_vertical = char_cdo.get_editor_property("wall_jump_force_vertical")
dodge_impulse_strength = char_cdo.get_editor_property("dodge_impulse_strength")
dodge_duration = char_cdo.get_editor_property("dodge_duration")

max_jump_height = jump_z_velocity ** 2 / (2 * gravity_z)
_standing_airtime = 2 * jump_z_velocity / gravity_z
max_jump_distance = max_walk_speed * _standing_airtime

wall_jump_max_height = wall_jump_force_vertical ** 2 / (2 * gravity_z)
_wall_jump_airtime = 2 * wall_jump_force_vertical / gravity_z
wall_jump_max_distance = wall_jump_force_horizontal * _wall_jump_airtime

dodge_distance = dodge_impulse_strength * dodge_duration

movement_constants = {
    "gravity_z": gravity_z,
    "jump_z_velocity": jump_z_velocity,
    "max_walk_speed": max_walk_speed,
    "max_jump_height": max_jump_height,
    "max_jump_distance": max_jump_distance,
    "wall_jump_force_horizontal": wall_jump_force_horizontal,
    "wall_jump_force_vertical": wall_jump_force_vertical,
    "wall_jump_max_height": wall_jump_max_height,
    "wall_jump_max_distance": wall_jump_max_distance,
    "dodge_impulse_strength": dodge_impulse_strength,
    "dodge_duration": dodge_duration,
    "dodge_distance": dodge_distance,
}
unreal.log_warning("[FOUNDATION EXTRACTOR] live movement constants: " + str(movement_constants))

# ---- LIVE enemy constants -- deliberately skip shoot_range / ranged-attack properties ----
melee_range = None
detection_radius = None
contact_damage = None
shoot_range_status = {"not_measured": True}
enemy_classes_seen = set()
for a in attached:
    if isinstance(a, unreal.DeathMetalCatEnemyBase):
        enemy_classes_seen.add(a.get_class())

for enemy_class in enemy_classes_seen:
    enemy_cdo = unreal.get_default_object(enemy_class)
    melee_range = enemy_cdo.get_editor_property("melee_range")
    detection_radius = enemy_cdo.get_editor_property("detection_radius")
    contact_damage = enemy_cdo.get_editor_property("contact_damage")
    break  # same base-class constants regardless of which enemy subclass is present

# ---- Classify every attached actor ----
# Platform = anything the player can stand on and jump between -- StaticMeshActor (flat-run floor
# pieces) AND OneWayPlatform (the pass-through-from-below platform class used for the room's
# vertical climbing sections). Missing OneWayPlatform here would silently drop the room's actual
# jump layout from gameplay_spacing while still (correctly) listing them under art_placement.
platform_actors = []   # used for gap computation AND appear in art_placement
enemy_actors = []
non_character_non_enemy = []  # everything that goes into art_placement

for a in attached:
    if isinstance(a, unreal.DeathMetalCatCharacter):
        continue  # excluded from both categories -- the player, not room content
    if isinstance(a, unreal.DeathMetalCatEnemyBase):
        enemy_actors.append(a)
        continue  # excluded from art_placement -- see non_character_non_enemy below
    non_character_non_enemy.append(a)
    if isinstance(a, unreal.StaticMeshActor) or isinstance(a, unreal.OneWayPlatform):
        platform_actors.append(a)

def get_bounds(actor):
    origin, extent = actor.get_actor_bounds(False)
    return origin, extent

# ---- Gap computation: gameplay plane = world (X, Z); world Y is depth, handled separately.
# The actual gap-computation/reachability math lives in ONE place, Tools/foundation_extractor.py's
# own compute_gaps() -- imported here rather than duplicated, so Reachability Verifier (which
# calls the exact same function against a generated room's platform list) can never drift out of
# sync with what Foundation Extractor itself uses to measure the golden room. ----
import sys as _sys
_sys.path.insert(0, r"__TOOLS_DIR__")
import foundation_extractor as _fe

platform_data = []
for a in platform_actors:
    origin, extent = get_bounds(a)
    platform_data.append({
        "label": a.get_actor_label(),
        "left_x": origin.x - extent.x,
        "right_x": origin.x + extent.x,
        "top_z": origin.z + extent.z,
    })

gaps = _fe.compute_gaps(
    platform_data,
    max_jump_distance, max_jump_height,
    wall_jump_max_distance, wall_jump_max_height,
    dodge_distance,
    tight_tolerance_fraction=tight_tolerance_fraction,
)

# ---- Enemy spacing (provisional) ----
enemy_spacing = []
for e in enemy_actors:
    e_loc = e.get_actor_location()
    if platform_data:
        nearest_edge_distance = min(
            min(abs(e_loc.x - p["left_x"]), abs(e_loc.x - p["right_x"]))
            for p in platform_data
        )
    else:
        nearest_edge_distance = None
    enemy_spacing.append({
        "spawn_pos": [e_loc.x, e_loc.z],
        "nearest_patrol_edge_distance": nearest_edge_distance,
        "contact_damage_range": melee_range,
        "shoot_range": shoot_range_status,
        "provisional": True,
    })

# ---- Art placement: bucket by world-Y offset from the gameplay plane (Y=0), not by name ----
art_entries = []
floor_entries = []  # StaticMeshActor entries -- same dict objects as in art_entries (by reference),
                     # so fields added to them below also appear in the background/midground/
                     # foreground buckets built from art_entries.
for a in non_character_non_enemy:
    origin, extent = get_bounds(a)
    loc = a.get_actor_location()
    rot = a.get_actor_rotation()
    entry = {
        "name": a.get_actor_label(),
        "class": a.get_class().get_name(),
        "position": [loc.x, loc.y, loc.z],
        "offset_from_plane": loc.y,
        # Confirmed a real, necessary value, not incidental -- SP_Room1_LongWalkwayTopdown needs a
        # 90-degree roll to sit correctly on top of a floor (a top-down-view sprite laid flat isn't
        # just repositioned, it's reoriented). Recorded for every actor, not just known-rotated
        # ones, since a future biome's convention isn't known in advance.
        "rotation": {"pitch": rot.pitch, "yaw": rot.yaw, "roll": rot.roll},
    }
    # Internal-only, stripped before writing -- used for floor_dressing overlap detection below.
    entry["_left_x"] = origin.x - extent.x
    entry["_right_x"] = origin.x + extent.x
    entry["_top_z"] = origin.z + extent.z

    # scale/color_value/color_rgba are the reference baseline for this asset TYPE (e.g. every
    # future SP_Room1_FloatingPlatform instance the Room Variation Generator places elsewhere
    # should match this exactly, not introduce variation) -- visual consistency across generated
    # rooms depends on these staying fixed, so they're recorded per-instance here rather than
    # assumed constant. Only meaningful for PaperSprite actors (StaticMeshActor/RoomExitTrigger
    # entries don't get these fields at all).
    if isinstance(a, unreal.PaperSpriteActor):
        actor_scale = a.get_actor_scale3d()
        entry["scale"] = {"x": actor_scale.x, "y": actor_scale.y, "z": actor_scale.z}

        sprite_comp = a.get_component_by_class(unreal.PaperSpriteComponent)
        sprite_color = sprite_comp.get_editor_property("sprite_color")
        # Full RGBA -- confirmed necessary, not redundant with color_value: some sprites (e.g.
        # Background_MidgroundCity, r=0.021/g=0.006/b=0.006) are NOT neutral gray, so reusing just
        # the V channel with R=G=B loses real tint information and produces a visibly wrong color
        # when a future room reuses this asset type. color_value is kept alongside it (not
        # replaced) since it's still a convenient single-number brightness summary.
        entry["color_rgba"] = {"r": sprite_color.r, "g": sprite_color.g, "b": sprite_color.b, "a": sprite_color.a}
        # HSV Value = max(R, G, B) by definition -- no conversion library needed, and this avoids
        # any uncertainty about a Python-exposed HSV helper's own normalization/gamma assumptions.
        entry["color_value"] = max(sprite_color.r, sprite_color.g, sprite_color.b)

    # depth_scale_y is a fixed CONVENTION, not incidental -- confirmed directly with the room's
    # designer that a floor's Y-scale (world-depth thickness) always has to be this same value to
    # keep a consistent feel, so future room generation should reuse it exactly rather than
    # treating floor depth as a free parameter.
    # width_x on every entry (floor AND every sprite, not just dressing) -- confirmed a floor and
    # its paired top-down/front-dressing sprites are meant to always be the SAME length as each
    # other. Room1's own current measurements aren't a perfect match (FlatRun2/LongWalkway/
    # LongWalkwayTopdown differ by up to ~9%), so this is recorded as real measured data for the
    # Room Variation Generator to compare/enforce going forward, not asserted as already-exact here
    # -- this tool measures, it doesn't correct Room1's own placement.
    entry["width_x"] = extent.x * 2

    if isinstance(a, unreal.StaticMeshActor):
        actor_scale = a.get_actor_scale3d()
        entry["depth_scale_y"] = actor_scale.y
        floor_entries.append(entry)

    art_entries.append(entry)

background, midground, foreground = [], [], []
for entry in art_entries:
    offset = entry["offset_from_plane"]
    if offset < -gameplay_plane_y_band:
        background.append(entry)
    elif offset > gameplay_plane_y_band:
        foreground.append(entry)
    else:
        midground.append(entry)

# ---- Floor-to-background depth offset: also a fixed convention, not incidental -- confirmed
# directly with the room's designer that a floor always has to sit this far (in world-Y depth) from
# the background art "to keep a consistent feel". Measured as the Y-distance from each floor to the
# NEAREST background-layer actor, so future rooms reuse the same separation instead of an arbitrary
# one. ----
for floor in floor_entries:
    if background:
        floor["depth_offset_from_background"] = min(
            abs(floor["offset_from_plane"] - bg["offset_from_plane"]) for bg in background
        )
    else:
        floor["depth_offset_from_background"] = None

# ---- Floor dressing: sprites whose X-footprint overlaps a floor's AND whose position sits close
# to the floor's top surface (Z-proximity) -- e.g. a top-down tile sprite sitting flush on the
# floor's top (Z within FLOOR_DRESSING_Z_TOLERANCE), paired with a second sprite placed in front of
# it (positive Y, foreground) for the 2D side-scrolling view. The Z-proximity check is required, not
# just X-overlap: wide background/foreground art (skyline, structures, cables) also happens to span
# a floor's entire X-range purely because of its own large scale, sitting hundreds to thousands of
# units above/below the floor -- that's incidental overlap, not "dressing the floor", and got
# wrongly included before this check was added. Detected purely by geometry, not by name, so this
# generalizes to any biome's floor+tile convention, not just this one specific asset pair. ----
FLOOR_DRESSING_Z_TOLERANCE = 100.0

def x_overlaps(a_left, a_right, b_left, b_right):
    return a_left < b_right and b_left < a_right

for floor in floor_entries:
    dressing = []
    for entry in art_entries:
        if entry is floor or entry["class"] != "PaperSpriteActor":
            continue
        if abs(entry["position"][2] - floor["_top_z"]) > FLOOR_DRESSING_Z_TOLERANCE:
            continue  # sits nowhere near the floor's surface -- incidental X-overlap only
        if x_overlaps(floor["_left_x"], floor["_right_x"], entry["_left_x"], entry["_right_x"]):
            if entry["offset_from_plane"] > gameplay_plane_y_band:
                layer = "foreground"
            elif entry["offset_from_plane"] < -gameplay_plane_y_band:
                layer = "background"
            else:
                layer = "midground"
            dressing.append({
                "name": entry["name"],
                "layer": layer,
                "y_offset_from_plane": entry["offset_from_plane"],
                "z_offset_from_floor_top": entry["position"][2] - floor["_top_z"],
                # Confirmed necessary: a top-down tile sprite needs a real rotation (e.g. 90-degree
                # roll) to lay flat on the floor -- position/scale alone don't reproduce the look.
                "rotation": entry["rotation"],
                # Confirmed a floor and its dressing sprites are meant to always share the same
                # length -- compare directly against floor["width_x"] (both measured the same way,
                # actor bounds X-extent * 2).
                "width_x": entry["width_x"],
            })
    floor["dressing"] = dressing

# ---- Nearby props: a SEPARATE, broader category from "dressing" above -- sprites whose X-span
# overlaps a floor but do NOT sit flush on its surface (outside FLOOR_DRESSING_Z_TOLERANCE), split
# into two genuinely different real patterns confirmed in Room1: scattered ground-level clutter at
# moderate height (e.g. SP_Room1_CityRubbleDebris, ~36-116uu above the floor, foreground-band Y) and
# wide overhead atmosphere props spanning the whole floor system (e.g. ForeGroundCable, ~1377uu
# above, also foreground-band Y). Deep background art (Y < -gameplay_plane_y_band, e.g. skyline/
# structures) is deliberately excluded here too -- it's real background dressing for the ROOM, not
# for this specific floor, even though it also happens to X-overlap. This does NOT attempt one
# single Z-threshold rule to classify everything (moderate vs. overhead here is descriptive, not a
# hard cutoff) -- the Room Variation Generator has the full z_offset_from_floor_top to interpret. ----
for floor in floor_entries:
    dressed_names = {d["name"] for d in floor["dressing"]}
    nearby_props = []
    for entry in art_entries:
        if entry is floor or entry["class"] != "PaperSpriteActor" or entry["name"] in dressed_names:
            continue
        if entry["offset_from_plane"] < -gameplay_plane_y_band:
            continue  # deep background art -- not a floor-specific prop, even if X-overlapping
        if x_overlaps(floor["_left_x"], floor["_right_x"], entry["_left_x"], entry["_right_x"]):
            nearby_props.append({
                "name": entry["name"],
                "y_offset_from_plane": entry["offset_from_plane"],
                "z_offset_from_floor_top": entry["position"][2] - floor["_top_z"],
                "rotation": entry["rotation"],
                "scale": entry.get("scale"),
                "color_rgba": entry.get("color_rgba"),
            })
    floor["nearby_props"] = nearby_props

# ---- Strip internal-only bounds fields (used only for the overlap detection above) ----
for entry in art_entries:
    entry.pop("_left_x", None)
    entry.pop("_right_x", None)
    entry.pop("_top_z", None)

def depth_range(bucket):
    if not bucket:
        return None
    offsets = [e["offset_from_plane"] for e in bucket]
    return [min(offsets), max(offsets)]

art_placement = {
    "background": background,
    "midground": midground,
    "foreground": foreground,
    "layer_depth_ranges": {
        "background": depth_range(background),
        "midground": depth_range(midground),
        "foreground": depth_range(foreground),
    },
}

biome_spec = {
    "biome": biome_name,
    "source_room": room_id_name,
    "movement_constants": movement_constants,
    "gameplay_spacing": {
        "gaps": gaps,
        "enemy_spacing": enemy_spacing,
    },
    "art_placement": art_placement,
}

with open(output_path, "w", encoding="utf-8") as f:
    json.dump(biome_spec, f, indent=2)

with open(output_path, "rb") as f:
    file_bytes = f.read()
file_hash = hashlib.sha256(file_bytes).hexdigest()

unreal.log_warning("[FOUNDATION EXTRACTOR] wrote " + output_path + " (" + str(len(file_bytes)) + " bytes, sha256=" + file_hash + ")")
unreal.log_warning("[FOUNDATION EXTRACTOR] SUMMARY gaps=" + str(len(gaps)) + " enemies=" + str(len(enemy_spacing)) + " background=" + str(len(background)) + " midground=" + str(len(midground)) + " foreground=" + str(len(foreground)))
"""


def build_extract_command(room_id_name: str, biome_name: str, output_path: str) -> str:
    return (
        _EXTRACT_TEMPLATE
        .replace("__ROOM_ID_NAME__", room_id_name)
        .replace("__BIOME_NAME__", biome_name)
        .replace("__OUTPUT_PATH__", output_path)
        .replace("__CHARACTER_BP_PATH__", CHARACTER_BP_PATH)
        .replace("__GAMEPLAY_PLANE_Y_BAND__", repr(GAMEPLAY_PLANE_Y_BAND))
        .replace("__TIGHT_TOLERANCE_FRACTION__", repr(TIGHT_TOLERANCE_FRACTION))
        .replace("__TOOLS_DIR__", TOOLS_DIR)
    )


def extract(room: str, biome: str, output_path: str, timeout: float) -> bool:
    room_id_name = room.upper()
    script_body = build_extract_command(room_id_name, biome, output_path)

    temp_fd, temp_path = tempfile.mkstemp(suffix=".py", prefix="foundation_extractor_")
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
            print("ERROR: No UE5 editor instance found. Is the editor running with Remote Execution enabled?")
            return False

        node_id = remote_exec.remote_nodes[0]["node_id"]
        remote_exec.open_command_connection(node_id)
        command = f"exec(open(r'{temp_path}').read())"
        result = remote_exec.run_command(command, unattended=True, exec_mode="ExecuteStatement")
        remote_exec.close_command_connection()
    finally:
        remote_exec.stop()
        os.remove(temp_path)

    if not result.get("success"):
        print(f"ERROR: editor reported failure:\n{result}")
        return False

    had_error = False
    for entry in result.get("output", []):
        print(f"[UE5] {entry.get('type')}: {entry.get('output')}")
        if entry.get("type") == "Error":
            had_error = True

    if had_error:
        print("ERROR: extraction reported at least one error above.")
        return False

    # ---- Verify the file was actually written -- via hash, not just "no exception thrown" ----
    if not os.path.exists(output_path):
        print(f"ERROR: {output_path} does not exist after a reported-successful run.")
        return False
    with open(output_path, "rb") as f:
        file_bytes = f.read()
    file_hash = hashlib.sha256(file_bytes).hexdigest()
    print(f"Verified {output_path}: {len(file_bytes)} bytes, sha256={file_hash}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Foundation Extractor -- ingests the GDD, checks it against live-queried movement constants, and (if that passes) measures a finished room into a biome_spec_<Biome>.json contract.")
    parser.add_argument("--room", required=True, help="Room to measure, e.g. Room1")
    parser.add_argument("--biome", required=True, help="Biome name to stamp into the output, e.g. AssassinCity")
    parser.add_argument("--output", help="biome_spec output JSON path. Defaults to Tools/biome_spec_<biome>.json")
    parser.add_argument("--gdd-output", help="gdd_reference output JSON path. Defaults to Tools/gdd_reference.json")
    parser.add_argument("--gdd-path", default=GDD_PATH, help=f"Path to the GDD docx. Defaults to {GDD_PATH}")
    parser.add_argument("--timeout", type=float, default=5.0, help="Seconds to wait for the editor to respond to discovery.")
    parser.add_argument("--acknowledge-drift", action="store_true",
                         help="Proceed to measure the golden room even if live CDO values mismatch "
                              "the C++ header defaults beyond tolerance. Without this flag, a "
                              "mismatch halts before any room measurement happens.")
    args = parser.parse_args()

    biome_output_path = args.output or os.path.join(TOOLS_DIR, f"biome_spec_{args.biome}.json")
    gdd_output_path = args.gdd_output or os.path.join(TOOLS_DIR, "gdd_reference.json")

    # ---- Step 1: GDD ingestion (plain Python, no editor connection needed) ----
    print(f"Reading GDD: {args.gdd_path}")
    gdd_data = read_gdd(args.gdd_path, ROOM_TYPES_HEADER_PATH)
    print(f"  agent_roster: {len(gdd_data['agent_roster'])} agents")
    print(f"  room_roles.matches_real_enum: {gdd_data['room_roles']['matches_real_enum']}")
    print(f"  movement constants documented in GDD text with real numbers: "
          f"{gdd_data['movement_constants_named_in_gdd']['numeric_values_documented_in_gdd']}")

    # ---- Step 2: live-CDO-vs-C++-header-defaults comparison (the actual numeric check) ----
    print("Querying live movement constants from the editor (lightweight pass, no room measurement yet)...")
    try:
        live_values = query_live_movement_constants(args.timeout)
    except RuntimeError as exc:
        print(f"ERROR: {exc}")
        return 1
    cpp_defaults = get_cpp_documented_defaults(CHARACTER_HEADER_PATH)
    mismatches = compare_constants(cpp_defaults, live_values, MOVEMENT_CONSTANT_MISMATCH_TOLERANCE_FRACTION)

    gdd_data["movement_constants_check"] = {
        "cpp_documented_defaults": cpp_defaults,
        "cpp_only_constants_not_yet_live_queried": CPP_ONLY_CONSTANTS_NOT_YET_LIVE_QUERIED,
        "live_cdo_values": live_values,
        "tolerance_fraction": MOVEMENT_CONSTANT_MISMATCH_TOLERANCE_FRACTION,
        "mismatches": mismatches,
    }

    with open(gdd_output_path, "w", encoding="utf-8") as f:
        json.dump(gdd_data, f, indent=2)
    print(f"Wrote {gdd_output_path}")

    if mismatches:
        print("\nHALT -- live CDO values do not match DeathMetalCatCharacter.h's documented C++ defaults:")
        for m in mismatches:
            print(f"  {m['constant']}: cpp_default={m['cpp_documented_default']}, "
                  f"live={m['live_cdo_value']}, delta={m['delta']:.4f} "
                  f"({m['relative_delta']*100:.3f}% relative, tolerance={m['tolerance_fraction']*100:.1f}%)")
        if not args.acknowledge_drift:
            print("\nNot proceeding to measure the golden room. Re-run with --acknowledge-drift once "
                  "you've confirmed how to handle this (e.g. update the C++ defaults to match the "
                  "live-tuned values, or re-tune the Blueprint back to the documented defaults).")
            return 1
        print("\n--acknowledge-drift set -- proceeding to measure the golden room anyway.")
    else:
        print("\nLive CDO values match the documented C++ defaults within tolerance. Proceeding.")

    # ---- Step 3: existing golden-room measurement pass (unchanged) ----
    ok = extract(args.room, args.biome, biome_output_path, args.timeout)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
