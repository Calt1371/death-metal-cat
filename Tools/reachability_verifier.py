#!/usr/bin/env python3
"""
reachability_verifier.py

Reachability Verifier -- standalone agent from the Death Metal Cat GDD (Section 4.2 agent
roster). Independently re-checks every gap in a generated room against biome_spec_<Biome>.json's
measured movement constants -- the deterministic (non-LLM) validation pass described in the GDD.

Does NOT reimplement the gap-computation/reachability math -- imports and calls
foundation_extractor.compute_gaps() directly, the exact same function Foundation Extractor's own
embedded UE template uses to measure the golden room. There is exactly one implementation of this
math in the project; this script is a second CALLER of it, not a second COPY of it.

INPUT:
  - A generated room's platform actor list -- queried live from the room's RoomShell (same
    StaticMeshActor/OneWayPlatform classification Foundation Extractor uses), OR supplied directly
    as a platform_data JSON file (see --platform-data) for a room that isn't live in the editor.
  - biome_spec_<Biome>.json -- for movement_constants (the SAME constants used to build the spec
    in the first place, so a generated room is checked against the same ruler it was designed
    against).

OUTPUT: pass/fail per gap (a gap passes iff reachable_by is non-empty -- at least one of
jump/wall_jump/dodge clears it) plus an overall room pass/fail (a room passes iff every gap
passes). Written to JSON and printed as a table.

Usage:
    python reachability_verifier.py --room Room2 --biome AssassinCity
    python reachability_verifier.py --room Room2 --biome AssassinCity --output Tools/test_output/reachability_Room2.json
    python reachability_verifier.py --platform-data platform_data.json --biome AssassinCity
"""

import argparse
import json
import os
import sys
import tempfile
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, os.path.join(TOOLS_DIR, "..", "AgentScripts"))

from remote_execution import RemoteExecution
import foundation_extractor as fe

VALID_ROOMS = ["Room1", "Room2", "Room3", "Room4A", "Room4B", "Room5", "Room6", "Room7", "Room8"]

# Same platform classification Foundation Extractor uses (see its "Classify every attached actor"
# section) -- StaticMeshActor (flat-run floor pieces) AND OneWayPlatform (pass-through climbing
# platforms). Kept as a comment cross-reference rather than re-imported, since the actual filter
# is baked into the embedded template below (isinstance checks need `unreal`, not importable from
# the outer Tools/ process).

_QUERY_PLATFORMS_TEMPLATE = """
import json
import unreal

room_id_name = "__ROOM_ID_NAME__"
output_path = r"__OUTPUT_PATH__"

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_subsystem.get_all_level_actors()
room_shell = next(
    (a for a in all_actors if isinstance(a, unreal.RoomShell) and a.get_actor_label() == "RoomShell_" + room_id_name),
    None,
)
if room_shell is None:
    raise RuntimeError("[REACHABILITY VERIFIER] No RoomShell_" + room_id_name + " found in the level")

attached = room_shell.get_attached_actors(False, True)

platform_data = []
for a in attached:
    if isinstance(a, unreal.DeathMetalCatCharacter) or isinstance(a, unreal.DeathMetalCatEnemyBase):
        continue
    if not (isinstance(a, unreal.StaticMeshActor) or isinstance(a, unreal.OneWayPlatform)):
        continue
    origin, extent = a.get_actor_bounds(False)
    platform_data.append({
        "label": a.get_actor_label(),
        "left_x": origin.x - extent.x,
        "right_x": origin.x + extent.x,
        "top_z": origin.z + extent.z,
    })

with open(output_path, "w", encoding="utf-8") as f:
    json.dump(platform_data, f, indent=2)

unreal.log_warning("[REACHABILITY VERIFIER] " + room_id_name + ": queried " + str(len(platform_data)) + " platform actor(s)")
"""


def build_query_command(room_id_name: str, output_path: str) -> str:
    return (
        _QUERY_PLATFORMS_TEMPLATE
        .replace("__ROOM_ID_NAME__", room_id_name)
        .replace("__OUTPUT_PATH__", output_path)
    )


def query_live_platform_data(room: str, timeout: float) -> list:
    """Live-queries a room's platform actors from the currently-open editor level -- same
    RemoteExecution bridge pattern as every other Tools/ script, same platform classification
    Foundation Extractor uses. Returns a list of {"label", "left_x", "right_x", "top_z"} dicts,
    exactly the shape compute_gaps() expects."""
    room_id_name = room.upper()
    temp_fd, temp_output_path = tempfile.mkstemp(suffix=".json", prefix="reachability_platforms_")
    os.close(temp_fd)
    script_body = build_query_command(room_id_name, temp_output_path)

    temp_fd, temp_script_path = tempfile.mkstemp(suffix=".py", prefix="reachability_verifier_")
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
        raise RuntimeError(f"editor reported failure querying platform data for {room}:\n{result}")
    for entry in result.get("output", []):
        print(f"[UE5] {entry.get('type')}: {entry.get('output')}")
        if entry.get("type") == "Error":
            raise RuntimeError(f"platform-data query for {room} reported at least one error above")

    if not os.path.exists(temp_output_path):
        raise RuntimeError(f"{temp_output_path} does not exist after a reported-successful run")
    with open(temp_output_path, "r", encoding="utf-8") as f:
        platform_data = json.load(f)
    os.remove(temp_output_path)
    return platform_data


def verify_room(room_label: str, platform_data: list, movement_constants: dict) -> dict:
    """Deterministic, non-LLM check -- no model judgment involved, same philosophy as Foundation
    Extractor's own gap logic (which this function calls directly, not a reimplementation).
    A gap passes iff reachable_by is non-empty (at least one of jump/wall_jump/dodge clears it).
    The room passes iff every gap passes."""
    gaps = fe.compute_gaps(
        platform_data,
        movement_constants["max_jump_distance"],
        movement_constants["max_jump_height"],
        movement_constants["wall_jump_max_distance"],
        movement_constants["wall_jump_max_height"],
        movement_constants["dodge_distance"],
    )

    gap_results = []
    for g in gaps:
        passed = bool(g["reachable_by"])
        gap_results.append({
            "from_label": g["from_label"],
            "to_label": g["to_label"],
            "distance": g["distance"],
            "reachable_by": g["reachable_by"],
            "tolerance": g["tolerance"],
            "passed": passed,
        })

    room_passed = all(g["passed"] for g in gap_results) if gap_results else True
    return {
        "room": room_label,
        "platform_count": len(platform_data),
        "gap_count": len(gap_results),
        "gaps": gap_results,
        "room_passed": room_passed,
        "failed_gap_count": sum(1 for g in gap_results if not g["passed"]),
    }


def print_report(result: dict) -> None:
    print(f"\n=== Reachability Verifier: {result['room']} ===")
    print(f"platforms={result['platform_count']} gaps={result['gap_count']}")
    header = f"{'from':<22} {'to':<22} {'dist':>8} {'reachable_by':<28} {'tolerance':<12} {'pass':>5}"
    print(header)
    print("-" * len(header))
    for g in result["gaps"]:
        status = "PASS" if g["passed"] else "FAIL"
        print(f"{g['from_label']:<22} {g['to_label']:<22} {g['distance']:>8.1f} "
              f"{','.join(g['reachable_by']) or '(none)':<28} {g['tolerance']:<12} {status:>5}")
    print("-" * len(header))
    overall = "PASS" if result["room_passed"] else "FAIL"
    print(f"OVERALL: {overall} ({result['failed_gap_count']} of {result['gap_count']} gap(s) failed)")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reachability Verifier -- re-checks a generated room's gaps against biome_spec.json's measured movement constants."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--room", help=f"Room to verify, queried live from the open editor level. One of: {', '.join(VALID_ROOMS)}")
    group.add_argument("--platform-data", help="Path to a platform_data JSON file (list of {label,left_x,right_x,top_z}) instead of a live query.")
    parser.add_argument("--biome", required=True, help="Biome name whose biome_spec_<Biome>.json supplies movement_constants, e.g. AssassinCity")
    parser.add_argument("--biome-spec-path", help="Override path to biome_spec_<Biome>.json. Defaults to Tools/biome_spec_<biome>.json")
    parser.add_argument("--output", help="Output JSON path. Defaults to Tools/reachability_<Room>.json (or _platform_data if --platform-data was used)")
    parser.add_argument("--timeout", type=float, default=5.0, help="Seconds to wait for the editor to respond to discovery (--room mode only).")
    args = parser.parse_args()

    biome_spec_path = args.biome_spec_path or os.path.join(TOOLS_DIR, f"biome_spec_{args.biome}.json")
    if not os.path.exists(biome_spec_path):
        print(f"ERROR: {biome_spec_path} does not exist -- generate it with foundation_extractor.py first.")
        return 1
    with open(biome_spec_path, "r", encoding="utf-8") as f:
        biome_spec = json.load(f)
    movement_constants = biome_spec["movement_constants"]

    if args.room:
        room_lookup = {name.lower(): name for name in VALID_ROOMS}
        canonical = room_lookup.get(args.room.lower())
        if canonical is None:
            print(f"ERROR: '{args.room}' is not a valid room. Must be one of: {', '.join(VALID_ROOMS)}")
            return 1
        try:
            platform_data = query_live_platform_data(canonical, args.timeout)
        except RuntimeError as exc:
            print(f"ERROR: {exc}")
            return 1
        room_label = canonical
        default_output = os.path.join(TOOLS_DIR, f"reachability_{canonical}.json")
    else:
        with open(args.platform_data, "r", encoding="utf-8") as f:
            platform_data = json.load(f)
        room_label = os.path.basename(args.platform_data)
        default_output = os.path.join(TOOLS_DIR, "reachability_platform_data.json")

    if not platform_data:
        print(f"ERROR: no platform actors found for {room_label} -- nothing to verify.")
        return 1

    result = verify_room(room_label, platform_data, movement_constants)
    print_report(result)

    output_path = args.output or default_output
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    print(f"\nWrote {output_path}")

    return 0 if result["room_passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
