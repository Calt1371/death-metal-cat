#!/usr/bin/env python3
"""
stress_test_room.py

Stress Tester -- standalone agent from the Death Metal Cat GDD (Section 4.2 agent roster). See
Docs/stress_tester_scope.md for the full scope this was built against.

Answers two questions Reachability Verifier's binary pass/fail doesn't:
  1. Is a room's PASS still trustworthy right now, or have the movement constants it was checked
     against drifted since biome_spec_<Biome>.json was generated?
  2. Is a PASS actually safe to trust on formula alone, or does it need a human to confirm it
     in-game (the OneWayPlatform11->15 lesson -- the formula can say reachable while the tightest
     cases still deserve a real playtest before anyone builds on top of that assumption)?

Reuses, never reimplements:
  - foundation_extractor.compute_gaps() (Part 2's shared gap/reachability math)
  - foundation_extractor.compute_derived_movement_constants() (this session's shared derived-
    constants formula -- one copy, called by both Foundation Extractor's own measurement pass and
    this script)
  - foundation_extractor.query_live_movement_constants() (Part 1's live-CDO query)
  - reachability_verifier.query_live_platform_data() (Part 2's live platform query)

Room status gate: checks Tools/room_status.json[--biome][--room] BEFORE touching the editor at
all. A room that isn't "finished" is skipped, not stress-tested -- see Docs/stress_tester_scope.md
for why (an in-progress room's "failures" aren't bugs, they're just unfinished work).

This agent only reports. It never fixes a drifted constant, never rewrites biome_spec.json, and
never flips a room's status -- those are human decisions, consistent with the GDD's own "final
review and hand-tweaking is a human gate" principle (Section 4.3).

Usage:
    python stress_test_room.py --room Room1 --biome AssassinCity
    python stress_test_room.py --room Room3 --biome AssassinCity   # will report SKIPPED
"""

import argparse
import json
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

import foundation_extractor as fe
import reachability_verifier as rv

DEFAULT_ROOM_STATUS_PATH = os.path.join(TOOLS_DIR, "room_status.json")

# Same category of check as Part 1's MOVEMENT_CONSTANT_MISMATCH_TOLERANCE_FRACTION (is this delta
# real tuning or float noise) against the same underlying constants -- reusing the exact value,
# not inventing a second number for what's conceptually the same question.
DRIFT_TOLERANCE_FRACTION = fe.MOVEMENT_CONSTANT_MISMATCH_TOLERANCE_FRACTION


def get_room_status(room_status_path: str, biome: str, room: str) -> str:
    """Reads Tools/room_status.json[biome][room], defaulting to "not_started" if the room, or the
    whole biome, has no entry -- the safe direction, so a room never gets stress-tested by
    accident just because it (or its biome) was never added to the file."""
    if not os.path.exists(room_status_path):
        return "not_started"
    with open(room_status_path, "r", encoding="utf-8") as f:
        room_status = json.load(f)
    return room_status.get(biome, {}).get(room, "not_started")


def diff_movement_constants(recorded: dict, current: dict, tolerance_fraction: float) -> list:
    """Deterministic, non-LLM comparison -- for every constant recorded in biome_spec.json's
    movement_constants, checks it against a fresh live-derived value using the SAME relative-
    tolerance philosophy Part 1 established (percentage delta, not absolute units -- see
    MOVEMENT_CONSTANT_MISMATCH_TOLERANCE_FRACTION's own comment for why). This is a different
    comparison than Part 1's compare_constants() (which diffs C++ header defaults vs. live CDO) --
    this one diffs a room's RECORDED spec-generation-time snapshot vs. right now."""
    mismatches = []
    for key, recorded_value in recorded.items():
        if key not in current or not isinstance(recorded_value, (int, float)):
            continue
        current_value = current[key]
        delta = abs(recorded_value - current_value)
        relative_delta = delta / abs(recorded_value) if recorded_value != 0 else (0.0 if delta == 0 else float("inf"))
        if relative_delta > tolerance_fraction:
            mismatches.append({
                "constant": key,
                "recorded_in_spec": recorded_value,
                "live_now": current_value,
                "delta": delta,
                "relative_delta": relative_delta,
            })
    return mismatches


def categorize_gaps(gaps: list) -> dict:
    """No new computation -- compute_gaps() already classifies every gap "tight"/"comfortable"
    and provides reachable_by. Re-splits Reachability Verifier's binary PASS bucket into
    comfortable_pass vs. tight_pass_needs_playtest (the OneWayPlatform11->15 category of issue --
    formula says reachable, margin is thin, confirm in-game before trusting it), and keeps FAIL
    unchanged from Reachability Verifier's own definition (reachable_by empty)."""
    comfortable_pass, tight_pass_needs_playtest, fail = [], [], []
    for g in gaps:
        if not g["reachable_by"]:
            fail.append(g)
        elif g["tolerance"] == "tight":
            tight_pass_needs_playtest.append(g)
        else:
            comfortable_pass.append(g)
    return {
        "comfortable_pass": comfortable_pass,
        "tight_pass_needs_playtest": tight_pass_needs_playtest,
        "fail": fail,
    }


def diff_gap_verdicts(recorded_gaps: list, fresh_gaps: list) -> list:
    """Matches gaps by (from_label, to_label) and reports any that changed reachable_by/tolerance
    between the recorded spec and a fresh recompute -- the specific, actionable signal ("this gap
    is no longer safe") rather than a vague "some constant drifted"."""
    recorded_by_pair = {(g["from_label"], g["to_label"]): g for g in recorded_gaps}
    flipped = []
    for fresh in fresh_gaps:
        pair = (fresh["from_label"], fresh["to_label"])
        recorded = recorded_by_pair.get(pair)
        if recorded is None:
            continue  # a genuinely new gap (platform layout changed) -- not a "flip", nothing to compare against
        old_passed = bool(recorded["reachable_by"])
        new_passed = bool(fresh["reachable_by"])
        if old_passed != new_passed or recorded["tolerance"] != fresh["tolerance"]:
            flipped.append({
                "from_label": pair[0],
                "to_label": pair[1],
                "recorded_reachable_by": recorded["reachable_by"],
                "recorded_tolerance": recorded["tolerance"],
                "fresh_reachable_by": fresh["reachable_by"],
                "fresh_tolerance": fresh["tolerance"],
            })
    return flipped


def stress_test_room(room: str, biome: str, biome_spec: dict, room_status_path: str, timeout: float) -> dict:
    status = get_room_status(room_status_path, biome, room)
    if status != "finished":
        return {
            "room": room,
            "biome": biome,
            "status": status,
            "skipped": True,
            "detail": f"SKIPPED -- {room} marked {status}, not stress-tested",
        }

    recorded_constants = biome_spec["movement_constants"]

    # biome_spec.json's stored gameplay_spacing.gaps only ever describes whichever room Foundation
    # Extractor actually measured (biome_spec["source_room"] -- the golden room, Room1) -- movement
    # constants are legitimately biome-wide (Cayde's moveset is the same in every room), but gaps
    # are inherently room-specific, and no OTHER room has a stored gap snapshot anywhere to fall
    # back on. So a stored baseline to diff against only exists when testing the source room
    # itself; every other room always needs a fresh live recompute, drift or not.
    source_room = biome_spec.get("source_room")
    is_source_room = source_room is not None and room.upper() == str(source_room).upper()
    recorded_gaps = biome_spec["gameplay_spacing"]["gaps"] if is_source_room else None

    raw_live_values = fe.query_live_movement_constants(timeout)
    current_constants = fe.compute_derived_movement_constants(raw_live_values)

    drift = diff_movement_constants(recorded_constants, current_constants, DRIFT_TOLERANCE_FRACTION)

    need_fresh_recompute = bool(drift) or not is_source_room
    if need_fresh_recompute:
        platform_data = rv.query_live_platform_data(room, timeout)
        fresh_gaps = fe.compute_gaps(
            platform_data,
            current_constants["max_jump_distance"], current_constants["max_jump_height"],
            current_constants["wall_jump_max_distance"], current_constants["wall_jump_max_height"],
            current_constants["dodge_distance"],
        )
        flipped_gaps = diff_gap_verdicts(recorded_gaps, fresh_gaps) if recorded_gaps is not None else []
        gaps_for_categorization = fresh_gaps
    else:
        flipped_gaps = []
        gaps_for_categorization = recorded_gaps

    categorized = categorize_gaps(gaps_for_categorization)

    return {
        "room": room,
        "biome": biome,
        "status": status,
        "skipped": False,
        "is_source_room": is_source_room,
        "used_fresh_recompute": need_fresh_recompute,
        "drift_detected": bool(drift),
        "drift": drift,
        "flipped_gaps": flipped_gaps,
        "gap_summary": {
            "comfortable_pass": len(categorized["comfortable_pass"]),
            "tight_pass_needs_playtest": len(categorized["tight_pass_needs_playtest"]),
            "fail": len(categorized["fail"]),
        },
        "gaps": categorized,
    }


def print_report(result: dict) -> None:
    print(f"\n=== Stress Tester: {result['room']} ({result['biome']}) ===")
    if result["skipped"]:
        print(result["detail"])
        return

    if result["is_source_room"]:
        print("This is biome_spec.json's own source_room -- it has a stored gap baseline to diff against.")
    else:
        print("Not biome_spec.json's source_room -- no stored gap baseline exists for this room, "
              "always using a fresh live recompute (movement constants are biome-wide and still checked for drift).")

    if result["drift_detected"]:
        print(f"DRIFT DETECTED -- {len(result['drift'])} constant(s) moved beyond {DRIFT_TOLERANCE_FRACTION*100:.1f}% since biome_spec.json was generated:")
        for d in result["drift"]:
            print(f"  {d['constant']}: recorded={d['recorded_in_spec']:.2f}, live_now={d['live_now']:.2f} "
                  f"({d['relative_delta']*100:.2f}% relative)")
        if result["flipped_gaps"]:
            print(f"\n{len(result['flipped_gaps'])} gap(s) changed verdict as a result:")
            for g in result["flipped_gaps"]:
                print(f"  {g['from_label']} -> {g['to_label']}: "
                      f"was reachable_by={g['recorded_reachable_by']} ({g['recorded_tolerance']}), "
                      f"now reachable_by={g['fresh_reachable_by']} ({g['fresh_tolerance']})")
        elif result["is_source_room"]:
            print("\nNo gap verdicts actually changed despite the drift (still within margin either way).")
        # else (not source_room): no stored baseline exists for this room at all, so there's
        # nothing to report a "flip" against -- the fresh gap categorization below is the report.
    else:
        print("No constant drift detected -- movement constants match biome_spec.json's recorded snapshot.")

    s = result["gap_summary"]
    print(f"\nGap categorization: {s['comfortable_pass']} comfortable_pass, "
          f"{s['tight_pass_needs_playtest']} tight_pass_needs_playtest, {s['fail']} fail")
    for g in result["gaps"]["tight_pass_needs_playtest"]:
        print(f"  NEEDS PLAYTEST: {g['from_label']} -> {g['to_label']} (dist={g['distance']:.1f}, reachable_by={g['reachable_by']})")
    for g in result["gaps"]["fail"]:
        print(f"  FAIL: {g['from_label']} -> {g['to_label']} (dist={g['distance']:.1f})")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Stress Tester -- checks a finished room's constants for drift and flags tight-margin gaps for manual playtest confirmation."
    )
    parser.add_argument("--room", required=True, help="Room to stress-test, e.g. Room1")
    parser.add_argument("--biome", required=True, help="Biome name, e.g. AssassinCity")
    parser.add_argument("--biome-spec-path", help="Override path to biome_spec_<Biome>.json. Defaults to Tools/biome_spec_<biome>.json")
    parser.add_argument("--room-status-path", default=DEFAULT_ROOM_STATUS_PATH, help="Path to room_status.json")
    parser.add_argument("--output", help="Output JSON path. Defaults to Tools/stress_test_<Room>.json")
    parser.add_argument("--timeout", type=float, default=5.0, help="Seconds to wait for the editor to respond to discovery.")
    args = parser.parse_args()

    biome_spec_path = args.biome_spec_path or os.path.join(TOOLS_DIR, f"biome_spec_{args.biome}.json")
    if not os.path.exists(biome_spec_path):
        print(f"ERROR: {biome_spec_path} does not exist -- generate it with foundation_extractor.py first.")
        return 1
    with open(biome_spec_path, "r", encoding="utf-8") as f:
        biome_spec = json.load(f)

    try:
        result = stress_test_room(args.room, args.biome, biome_spec, args.room_status_path, args.timeout)
    except RuntimeError as exc:
        print(f"ERROR: {exc}")
        return 1

    print_report(result)

    output_path = args.output or os.path.join(TOOLS_DIR, f"stress_test_{args.room}.json")
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    print(f"\nWrote {output_path}")

    if result["skipped"]:
        return 0
    needs_attention = result["drift_detected"] or result["gap_summary"]["fail"] > 0 or result["gap_summary"]["tight_pass_needs_playtest"] > 0
    return 1 if needs_attention else 0


if __name__ == "__main__":
    sys.exit(main())
