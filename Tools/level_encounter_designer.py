#!/usr/bin/env python3
"""Level & Encounter Designer -- standalone agent from the Death Metal Cat GDD (Pillar 1 roster,
GDD Section 8.2 agent table). Same architecture as Tools/room_geometry_designer.py: one Claude API
call constrained by a system prompt built from real project data, followed by a deterministic
(non-LLM) validation pass before anything is considered final -- the model is never trusted to
get the schema or the difficulty-tier sanity checks right on its own.

Given a room's real difficulty tier/role and the spawn markers Room Geometry Designer already
placed in that room (Tools/room_geometry_<RoomID>.json), decides what (if anything) populates
each marker: an enemy (with a density/count hint), a pickup, or left empty. This is the
population DECISION only -- it does not spawn any actor in UE5. Placing a real enemy/pickup actor
at a populated marker is a separate, not-yet-built step (AEncounterSpawnMarker is explicitly a
data-only marker with no spawn logic of its own -- see EncounterSpawnMarker.h).

SCHEMA NOTES (read from the real generated data, not assumed -- see conversation for the full
audit before this was built):
- Tools/room_geometry_<RoomID>.json's spawn_markers entries are {marker_id, marker_type,
  after_piece_index}. There is NO position/coordinate field anywhere (after_piece_index is a
  structural reference to a piece, not spatial X/Y) -- AgentScripts/export_room_markers.py's live
  query is an even smaller subset, {marker_id, marker_type} only. Position is irrelevant to this
  agent's job anyway (it decides population, not placement), but don't assume a "marker position"
  field exists if extending this later.
- marker_type is a strict binary enum, EnemySpawn | PickupSpawn (see RoomTypes.h's
  EEncounterMarkerType) -- purely a candidate-slot descriptor from the geometry pass, not a
  binding commitment. This agent treats it as constraining which populations are valid for that
  marker (EnemySpawn -> enemy|empty, PickupSpawn -> pickup|empty), never crossing the streams,
  since Room Geometry Designer already placed EnemySpawn markers specifically within/adjacent to
  enemy_arena pieces (see validate_room()) and PickupSpawn markers elsewhere.
- difficulty_tier and room_role are NOT stored in room_geometry_<RoomID>.json at all -- they only
  exist as the ALL_ROOMS mapping inside room_geometry_designer.py. Reused directly from there
  (single source of truth) rather than re-declared here.
- marker_id has NO consistent naming convention across rooms (Room1 uses "spawn_0"/"spawn_1"...,
  Room4B uses "SM_Room4B_01"...) -- treated as an opaque passthrough string, never parsed.
- No pickup-type taxonomy exists ANYWHERE in the project yet -- no pickup actor class, no pickup
  enum beyond the marker's own EnemySpawn/PickupSpawn descriptor, and the GDD itself never
  designs an item/consumable economy (the core loop is kills -> XP -> auto-applied attributes,
  no pickups mentioned). PICKUP_TYPES below is therefore a single honest placeholder ("Generic"),
  not an invented variety -- flagged here rather than silently fabricating flavor types (health
  potion, ammo crate, etc.) with no design or implementation behind them.

Usage:
    python level_encounter_designer.py --room Room1
    python level_encounter_designer.py --all-rooms
"""

import argparse
import json
import os
import sys

from anthropic import Anthropic, AnthropicError

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)  # guarantees the sibling import below resolves regardless of cwd

import room_geometry_designer as geo_designer

MODEL = "claude-sonnet-4-6"

# {room_id: (role, tier)} -- reused directly from Room Geometry Designer's own ALL_ROOMS, the only
# place this project records difficulty tier/role per room (see schema notes above).
ROOM_ROLE_AND_TIER = {room_id: (role, tier) for room_id, role, tier in geo_designer.ALL_ROOMS}

VALID_ROOMS = list(ROOM_ROLE_AND_TIER.keys())

# Exactly one enemy type exists right now (ADeathMetalCatEnemyBase, see Source/PythonTest) -- the
# model must scale difficulty through density/spacing/pickup scarcity, never invented variety.
ENEMY_ROSTER_DESCRIPTION = (
    "Exactly one enemy type exists in the project right now: a generic melee/contact-damage "
    "enemy (ADeathMetalCatEnemyBase). There is no second type, no ranged enemy, no boss -- do "
    "not invent one. Difficulty scales ONLY through how many of this one type populate a marker "
    "and how the markers are already spaced (fixed by Room Geometry Designer, not yours to "
    "change), never through enemy variety."
)

# See schema notes above -- no real pickup-type taxonomy exists in this project yet. This is the
# single honest placeholder, not an invented item economy.
PICKUP_TYPES = ("Generic",)

# Per-tier ceiling on how many enemy instances a single EnemySpawn marker may represent -- the
# deterministic "no hard mathematical mismatch" check (e.g. a Room1 marker shouldn't come back
# saturated with a Room8-scale count). Placeholder values, tune freely. Rooms confirmed pure-combat
# (see HAS_TRAVERSAL_CHALLENGE_BUMP below) get +1 over their tier's normal ceiling, since that's
# the room's actual design intent, not a violation of it.
TIER_MAX_ENEMY_COUNT = {1: 1, 2: 1, 3: 2, 4: 2, 5: 3, 6: 3, 7: 4, 8: 4}
COMBAT_HEAVY_CEILING_BUMP = 1

SYSTEM_PROMPT = f"""You are the Level & Encounter Designer for Death Metal Cat, a 2D side-scroller (GDD Section 8.2). Given a room's difficulty tier/role and the exact list of spawn markers Room Geometry Designer already placed in that room, you decide what (if anything) populates each marker. You do NOT place markers, move them, or add/remove any -- every marker in the input list gets exactly one population decision, no more, no fewer.

ENEMY ROSTER: {ENEMY_ROSTER_DESCRIPTION}

PICKUP TYPES: only {", ".join(PICKUP_TYPES)!r} exists as a pickup type right now -- no health potions, ammo crates, or other invented items. If a marker becomes a pickup, its pickup_type MUST be one of: {", ".join(repr(t) for t in PICKUP_TYPES)}.

MARKER TYPE IS A CONSTRAINT, NOT A SUGGESTION: each input marker has a marker_type of "EnemySpawn" or "PickupSpawn", set by Room Geometry Designer as a candidate-slot descriptor (EnemySpawn markers already sit within/adjacent to combat arenas, PickupSpawn markers elsewhere). You may leave either kind "empty", but you may NEVER assign "enemy" to a PickupSpawn marker or "pickup" to an EnemySpawn marker -- that would contradict the room's own structural design.

DIFFICULTY CURVE (GDD Section 7 -- the level should ramp from forgiving to dangerous):
- Tiers 1-3 (early rooms): sparse and forgiving. Lean toward "empty" for a meaningful fraction of markers -- not every candidate slot needs to be filled. Pickups are weighted to appear here since they matter most early (the player has the least health/attribute buffer). Enemy counts should sit at the low end of that tier's ceiling.
- Tiers 4-5 (mid rooms): the ramp begins. Enemy density and count increase, pickups get somewhat scarcer, but this is a transition, not the hardest part of the run yet.
- Tiers 6-8 (late rooms, up to the biome end): busier and more dangerous. Enemy density and per-marker counts should sit at or near the top of that tier's ceiling, pickups scarcer still, tension rising toward the biome end.
- Combat-heavy branch rooms (a room role of branch_a/branch_b with NO traversal pieces at all in its actual generated geometry -- you will be told explicitly whether this room qualifies) should get enemy density at or above their tier's normal ceiling, since a pure-combat room is that way by deliberate design, not an accident of its raw tier number.
- Whole-run pacing signal (not this decision alone to guarantee, but keep it in mind and reference it in your rationale where relevant): across a full playthrough, the player's Gnarly Rank should build up and fully reset (from taking a hit) at least twice, per GDD Section 7. A rationale that explains how this room's density contributes to (or deliberately avoids, if it's meant as a breather) that build-and-reset rhythm is exactly the kind of reasoning the Balance/QA Reviewer agent will need to check later -- write real reasoning tied to this, not filler like "adds challenge".

OUTPUT FORMAT (strict -- this is the entire response):
Return ONLY a single JSON object. No preamble, no trailing commentary, no markdown code fences. Exactly this shape:
{{"room_id": "<id>", "populations": [{{"marker_id": "<id>", "population": "enemy", "enemy_count": 1, "rationale": "<why>"}}, {{"marker_id": "<id>", "population": "pickup", "pickup_type": "Generic", "rationale": "<why>"}}, {{"marker_id": "<id>", "population": "empty", "rationale": "<why>"}}]}}
Every marker_id from the input list must appear exactly once in "populations". "enemy_count" is required (and only meaningful) when population is "enemy"; "pickup_type" is required (and only meaningful) when population is "pickup"; omit both for "empty". "rationale" is required on every entry."""


def build_user_prompt(
    room_id: str,
    tier: int,
    role: str,
    markers: list[dict],
    has_traversal_challenge: bool,
    validation_feedback: str | None = None,
) -> str:
    marker_lines = "\n".join(
        f"  - marker_id={m['marker_id']!r}, marker_type={m['marker_type']!r}" for m in markers
    )
    ceiling = TIER_MAX_ENEMY_COUNT[tier] + (0 if has_traversal_challenge else COMBAT_HEAVY_CEILING_BUMP)
    lines = [
        f"room_id: {room_id}",
        f"difficulty_tier: {tier} (1-8)",
        f"room_role: {role}",
        f"combat_heavy (no traversal pieces at all in this room's real geometry): {not has_traversal_challenge}",
        f"this room's enemy_count ceiling per marker: {ceiling}",
        f"markers ({len(markers)} total, every one needs exactly one decision):",
        marker_lines,
    ]
    if validation_feedback:
        lines.append(
            "Your previous attempt failed deterministic validation with this exact error -- "
            "fix the specific marker/decision it names, do not just regenerate blindly:\n"
            f"{validation_feedback}"
        )
    lines.append("Generate the population decisions now, per the difficulty curve and the strict JSON output format.")
    return "\n".join(lines)


def _extract_json_object(raw: str) -> dict:
    """Strip accidental markdown fences before parsing -- the prompt forbids them,
    this is just a defensive tolerance for an occasional slip, not a format we rely on."""
    text = raw.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1] if "\n" in text else text[3:]
        if text.endswith("```"):
            text = text[:-3]
        text = text.strip()
        if text.lower().startswith("json"):
            text = text[4:].strip()
    return json.loads(text)


def generate_population(
    client: Anthropic,
    room_id: str,
    tier: int,
    role: str,
    markers: list[dict],
    has_traversal_challenge: bool,
    validation_feedback: str | None = None,
) -> dict:
    response = client.messages.create(
        model=MODEL,
        max_tokens=4000,
        system=SYSTEM_PROMPT,
        messages=[{
            "role": "user",
            "content": build_user_prompt(room_id, tier, role, markers, has_traversal_challenge, validation_feedback),
        }],
    )
    raw_text = next(block.text for block in response.content if block.type == "text")

    try:
        result = _extract_json_object(raw_text)
    except json.JSONDecodeError as exc:
        raise ValueError(
            f"Model did not return valid JSON for room_id={room_id!r}. Raw response:\n{raw_text}"
        ) from exc

    for key in ("room_id", "populations"):
        if key not in result:
            raise ValueError(f"Model JSON missing required key {key!r}: {result}")

    return result


def validate_population(
    markers: list[dict],
    tier: int,
    has_traversal_challenge: bool,
    result: dict,
) -> tuple[bool, str | None]:
    """Deterministic, non-LLM validation -- never trusts the model's own arithmetic or bookkeeping.
    Confirms every marker got exactly one decision, the decision is compatible with that marker's
    marker_type, the schema is well-formed, and enemy counts don't exceed a hard per-tier ceiling.
    This is a basic sanity check, not full balance analysis (that's Balance/QA Reviewer's separate,
    not-yet-built job)."""
    populations = result.get("populations", [])
    if not populations:
        return False, "result has no populations"

    marker_type_by_id = {m["marker_id"]: m["marker_type"] for m in markers}
    expected_ids = set(marker_type_by_id.keys())

    seen_ids: set[str] = set()
    ceiling = TIER_MAX_ENEMY_COUNT[tier] + (0 if has_traversal_challenge else COMBAT_HEAVY_CEILING_BUMP)

    for entry in populations:
        marker_id = entry.get("marker_id")
        if marker_id not in expected_ids:
            return False, f"populations entry references marker_id={marker_id!r}, which is not in this room's marker list"
        if marker_id in seen_ids:
            return False, f"marker_id={marker_id!r} was assigned a decision more than once"
        seen_ids.add(marker_id)

        population = entry.get("population")
        if population not in ("enemy", "pickup", "empty"):
            return False, f"marker_id={marker_id!r} has invalid population={population!r} (must be enemy/pickup/empty)"

        marker_type = marker_type_by_id[marker_id]
        if population == "enemy" and marker_type != "EnemySpawn":
            return False, f"marker_id={marker_id!r} is marker_type={marker_type!r} but was assigned population='enemy' -- only EnemySpawn markers may become enemy"
        if population == "pickup" and marker_type != "PickupSpawn":
            return False, f"marker_id={marker_id!r} is marker_type={marker_type!r} but was assigned population='pickup' -- only PickupSpawn markers may become pickup"

        if population == "enemy":
            enemy_count = entry.get("enemy_count")
            if not isinstance(enemy_count, int):
                return False, f"marker_id={marker_id!r} has population='enemy' but missing/non-integer enemy_count"
            if enemy_count < 1:
                return False, f"marker_id={marker_id!r} has non-positive enemy_count={enemy_count}"
            if enemy_count > ceiling:
                return False, (
                    f"marker_id={marker_id!r} has enemy_count={enemy_count}, which exceeds this room's "
                    f"tier-{tier} ceiling of {ceiling} -- a hard density mismatch for this room's difficulty"
                )

        if population == "pickup":
            pickup_type = entry.get("pickup_type")
            if pickup_type not in PICKUP_TYPES:
                return False, f"marker_id={marker_id!r} has invalid pickup_type={pickup_type!r} (must be one of {PICKUP_TYPES})"

        rationale = entry.get("rationale")
        if not isinstance(rationale, str) or not rationale.strip():
            return False, f"marker_id={marker_id!r} is missing a non-empty rationale"

    missing_ids = expected_ids - seen_ids
    if missing_ids:
        return False, f"marker_id(s) {sorted(missing_ids)} from the input marker list got no decision at all"

    return True, None


MAX_VALIDATION_RETRIES = 3


def generate_valid_population(
    client: Anthropic,
    room_id: str,
    tier: int,
    role: str,
    markers: list[dict],
    has_traversal_challenge: bool,
) -> dict:
    feedback: str | None = None
    result: dict = {}
    for attempt in range(MAX_VALIDATION_RETRIES + 1):
        result = generate_population(client, room_id, tier, role, markers, has_traversal_challenge, validation_feedback=feedback)
        is_valid, error = validate_population(markers, tier, has_traversal_challenge, result)
        if is_valid:
            return result
        print(f"  [{room_id}] validation failed (attempt {attempt + 1}): {error}", file=sys.stderr)
        feedback = error

    raise ValueError(f"Population for {room_id!r} still failed validation after {MAX_VALIDATION_RETRIES} retries: {feedback}")


def load_room_markers(room_id: str) -> tuple[int, str, list[dict], bool]:
    """Reads a room's real spawn marker list straight from Room Geometry Designer's own generated
    JSON (Tools/room_geometry_<RoomID>.json) -- the same file already imported into the live UE5
    level -- rather than assuming a schema. Returns (tier, role, markers, has_traversal_challenge),
    where has_traversal_challenge is computed from the room's ACTUAL piece list (True if it
    contains any gap/ledge_step/wall_jump_shaft), not hardcoded by room name, so a future room
    with the same "pure combat" design intent is detected the same way Room4B is."""
    if room_id not in ROOM_ROLE_AND_TIER:
        raise ValueError(f"{room_id!r} is not a valid room. Must be one of: {VALID_ROOMS}")
    role, tier = ROOM_ROLE_AND_TIER[room_id]

    json_path = os.path.join(TOOLS_DIR, f"room_geometry_{room_id}.json")
    if not os.path.exists(json_path):
        raise ValueError(f"{json_path} does not exist -- generate it with room_geometry_designer.py first.")

    with open(json_path, "r", encoding="utf-8") as f:
        room = json.load(f)

    markers = [{"marker_id": m["marker_id"], "marker_type": m["marker_type"]} for m in room.get("spawn_markers", [])]
    has_traversal_challenge = any(
        p["type"] in ("gap", "ledge_step", "wall_jump_shaft") for p in room.get("pieces", [])
    )

    return tier, role, markers, has_traversal_challenge


def run_room(client: Anthropic, room_id: str, output_dir: str) -> dict:
    tier, role, markers, has_traversal_challenge = load_room_markers(room_id)
    if not markers:
        return {"room_id": room_id, "marker_count": 0, "enemy": 0, "pickup": 0, "empty": 0, "valid": True, "detail": "no markers in this room -- nothing to populate"}

    try:
        result = generate_valid_population(client, room_id, tier, role, markers, has_traversal_challenge)
    except (AnthropicError, ValueError) as exc:
        return {"room_id": room_id, "marker_count": len(markers), "enemy": 0, "pickup": 0, "empty": 0, "valid": False, "detail": str(exc)}

    out_path = os.path.join(output_dir, f"encounter_population_{room_id}.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)

    counts = {"enemy": 0, "pickup": 0, "empty": 0}
    for entry in result["populations"]:
        counts[entry["population"]] += 1

    return {
        "room_id": room_id,
        "marker_count": len(markers),
        "enemy": counts["enemy"],
        "pickup": counts["pickup"],
        "empty": counts["empty"],
        "valid": True,
        "detail": out_path,
    }


def print_summary_table(summary: list[dict]) -> None:
    header = f"{'Room':<10} {'Markers':>7} {'Enemy':>6} {'Pickup':>7} {'Empty':>6} {'Valid':>6}  Detail"
    print(header)
    print("-" * len(header))
    for row in summary:
        valid_str = "yes" if row["valid"] else "NO"
        print(f"{row['room_id']:<10} {row['marker_count']:>7} {row['enemy']:>6} {row['pickup']:>7} {row['empty']:>6} {valid_str:>6}  {row['detail']}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Level & Encounter Designer -- Death Metal Cat's marker-population agent (standalone, calls Claude API directly)."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--room", help=f"Single room to populate. One of: {', '.join(VALID_ROOMS)}")
    group.add_argument("--all-rooms", action="store_true", help="Populate all 9 real rooms.")
    parser.add_argument("--output-dir", default=TOOLS_DIR, help="Directory to save each room's population JSON in (default: this script's own directory).")
    args = parser.parse_args()

    try:
        client = Anthropic()
    except AnthropicError as exc:
        print(f"Failed to initialize Anthropic client: {exc}", file=sys.stderr)
        print("Set the ANTHROPIC_API_KEY environment variable and try again.", file=sys.stderr)
        return 1

    if args.all_rooms:
        summary = [run_room(client, room_id, args.output_dir) for room_id in VALID_ROOMS]
        print_summary_table(summary)
        return 1 if any(not row["valid"] for row in summary) else 0

    room_lookup = {name.lower(): name for name in VALID_ROOMS}
    canonical = room_lookup.get(args.room.lower())
    if canonical is None:
        print(f"ERROR: '{args.room}' is not a valid room. Must be one of: {', '.join(VALID_ROOMS)}")
        return 1

    row = run_room(client, canonical, args.output_dir)
    print_summary_table([row])
    return 0 if row["valid"] else 1


if __name__ == "__main__":
    sys.exit(main())
