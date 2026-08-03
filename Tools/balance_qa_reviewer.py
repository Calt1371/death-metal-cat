#!/usr/bin/env python3
"""
Balance/QA Reviewer -- last agent in the roster (GDD Section 8.2). Given a room's difficulty
tier, its Room Geometry Designer piece-sequence JSON, and its Level & Encounter Designer
marker-population JSON, runs five deterministic balance checks and flags anything that fails
them.

ARCHITECTURE: deliberately almost entirely deterministic. Every flag decision and every number
in "detail" is plain Python math against real, live-queried project constants -- never LLM
judgment. The LLM is called exactly once, AFTER every flag is already final, only to write a
short rationale sentence per flag using nothing but the numbers already in "detail". It cannot
create, remove, or re-decide a flag.

REAL COMBAT CONSTANTS (queried live from BP_DeathMetalCat/BP_EnemyBase's CDOs via the editor
scripting bridge on 2026-07-27, not hardcoded from memory or guessed -- see each constant's own
comment below):
    Player MaxHealth = 100.0, Player MaxMoveSpeed = 600.0 (matches live CharacterMovement.MaxWalkSpeed)
    Enemy ContactDamage = 10.0, Enemy ContactDamageCooldown = 1.0s, Enemy MaxHealth = 100.0

NOTE ON THE INPUT PREMISE: Level & Encounter Designer already exists and has real committed
output for all 9 rooms (Tools/encounter_population_<RoomID>.json) -- it isn't actually unbuilt.
This tool still defaults to that real path by convention, but --population-json lets you point
it at a hand-written example instead (as this task's proof run does for Room1), so it works
identically whether the population data is real or hand-authored.

Usage:
    python balance_qa_reviewer.py --room Room1 --tier 1
    python balance_qa_reviewer.py --room Room1 --tier 1 --population-json my_example.json
    python balance_qa_reviewer.py --room Room4A --tier 4 --previous-score 3
"""

import argparse
import json
import math
import os
import sys

from anthropic import Anthropic, AnthropicError

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

import room_geometry_designer as geo_designer

MODEL = "claude-sonnet-4-6"

# ---- Real combat constants (live-queried 2026-07-27, see docstring) ------------------------
PLAYER_MAX_HEALTH = 100.0
PLAYER_MAX_MOVE_SPEED = 600.0
ENEMY_CONTACT_DAMAGE = 10.0
ENEMY_CONTACT_DAMAGE_COOLDOWN = 1.0

# ---- Stated assumptions / thresholds (deliberately simple, tune freely) --------------------
# Rule 1: total raw damage exposure crossing the room is flagged if it exceeds this fraction of
# MaxHealth -- "150%" per this task's own spec, not a guess.
SURVIVABILITY_CEILING_MULTIPLIER = 1.5

# Rule 3: a gap counts as "near max jump distance" (and therefore too hard for tier 1-2) once its
# width reaches this fraction of room_geometry_designer.MAX_JUMP_DISTANCE. Placeholder, tune freely.
NEAR_MAX_JUMP_GAP_THRESHOLD = 0.85

# Rule 5: minimum enemy_arena width, in units, needed per enemy assigned to it -- a simple
# "personal space" floor below which enemies are packed too densely to fairly react to.
# Placeholder value (roughly 1x MeleeRange), tune freely.
MIN_WIDTH_PER_ENEMY = 100.0


def load_geometry(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_population(path: str) -> list[dict]:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data["populations"]


def compute_danger_score(population: list[dict]) -> int:
    """Simple weighted enemy count (weight = 1 per enemy instance) -- a deliberately plain
    "danger score" for the monotonicity check, not a fully-modeled difficulty metric."""
    return sum(p["enemy_count"] for p in population if p["population"] == "enemy")


def make_flag(rule: str, severity: str, detail: str) -> dict:
    return {"rule": rule, "severity": severity, "detail": detail, "rationale": None}


def rule_survivability_ceiling(geometry: dict, population: list[dict]) -> dict | None:
    """Total raw damage exposure = enemy count x contact damage x hits-per-enemy, where
    hits-per-enemy is how many times ONE enemy's contact-damage cooldown could tick over during
    the room's estimated traversal time (footprint / player move speed) -- a deliberately
    worst-case ceiling (every enemy in sustained contact the whole traversal), not an average-case
    simulation. Flagged if this exceeds SURVIVABILITY_CEILING_MULTIPLIER x MaxHealth."""
    footprint = geo_designer.compute_room_footprint(geometry)
    traversal_time = footprint / PLAYER_MAX_MOVE_SPEED
    hits_per_enemy = math.floor(traversal_time / ENEMY_CONTACT_DAMAGE_COOLDOWN) + 1
    total_enemy_count = sum(p["enemy_count"] for p in population if p["population"] == "enemy")
    total_exposure = total_enemy_count * ENEMY_CONTACT_DAMAGE * hits_per_enemy
    ceiling = SURVIVABILITY_CEILING_MULTIPLIER * PLAYER_MAX_HEALTH

    if total_exposure > ceiling:
        detail = (
            f"footprint={footprint:.0f}u, traversal_time={traversal_time:.2f}s (at MaxMoveSpeed={PLAYER_MAX_MOVE_SPEED:.0f}), "
            f"hits_per_enemy={hits_per_enemy} (floor(traversal_time/{ENEMY_CONTACT_DAMAGE_COOLDOWN:.1f}s)+1), "
            f"total_enemy_count={total_enemy_count}, contact_damage={ENEMY_CONTACT_DAMAGE:.0f}, "
            f"total_exposure={total_exposure:.0f} > ceiling={ceiling:.0f} ({SURVIVABILITY_CEILING_MULTIPLIER*100:.0f}% of MaxHealth={PLAYER_MAX_HEALTH:.0f})"
        )
        return make_flag("survivability_ceiling", "fail", detail)
    return None


def rule_curve_monotonicity(danger_score: int, tier: int, previous_score: float | None) -> dict | None:
    """Compares this room's danger score to the previous tier's, given explicitly via
    --previous-score (this tool reviews one room per invocation, so it can't see prior rooms on
    its own). Not evaluable in isolation -- returns None (not a pass, not a fail) if omitted."""
    if previous_score is None:
        return None
    if danger_score < previous_score:
        detail = f"this room's danger_score={danger_score} (tier {tier}) is lower than the previous tier's danger_score={previous_score}"
        return make_flag("curve_monotonicity", "warning", detail)
    return None


def rule_geometry_tier_alignment(geometry: dict, tier: int) -> dict | None:
    """Tier 1-2 rooms shouldn't contain "hard" traversal pieces: any wall_jump_shaft at all, or a
    gap whose width reaches NEAR_MAX_JUMP_GAP_THRESHOLD of the current MAX_JUMP_DISTANCE."""
    if tier > 2:
        return None

    hard_pieces = []
    for piece in geometry["pieces"]:
        if piece["type"] == "wall_jump_shaft":
            hard_pieces.append(f"wall_jump_shaft@idx{piece['sequence_index']} (wall_height={piece['params']['wall_height']:.0f}u)")
        elif piece["type"] == "gap":
            width = piece["params"]["width"]
            threshold = NEAR_MAX_JUMP_GAP_THRESHOLD * geo_designer.MAX_JUMP_DISTANCE
            if width >= threshold:
                pct = width / geo_designer.MAX_JUMP_DISTANCE * 100
                hard_pieces.append(f"gap@idx{piece['sequence_index']} (width={width:.0f}u, {pct:.0f}% of MAX_JUMP_DISTANCE={geo_designer.MAX_JUMP_DISTANCE:.0f}u)")

    if hard_pieces:
        detail = f"tier={tier} (<=2) room contains hard piece(s): {'; '.join(hard_pieces)}"
        return make_flag("geometry_tier_alignment", "fail", detail)
    return None


def rule_pickup_floor(population: list[dict]) -> dict | None:
    """Flags a room that has enemy markers but zero populated pickup markers."""
    enemy_marker_count = sum(1 for p in population if p["population"] == "enemy")
    pickup_marker_count = sum(1 for p in population if p["population"] == "pickup")
    if enemy_marker_count > 0 and pickup_marker_count == 0:
        detail = f"enemy_marker_count={enemy_marker_count}, pickup_marker_count=0"
        return make_flag("pickup_floor", "warning", detail)
    return None


def rule_arena_density_cap(geometry: dict, population: list[dict]) -> list[dict]:
    """For every enemy_arena piece, sums the enemy_count of every population marker whose
    after_piece_index (from the GEOMETRY json's spawn_markers, cross-referenced by marker_id --
    the population json itself has no position data, see prior schema audit) points at it, then
    flags if that arena's width-per-enemy falls below MIN_WIDTH_PER_ENEMY."""
    marker_to_piece_idx = {m["marker_id"]: m["after_piece_index"] for m in geometry["spawn_markers"]}
    arena_widths = {p["sequence_index"]: p["params"]["width"] for p in geometry["pieces"] if p["type"] == "enemy_arena"}

    enemies_per_arena: dict[int, int] = {}
    for p in population:
        if p["population"] != "enemy":
            continue
        piece_idx = marker_to_piece_idx.get(p["marker_id"])
        if piece_idx in arena_widths:
            enemies_per_arena[piece_idx] = enemies_per_arena.get(piece_idx, 0) + p["enemy_count"]

    flags = []
    for idx, width in arena_widths.items():
        count = enemies_per_arena.get(idx, 0)
        if count == 0:
            continue
        width_per_enemy = width / count
        if width_per_enemy < MIN_WIDTH_PER_ENEMY:
            detail = f"enemy_arena@idx{idx} width={width:.0f}u, enemies_assigned={count}, width_per_enemy={width_per_enemy:.1f}u < MIN_WIDTH_PER_ENEMY={MIN_WIDTH_PER_ENEMY:.0f}u"
            flags.append(make_flag("arena_density_cap", "fail", detail))
    return flags


SYSTEM_PROMPT = """You are the Balance/QA Reviewer for Death Metal Cat, a 2D side-scroller (GDD Section 8.2). You are given a list of ALREADY-DECIDED flags -- every flag, its severity, and the numbers in its "detail" string were computed by deterministic Python, not by you. Your only job is to write ONE short, specific rationale sentence per flag, using ONLY the numbers already present in that flag's detail string. You may NOT invent new numbers, change any given number, add or remove flags, or second-guess whether a flag should exist -- that decision is final and not yours to make.

OUTPUT FORMAT (strict -- this is the entire response):
Return ONLY a single JSON array, one object per input flag, in the SAME ORDER as given. No preamble, no trailing commentary, no markdown code fences. Exactly this shape:
[{"index": 0, "rationale": "<one sentence, citing the actual numbers from that flag's detail>"}, ...]"""


def build_rationale_prompt(flags: list[dict]) -> str:
    lines = ["Write one rationale per flag below, in order, citing its exact numbers:"]
    for i, f in enumerate(flags):
        lines.append(f"{i}. rule={f['rule']!r} severity={f['severity']!r} detail={f['detail']!r}")
    return "\n".join(lines)


def _extract_json_array(raw: str) -> list:
    text = raw.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1] if "\n" in text else text[3:]
        if text.endswith("```"):
            text = text[:-3]
        text = text.strip()
        if text.lower().startswith("json"):
            text = text[4:].strip()
    return json.loads(text)


def attach_rationales(client: Anthropic, flags: list[dict]) -> None:
    """Mutates each flag's "rationale" in place. A pure formatting step -- if this fails, every
    flag/number computed above is already final and correct regardless; only the prose is missing."""
    if not flags:
        return

    response = client.messages.create(
        model=MODEL,
        max_tokens=2000,
        system=SYSTEM_PROMPT,
        messages=[{"role": "user", "content": build_rationale_prompt(flags)}],
    )
    raw_text = next(block.text for block in response.content if block.type == "text")
    rationales = _extract_json_array(raw_text)

    if len(rationales) != len(flags):
        raise ValueError(f"Expected {len(flags)} rationales, got {len(rationales)}")

    for entry in rationales:
        flags[entry["index"]]["rationale"] = entry["rationale"]


def review_room(room_id: str, tier: int, geometry: dict, population: list[dict], previous_score: float | None) -> tuple[dict, int]:
    flags = []

    f = rule_survivability_ceiling(geometry, population)
    if f:
        flags.append(f)

    danger_score = compute_danger_score(population)
    f = rule_curve_monotonicity(danger_score, tier, previous_score)
    if f:
        flags.append(f)

    f = rule_geometry_tier_alignment(geometry, tier)
    if f:
        flags.append(f)

    f = rule_pickup_floor(population)
    if f:
        flags.append(f)

    flags.extend(rule_arena_density_cap(geometry, population))

    passed = not any(f["severity"] == "fail" for f in flags)
    return {"room_id": room_id, "flags": flags, "passed": passed}, danger_score


def main() -> int:
    parser = argparse.ArgumentParser(description="Balance/QA Reviewer -- runs five deterministic balance checks against one room's real geometry + population data.")
    parser.add_argument("--room", required=True, help="Room id, e.g. Room1, Room4A.")
    parser.add_argument("--tier", type=int, required=True, help="Difficulty tier, 1-8.")
    parser.add_argument("--geometry-json", default=None, help="Path to the Room Geometry Designer JSON. Defaults to Tools/room_geometry_<room>.json.")
    parser.add_argument("--population-json", default=None, help="Path to the Level & Encounter Designer population JSON (or a hand-written example in the same shape). Defaults to Tools/encounter_population_<room>.json.")
    parser.add_argument("--previous-score", type=float, default=None, help="The previous tier's danger_score, for the curve-monotonicity check. Omit if this is the first room being reviewed.")
    parser.add_argument("--no-llm", action="store_true", help="Skip the rationale-writing API call (flags are unaffected, only prose is omitted).")
    args = parser.parse_args()

    geometry_path = args.geometry_json or os.path.join(TOOLS_DIR, f"room_geometry_{args.room}.json")
    population_path = args.population_json or os.path.join(TOOLS_DIR, f"encounter_population_{args.room}.json")

    geometry = load_geometry(geometry_path)
    population = load_population(population_path)

    result, danger_score = review_room(args.room, args.tier, geometry, population, args.previous_score)

    if not args.no_llm:
        try:
            client = Anthropic()
            attach_rationales(client, result["flags"])
        except (AnthropicError, ValueError, KeyError) as exc:
            print(f"WARNING: rationale generation failed ({exc}) -- flags above are unaffected, only prose is missing.", file=sys.stderr)

    print(json.dumps(result, indent=2))
    print(f"\ndanger_score={danger_score} -- pass as --previous-score {danger_score} when reviewing the NEXT room in tier order.", file=sys.stderr)

    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
