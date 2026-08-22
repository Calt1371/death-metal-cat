#!/usr/bin/env python3
"""Integration test for the three-agent Death Metal Cat content pipeline (GDD Section 4.2).

Proves the HANDOFF between agents, not just that each runs in isolation:

  Room Geometry Designer  --spawn_markers-->  Level & Encounter Designer  --enemy markers-->  Quip Generator

Step 1 calls Room Geometry Designer's own generate_valid_room() for Room1's real role/tier
(from its own ALL_ROOMS -- the single source of truth other tooling in this project already
reuses), as a fresh, separate, throwaway generation. It does NOT read Room1's real committed
Tools/room_geometry_Room1.json.

Step 2 takes that exact in-memory spawn_markers list -- unmodified, no disk roundtrip -- and
feeds it straight into Level & Encounter Designer's generate_valid_population(), the same
function level_encounter_designer.py's own CLI calls once it has read a room's markers.

Step 3 takes Step 2's population decisions and, for every marker populated with an enemy,
calls Quip Generator's generate_unique_quip() for a "kill" trigger -- i.e. the exact quip that
would fire when that specific spawned enemy dies. Cayde is the only character in this project;
his voice is locked directly into Quip Generator's system prompt, so there is no separate
"character profile" argument to pass.

SANDBOXED DEMO -- SAFETY NOTE: this script only imports the three agents' generation
functions and writes JSON under Tools/test_output/. It never calls
Tools/import_room_geometry.py, Tools/spawn_encounter_actors.py, or anything else that opens a
Remote Execution connection to the live UE5 editor. Nothing in Content/, the level, or any of
the 9 rooms' real committed JSON is read from or written to.

Usage:
    python test_agent_crew_pipeline.py
"""

import json
import os
import sys
from datetime import datetime, timezone

from anthropic import AnthropicError

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)  # sibling imports below resolve regardless of cwd

import room_geometry_designer as geo_designer
import level_encounter_designer as encounter_designer
import quip_generator

OUTPUT_DIR = os.path.join(TOOLS_DIR, "test_output")

ROOM_ID = "Room1"
# Matches this project's own --batch-rooms convention (see room_geometry_designer.py's module
# docstring: "python room_geometry_designer.py --batch-rooms --length 1200") -- not read from
# Room1's real committed JSON, per this run's explicit isolation requirement.
TARGET_ROOM_LENGTH = 1200.0

KILL_QUIP_CONTEXT = "basic_enemy"  # the one real enemy type in the project (ADeathMetalCatEnemyBase)


def fail(step: str, agent: str, detail: str) -> "typing.NoReturn":
    print(f"\nPIPELINE FAILED at {step} ({agent}):\n  {detail}", file=sys.stderr)
    sys.exit(1)


def write_json(filename: str, payload) -> str:
    path = os.path.join(OUTPUT_DIR, filename)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
    return path


def main() -> int:
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    try:
        from anthropic import Anthropic
        client = Anthropic()
    except AnthropicError as exc:
        fail("client init", "n/a", f"Failed to initialize Anthropic client: {exc}")

    room_role_and_tier = {room_id: (role, tier) for room_id, role, tier in geo_designer.ALL_ROOMS}
    if ROOM_ID not in room_role_and_tier:
        fail("setup", "n/a", f"{ROOM_ID!r} is not in Room Geometry Designer's own ALL_ROOMS")
    role, tier = room_role_and_tier[ROOM_ID]

    # ---- Step 1: Room Geometry Designer (generation + validation only, no import) ----
    print(f"=== STEP 1: Room Geometry Designer -- fresh isolated generation for {ROOM_ID} (role={role}, tier={tier}) ===")
    try:
        room = geo_designer.generate_valid_room(client, ROOM_ID, tier, role, TARGET_ROOM_LENGTH)
    except AnthropicError as exc:
        fail("Step 1", "Room Geometry Designer", f"Claude API error: {exc}")
    except ValueError as exc:
        fail("Step 1", "Room Geometry Designer", str(exc))

    geometry_path = write_json("room1_geometry_demo.json", room)
    print(f"  OK -- {len(room['pieces'])} pieces, {len(room['spawn_markers'])} spawn markers -> {geometry_path}")

    # ---- Step 2: Level & Encounter Designer, fed Step 1's exact marker list (in-memory) ----
    print("=== STEP 2: Level & Encounter Designer -- consuming Step 1's spawn_markers directly ===")
    markers = [{"marker_id": m["marker_id"], "marker_type": m["marker_type"]} for m in room["spawn_markers"]]
    has_traversal_challenge = any(
        p["type"] in ("gap", "ledge_step", "wall_jump_shaft") for p in room["pieces"]
    )
    if not markers:
        fail("Step 2", "Level & Encounter Designer", "Step 1's room came back with zero spawn_markers -- nothing to hand off")

    try:
        population = encounter_designer.generate_valid_population(
            client, ROOM_ID, tier, role, markers, has_traversal_challenge
        )
    except AnthropicError as exc:
        fail("Step 2", "Level & Encounter Designer", f"Claude API error: {exc}")
    except ValueError as exc:
        fail("Step 2", "Level & Encounter Designer", str(exc))

    encounters_path = write_json("room1_encounters_demo.json", population)
    counts = {"enemy": 0, "pickup": 0, "empty": 0}
    for entry in population["populations"]:
        counts[entry["population"]] += 1
    print(f"  OK -- {counts['enemy']} enemy, {counts['pickup']} pickup, {counts['empty']} empty -> {encounters_path}")

    # ---- Step 3: Quip Generator, one "kill" quip per enemy-populated marker ----
    print("=== STEP 3: Quip Generator -- one 'kill' quip per enemy-populated marker (Cayde's locked voice) ===")
    quips_by_marker: dict = {}
    used_lines: list = []
    enemy_entries = [e for e in population["populations"] if e["population"] == "enemy"]
    if not enemy_entries:
        print("  (no enemy-populated markers in this run -- nothing for Quip Generator to voice)")
    for entry in enemy_entries:
        marker_id = entry["marker_id"]
        result = quip_generator.generate_unique_quip(client, "kill", KILL_QUIP_CONTEXT, used_lines)
        if "error" in result:
            fail("Step 3", "Quip Generator", f"marker {marker_id!r}: {result['error']}")
        used_lines.append(result["line"])
        quips_by_marker[marker_id] = result
        print(f"  [{marker_id}] \"{result['line']}\" ({result['sound_tag']})")

    quips_path = write_json("room1_quips_demo.json", quips_by_marker)
    print(f"  OK -- {len(quips_by_marker)} quip(s) generated -> {quips_path}")

    # ---- Step 4: combined trace, keyed by marker_id ----
    print("=== STEP 4: combined trace (Room Geometry Designer -> Level & Encounter Designer -> Quip Generator) ===")
    population_by_marker = {e["marker_id"]: e for e in population["populations"]}
    geometry_by_marker = {m["marker_id"]: m for m in room["spawn_markers"]}

    trace_markers = {}
    for marker_id in geometry_by_marker:
        trace_markers[marker_id] = {
            "geometry_output": geometry_by_marker[marker_id],
            "encounter_decision": population_by_marker.get(marker_id),
            "quip_selected": quips_by_marker.get(marker_id),
        }

    trace = {
        "room_id": ROOM_ID,
        "difficulty_tier": tier,
        "room_role": role,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "note": "Sandboxed demo run -- generation functions only, no live UE5 level was read or modified.",
        "summary": {
            "piece_count": len(room["pieces"]),
            "marker_count": len(markers),
            "enemy": counts["enemy"],
            "pickup": counts["pickup"],
            "empty": counts["empty"],
            "quips_generated": len(quips_by_marker),
        },
        "markers": trace_markers,
    }
    trace_path = write_json("pipeline_run_room1.json", trace)
    print(json.dumps(trace, indent=2))
    print(f"\nCombined trace written -> {trace_path}")
    print("\nPIPELINE OK -- all three agents handed off correctly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
