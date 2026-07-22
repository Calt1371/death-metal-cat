#!/usr/bin/env python3
"""Room Geometry Designer -- standalone agent from the Death Metal Cat GDD (Pillar 1 roster).

Same structure/pattern as quip_generator.py (locked system prompt, one Claude API call per
generation, strict-JSON-only output, retry-with-feedback loop). Feeds into the still-pending
Level & Encounter Designer / import-to-UE5 pipeline (see Docs/Death_Metal_Cat_Build_Outline.txt
tasks #24-26) -- level_encounter_designer.py itself doesn't exist yet in this repo, so this
script is patterned directly off quip_generator.py instead.

Given a room_id, difficulty_tier (1-8), room_role (linear/branch_a/branch_b), and a rough
target_room_length, composes a single room's traversal layout as a strict sequence of geometry
pieces (see PIECE VOCABULARY below) plus spawn markers, expressed as strict JSON.

Movement constraints are hard-coded as physics-derived numbers from ADeathMetalCatCharacter's
actual current tuning (queried live from BP_DeathMetalCat's CDO + the project's PhysicsSettings
on 2026-07-21 -- see MOVEMENT CONSTRAINTS below for the exact source values and formulas). These
are baked into the system prompt as hard bounds, AND re-checked in validate_room() -- the model
is never trusted to do this arithmetic correctly on its own.

Usage:
    python room_geometry_designer.py --room Room4 --tier 4 --role linear --length 3000
    python room_geometry_designer.py --room Room4A --tier 4 --role branch_a --length 2500
    python room_geometry_designer.py --room Room4B --tier 4 --role branch_b --length 2500

    # Batch mode: generate all 9 real city-biome rooms (see ALL_ROOMS) at once, each with its
    # correct role/tier, saving each to Tools/room_geometry_<RoomID>.json and printing a summary
    # table instead of a single JSON blob.
    python room_geometry_designer.py --batch-rooms --length 1200
"""

import argparse
import json
import os
import sys

from anthropic import Anthropic, AnthropicError

MODEL = "claude-sonnet-4-6"

ROOM_ROLES = ("linear", "branch_a", "branch_b")

# ---- Movement constraints -----------------------------------------------------------------
# Source values queried live from BP_DeathMetalCat's CharacterMovementComponent/CDO and the
# project's PhysicsSettings on 2026-07-21 (Config default gravity, unmodified):
#   JumpZVelocity=420.0, GravityScale=1.0, MaxWalkSpeed=600.0, DodgeImpulseStrength=1200.0,
#   DodgeDuration=0.75, WallJumpForceHorizontal=600.0, WallJumpForceVertical=700.0,
#   world GravityZ=-980.0 (PhysicsSettings.default_gravity_z)
# These are derived, not placeholders -- if any of the above tunables change in the engine,
# re-query and update the constants below to match, since the whole point is that a generated
# room is only ever as crossable as Cayde's ACTUAL current moveset allows.
GRAVITY_Z = 980.0  # magnitude, uu/s^2

JUMP_Z_VELOCITY = 420.0
MAX_WALK_SPEED = 600.0
# Full jump-arc height: v^2 / (2g).
MAX_JUMP_HEIGHT = JUMP_Z_VELOCITY ** 2 / (2 * GRAVITY_Z)
# Full jump-arc airtime (up and back down to the same height): 2v/g. Horizontal distance
# assumes full running speed maintained through the whole arc (an ideal-case running jump,
# the correct upper bound for "is this gap ever crossable").
_JUMP_AIRTIME = 2 * JUMP_Z_VELOCITY / GRAVITY_Z
MAX_JUMP_DISTANCE = MAX_WALK_SPEED * _JUMP_AIRTIME

WALL_JUMP_FORCE_HORIZONTAL = 600.0
WALL_JUMP_FORCE_VERTICAL = 700.0
WALL_JUMP_MAX_HEIGHT = WALL_JUMP_FORCE_VERTICAL ** 2 / (2 * GRAVITY_Z)
_WALL_JUMP_AIRTIME = 2 * WALL_JUMP_FORCE_VERTICAL / GRAVITY_Z
WALL_JUMP_MAX_DISTANCE = WALL_JUMP_FORCE_HORIZONTAL * _WALL_JUMP_AIRTIME

DODGE_IMPULSE_STRENGTH = 1200.0
DODGE_DURATION = 0.75
# Simple v*t approximation (no friction/deceleration modeled) -- consistent with this project's
# general precision level for placeholder movement tunables, not a physically exact simulation.
DODGE_DISTANCE = DODGE_IMPULSE_STRENGTH * DODGE_DURATION

# Nominal fixed horizontal footprints for pieces whose own params don't include a width, used
# only for loosely budgeting against target_room_length (told to the model in the prompt too).
WALL_JUMP_SHAFT_NOMINAL_WIDTH = 200.0
DROP_DOWN_NOMINAL_WIDTH = 150.0

PIECE_TYPES = ("flat_run", "ledge_step", "gap", "wall_jump_shaft", "drop_down", "enemy_arena")

# The 9 real rooms in the city biome's progression chain (see Source/PythonTest/RoomTypes.h /
# RoomProgressionManager) -- used by --batch-rooms to (re)generate every room's geometry JSON in
# one pass with each room's correct role/tier, rather than the caller having to hand-type all 9
# invocations (and risk a typo'd role/tier mismatch against the actual room-progression graph).
ALL_ROOMS = (
    ("Room1", "linear", 1),
    ("Room2", "linear", 2),
    ("Room3", "linear", 3),
    ("Room4A", "branch_a", 4),
    ("Room4B", "branch_b", 4),
    ("Room5", "linear", 5),
    ("Room6", "linear", 6),
    ("Room7", "linear", 7),
    ("Room8", "linear", 8),
)

SYSTEM_PROMPT = f"""You are the Room Geometry Designer for Death Metal Cat, a 2D side-scroller. You compose a single room's traversal layout as a strict sequence of geometry pieces, left to right, for the city biome's room-progression framework.

MOVEMENT CONSTRAINTS (hard bounds, derived from Cayde's actual current tuning -- not guesses):
- Max horizontal jump distance: {MAX_JUMP_DISTANCE:.1f} units (running jump, full speed maintained through the arc)
- Max jump height: {MAX_JUMP_HEIGHT:.1f} units
- Wall-jump horizontal reach: {WALL_JUMP_MAX_DISTANCE:.1f} units
- Wall-jump vertical reach: {WALL_JUMP_MAX_HEIGHT:.1f} units
- Dodge distance: {DODGE_DISTANCE:.1f} units (grounded i-frame horizontal burst, not a jump extender)

Every "gap" width MUST be <= the max horizontal jump distance above. Every "ledge_step" height_up MUST be <= the max jump height above. Every "wall_jump_shaft" wall_height MUST be <= the wall-jump vertical reach above. Never exceed these -- there is no move in Cayde's moveset that crosses a wider/taller obstacle than these bounds allow, so a violation makes the room physically uncompletable, not just harder.

PIECE VOCABULARY (the ONLY building blocks you may use -- do not invent new piece types or extra params):
- flat_run {{ length }} -- flat walkable floor. length > 0.
- ledge_step {{ height_up, length }} -- a step up onto a higher ledge. height_up <= max jump height above. length > 0 is the ledge's own horizontal footprint.
- gap {{ width }} -- a horizontal gap the player must jump across. width <= max horizontal jump distance above.
- wall_jump_shaft {{ wall_height }} -- a vertical traversal section climbed via wall-slide + wall-jump. wall_height <= wall-jump vertical reach above. No width param -- treat its horizontal footprint as a fixed {WALL_JUMP_SHAFT_NOMINAL_WIDTH:.0f} units for room-length budgeting.
- drop_down {{ height_down, fall_damage }} -- a drop to a lower level. height_down > 0, any value (no upper bound). fall_damage is an optional bool, default false. No width param -- treat its horizontal footprint as a fixed {DROP_DOWN_NOMINAL_WIDTH:.0f} units for room-length budgeting.
- enemy_arena {{ width }} -- a flat stretch sized for combat. width > 0. This is where spawn_markers get placed.

DESIGN LOGIC:
- Compose pieces end-to-end, left to right. sequence_index starts at 0 and increments by 1 with no gaps or repeats.
- Loosely respect target_room_length as the sum of each piece's horizontal footprint (flat_run.length, ledge_step.length, gap.width, enemy_arena.width, {WALL_JUMP_SHAFT_NOMINAL_WIDTH:.0f} per wall_jump_shaft, {DROP_DOWN_NOMINAL_WIDTH:.0f} per drop_down) -- "loosely" means roughly within 20% of the target, not exact.
- Higher difficulty_tier (1-8) rooms should lean toward MORE gap and wall_jump_shaft pieces (harder traversal) and denser enemy_arena placement (more spawn markers, tighter spacing, more EnemySpawn relative to PickupSpawn). Lower tiers should lean toward flat_run and gentle ledge_step pieces, with fewer/lighter enemy_arena sections.
- room_role "linear" is a single straightforward path. "branch_a"/"branch_b" are one of a pair of alternate parallel paths that both funnel back into the same next room -- make these feel like genuinely different routes from each other (e.g. one leans traversal-heavy with gaps/wall-jumps, the other leans combat-heavy with enemy_arena), not near-identical layouts.
- Place spawn_markers only within/adjacent to enemy_arena or flat_run pieces, never mid-gap or mid-wall_jump_shaft. marker_type is "EnemySpawn" for combat encounters or "PickupSpawn" for rewards. after_piece_index must reference one of the piece list's actual sequence_index values.

OUTPUT FORMAT (strict -- this is the entire response):
Return ONLY a single JSON object. No preamble, no trailing commentary, no markdown code fences. Exactly this shape:
{{"room_id": "<id>", "pieces": [{{"type": "<piece type>", "params": {{...}}, "sequence_index": 0}}], "spawn_markers": [{{"marker_id": "<id>", "marker_type": "EnemySpawn", "after_piece_index": 0}}]}}"""


def build_user_prompt(
    room_id: str,
    difficulty_tier: int,
    room_role: str,
    target_room_length: float,
    validation_feedback: str | None = None,
) -> str:
    lines = [
        f"room_id: {room_id}",
        f"difficulty_tier: {difficulty_tier} (1-8)",
        f"room_role: {room_role}",
        f"target_room_length: {target_room_length:.0f} units",
    ]
    if validation_feedback:
        lines.append(
            "Your previous attempt failed deterministic validation with this exact error -- "
            "fix the specific piece/transition it names, do not just regenerate blindly:\n"
            f"{validation_feedback}"
        )
    lines.append("Generate the room layout now, per the movement constraints and strict JSON output format.")
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


def generate_room(
    client: Anthropic,
    room_id: str,
    difficulty_tier: int,
    room_role: str,
    target_room_length: float,
    validation_feedback: str | None = None,
) -> dict:
    if room_role not in ROOM_ROLES:
        raise ValueError(f"room_role must be one of {ROOM_ROLES}, got {room_role!r}")
    if not 1 <= difficulty_tier <= 8:
        raise ValueError(f"difficulty_tier must be 1-8, got {difficulty_tier!r}")

    response = client.messages.create(
        model=MODEL,
        max_tokens=2000,
        system=SYSTEM_PROMPT,
        messages=[{
            "role": "user",
            "content": build_user_prompt(room_id, difficulty_tier, room_role, target_room_length, validation_feedback),
        }],
    )
    raw_text = next(block.text for block in response.content if block.type == "text")

    try:
        room = _extract_json_object(raw_text)
    except json.JSONDecodeError as exc:
        raise ValueError(
            f"Model did not return valid JSON for room_id={room_id!r}. Raw response:\n{raw_text}"
        ) from exc

    for key in ("room_id", "pieces", "spawn_markers"):
        if key not in room:
            raise ValueError(f"Model JSON missing required key {key!r}: {room}")

    return room


def validate_room(room: dict) -> tuple[bool, str | None]:
    """Deterministic, non-LLM validation -- walks pieces in sequence_index order and confirms
    every transition is physically reachable given the movement constraints, plus basic
    structural sanity (contiguous sequence_index, valid spawn_marker references). Returns
    (True, None) if the room is valid, or (False, "<exactly which piece/transition failed>")
    otherwise. Never trusts the model's own arithmetic."""
    pieces = room.get("pieces", [])
    if not pieces:
        return False, "room has no pieces"

    try:
        sorted_pieces = sorted(pieces, key=lambda p: p["sequence_index"])
    except KeyError:
        return False, "one or more pieces is missing sequence_index"

    expected_indices = list(range(len(sorted_pieces)))
    actual_indices = [p["sequence_index"] for p in sorted_pieces]
    if actual_indices != expected_indices:
        return False, f"sequence_index values are not contiguous starting at 0: got {actual_indices}, expected {expected_indices}"

    for i, piece in enumerate(sorted_pieces):
        ptype = piece.get("type")
        params = piece.get("params", {})
        idx = piece["sequence_index"]

        if ptype not in PIECE_TYPES:
            return False, f"piece at sequence_index={idx} has unknown type {ptype!r} (must be one of {PIECE_TYPES})"

        if ptype == "gap":
            width = params.get("width")
            if not isinstance(width, (int, float)):
                return False, f"gap at sequence_index={idx} missing numeric 'width'"
            if width > MAX_JUMP_DISTANCE:
                return False, (
                    f"gap at sequence_index={idx} has width={width:.1f}, which exceeds "
                    f"MAX_JUMP_DISTANCE={MAX_JUMP_DISTANCE:.1f} -- physically uncrossable"
                )
            if width <= 0:
                return False, f"gap at sequence_index={idx} has non-positive width={width}"

        elif ptype == "ledge_step":
            height_up = params.get("height_up")
            length = params.get("length")
            if not isinstance(height_up, (int, float)) or not isinstance(length, (int, float)):
                return False, f"ledge_step at sequence_index={idx} missing numeric 'height_up'/'length'"
            if height_up > MAX_JUMP_HEIGHT:
                return False, (
                    f"ledge_step at sequence_index={idx} has height_up={height_up:.1f}, which exceeds "
                    f"MAX_JUMP_HEIGHT={MAX_JUMP_HEIGHT:.1f} -- physically unreachable"
                )
            if length <= 0:
                return False, f"ledge_step at sequence_index={idx} has non-positive length={length}"

        elif ptype == "wall_jump_shaft":
            wall_height = params.get("wall_height")
            if not isinstance(wall_height, (int, float)):
                return False, f"wall_jump_shaft at sequence_index={idx} missing numeric 'wall_height'"
            if wall_height > WALL_JUMP_MAX_HEIGHT:
                return False, (
                    f"wall_jump_shaft at sequence_index={idx} has wall_height={wall_height:.1f}, which exceeds "
                    f"WALL_JUMP_MAX_HEIGHT={WALL_JUMP_MAX_HEIGHT:.1f} -- physically unreachable"
                )
            if wall_height <= 0:
                return False, f"wall_jump_shaft at sequence_index={idx} has non-positive wall_height={wall_height}"

        elif ptype == "drop_down":
            height_down = params.get("height_down")
            if not isinstance(height_down, (int, float)):
                return False, f"drop_down at sequence_index={idx} missing numeric 'height_down'"
            if height_down <= 0:
                return False, f"drop_down at sequence_index={idx} has non-positive height_down={height_down}"

        elif ptype == "flat_run":
            length = params.get("length")
            if not isinstance(length, (int, float)):
                return False, f"flat_run at sequence_index={idx} missing numeric 'length'"
            if length <= 0:
                return False, f"flat_run at sequence_index={idx} has non-positive length={length}"

        elif ptype == "enemy_arena":
            width = params.get("width")
            if not isinstance(width, (int, float)):
                return False, f"enemy_arena at sequence_index={idx} missing numeric 'width'"
            if width <= 0:
                return False, f"enemy_arena at sequence_index={idx} has non-positive width={width}"

    valid_indices = set(actual_indices)
    for marker in room.get("spawn_markers", []):
        after_idx = marker.get("after_piece_index")
        if after_idx not in valid_indices:
            return False, (
                f"spawn_marker {marker.get('marker_id')!r} has after_piece_index={after_idx}, "
                f"which is not a valid sequence_index (valid: {sorted(valid_indices)})"
            )
        marker_type = marker.get("marker_type")
        if marker_type not in ("EnemySpawn", "PickupSpawn"):
            return False, f"spawn_marker {marker.get('marker_id')!r} has invalid marker_type={marker_type!r}"
        # Markers must sit within/adjacent to an enemy_arena or flat_run piece, never mid-gap
        # or mid-wall_jump_shaft (those are pure traversal, not valid spawn locations).
        referenced_piece = next(p for p in sorted_pieces if p["sequence_index"] == after_idx)
        if referenced_piece["type"] not in ("enemy_arena", "flat_run"):
            return False, (
                f"spawn_marker {marker.get('marker_id')!r} references sequence_index={after_idx} "
                f"(type={referenced_piece['type']!r}) -- markers may only sit within/adjacent to "
                f"enemy_arena or flat_run pieces"
            )

    return True, None


def compute_room_footprint(room: dict) -> float:
    """Sums each piece's horizontal footprint -- the same accounting the system prompt asks the
    model to loosely respect against target_room_length (explicit width/length for
    flat_run/ledge_step/gap/enemy_arena, the fixed nominal footprint for wall_jump_shaft/drop_down
    since those pieces have no width param of their own)."""
    total = 0.0
    for piece in room.get("pieces", []):
        ptype, params = piece["type"], piece["params"]
        if ptype in ("flat_run", "ledge_step"):
            total += params["length"]
        elif ptype in ("gap", "enemy_arena"):
            total += params["width"]
        elif ptype == "wall_jump_shaft":
            total += WALL_JUMP_SHAFT_NOMINAL_WIDTH
        elif ptype == "drop_down":
            total += DROP_DOWN_NOMINAL_WIDTH
    return total


# Extra attempts if the model's output fails deterministic validation -- regeneration is fed
# the exact validation failure as feedback (see build_user_prompt), same "tell it what went
# wrong, don't just re-roll blind" pattern as quip_generator.py's duplicate-line retries.
MAX_VALIDATION_RETRIES = 3


def generate_valid_room(
    client: Anthropic,
    room_id: str,
    difficulty_tier: int,
    room_role: str,
    target_room_length: float,
) -> dict:
    feedback: str | None = None
    room: dict = {}
    for attempt in range(MAX_VALIDATION_RETRIES + 1):
        room = generate_room(client, room_id, difficulty_tier, room_role, target_room_length, validation_feedback=feedback)
        is_valid, error = validate_room(room)
        if is_valid:
            return room
        print(f"  [{room_id}] validation failed (attempt {attempt + 1}): {error}", file=sys.stderr)
        feedback = error

    raise ValueError(f"Room for {room_id!r} still failed validation after {MAX_VALIDATION_RETRIES} retries: {feedback}")


def run_batch_rooms(client: Anthropic, target_room_length: float, output_dir: str) -> list[dict]:
    """Generates+validates all 9 real rooms (see ALL_ROOMS) with their correct role/tier, saving
    each as its own Tools/room_geometry_<RoomID>.json, and returns one summary row per room
    (room_id, piece_count, footprint, valid, detail) for the caller to print as a table. A room
    that still fails validation after MAX_VALIDATION_RETRIES is recorded as a failed row rather
    than aborting the rest of the batch, same "one bad item doesn't sink the batch" spirit as
    quip_generator.py's run_batch."""
    summary: list[dict] = []
    for room_id, room_role, tier in ALL_ROOMS:
        print(f"  [{room_id}] generating ({room_role}, tier {tier})...", file=sys.stderr)
        try:
            room = generate_valid_room(client, room_id, tier, room_role, target_room_length)
        except (AnthropicError, ValueError) as exc:
            summary.append({"room_id": room_id, "piece_count": 0, "footprint": 0.0, "valid": False, "detail": str(exc)})
            continue

        out_path = os.path.join(output_dir, f"room_geometry_{room_id}.json")
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(room, f, indent=2)

        summary.append({
            "room_id": room_id,
            "piece_count": len(room["pieces"]),
            "footprint": compute_room_footprint(room),
            "valid": True,
            "detail": out_path,
        })
        print(f"  [{room_id}] OK -> {out_path}", file=sys.stderr)

    return summary


def print_summary_table(summary: list[dict]) -> None:
    header = f"{'Room':<10} {'Pieces':>7} {'Footprint':>10} {'Valid':>6}  Detail"
    print(header)
    print("-" * len(header))
    for row in summary:
        valid_str = "yes" if row["valid"] else "NO"
        footprint_str = f"{row['footprint']:.0f}" if row["valid"] else "-"
        print(f"{row['room_id']:<10} {row['piece_count']:>7} {footprint_str:>10} {valid_str:>6}  {row['detail']}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Room Geometry Designer -- Death Metal Cat's room-layout agent (standalone, calls Claude API directly)."
    )
    parser.add_argument("--room", help="Room id, e.g. Room2, Room4A (required unless --batch-rooms).")
    parser.add_argument("--tier", type=int, help="Difficulty tier, 1-8 (required unless --batch-rooms).")
    parser.add_argument("--role", choices=ROOM_ROLES, help="linear (single path) or branch_a/branch_b (one of a pair of alternate paths) (required unless --batch-rooms).")
    parser.add_argument("--length", type=float, required=True, help="Rough total width budget for the room, in units (loosely respected). Applies to every room in --batch-rooms mode.")
    parser.add_argument("--batch-rooms", action="store_true", help="Generate all 9 real rooms (see ALL_ROOMS) with their correct role/tier, saving each to its own Tools/room_geometry_<RoomID>.json, and print a summary table instead of a single JSON blob.")
    parser.add_argument("--output-dir", default=os.path.dirname(os.path.abspath(__file__)), help="Directory to save each room's JSON in --batch-rooms mode (default: this script's own directory).")
    args = parser.parse_args()

    if not args.batch_rooms and not (args.room and args.tier and args.role):
        parser.error("--room, --tier, and --role are required unless --batch-rooms is set")

    try:
        client = Anthropic()
    except AnthropicError as exc:
        print(f"Failed to initialize Anthropic client: {exc}", file=sys.stderr)
        print("Set the ANTHROPIC_API_KEY environment variable and try again.", file=sys.stderr)
        return 1

    if args.batch_rooms:
        summary = run_batch_rooms(client, args.length, args.output_dir)
        print_summary_table(summary)
        return 1 if any(not row["valid"] for row in summary) else 0

    try:
        room = generate_valid_room(client, args.room, args.tier, args.role, args.length)
    except AnthropicError as exc:
        print(f"Claude API error: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    print(json.dumps(room, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
