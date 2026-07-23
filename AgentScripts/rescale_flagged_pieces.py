"""
rescale_flagged_pieces.py

One-off correction for the 9 stored Tools/room_geometry_<RoomID>.json files after Cayde's jump
tuning changed (JumpZVelocity 420->600, AirControl 0.05->1.0, measured 2026-07-23). Re-validating
all 9 rooms against the new constants (see room_geometry_designer.py) confirmed nothing is
unreachable, but every ledge_step and several early-room gaps were tuned against the OLD, much
smaller ceilings and are now trivially easy relative to Cayde's actual current jump range.

Rather than regenerating rooms via the LLM (which would also reshuffle pacing/enemy placement),
this proportionally rescales just the flagged pieces' height_up/width so each piece keeps the
SAME relative difficulty (% of ceiling) it had under the old constants -- preserving existing
room structure/pacing exactly as requested, not attempting to redesign anything.

Ratios (old ceiling -> new ceiling):
    LEDGE_RATIO = NEW_MAX_JUMP_HEIGHT / OLD_MAX_JUMP_HEIGHT     = 183.673.../90.0    = 2.040816...
    GAP_RATIO   = NEW_MAX_JUMP_DISTANCE / OLD_MAX_JUMP_DISTANCE = 647.11/514.2857... = 1.258268...
(OLD_MAX_JUMP_DISTANCE was JUMP_Z_VELOCITY=420's formula-derived value, 600*2*420/980=514.29 --
the pre-2026-07-23 constant this whole biome was actually built against.)

Scaled values are rounded to the nearest 5 units, matching this data's existing granularity (all
stored values are already multiples of 5 or 10). Only the flagged pieces below are touched --
every other piece (gaps/wall_jump_shafts still sitting at a meaningful % of the new ceiling) is
left untouched, since it was not flagged as having drifted into "trivially easy" territory.

This script only edits Tools/room_geometry_<RoomID>.json -- it does NOT touch the live UE level.
Tools/import_room_geometry.py --all-rooms still needs to be re-run afterward (as its own,
separately-confirmed step) to actually rebuild the in-level geometry from these updated files.

Usage:
    python rescale_flagged_pieces.py
"""

import json
import os

TOOLS_DIR = os.path.join(os.path.dirname(__file__), "..", "Tools")

# (room_id, sequence_index, piece_type, param_name, old_value, new_value)
# new_value = round(old_value * ratio, nearest 5).
CHANGES = [
    ("Room1", 3, "ledge_step", "height_up", 40.0, 80.0),
    ("Room1", 5, "gap", "width", 180.0, 225.0),

    ("Room2", 1, "ledge_step", "height_up", 40.0, 80.0),
    ("Room2", 3, "gap", "width", 180.0, 225.0),
    ("Room2", 6, "ledge_step", "height_up", 50.0, 100.0),

    ("Room3", 1, "ledge_step", "height_up", 40.0, 80.0),
    ("Room3", 3, "gap", "width", 280.0, 350.0),
    ("Room3", 5, "ledge_step", "height_up", 60.0, 120.0),
    ("Room3", 9, "gap", "width", 200.0, 250.0),

    ("Room4A", 1, "gap", "width", 320.0, 400.0),
    ("Room4A", 7, "ledge_step", "height_up", 70.0, 140.0),
    ("Room4A", 8, "gap", "width", 280.0, 350.0),

    ("Room5", 2, "gap", "width", 320.0, 400.0),
    ("Room5", 3, "ledge_step", "height_up", 70.0, 140.0),
    ("Room5", 7, "gap", "width", 280.0, 350.0),

    ("Room6", 3, "ledge_step", "height_up", 70.0, 140.0),

    ("Room7", 2, "ledge_step", "height_up", 75.0, 150.0),
]


def main() -> None:
    by_room: dict[str, list[tuple]] = {}
    for change in CHANGES:
        by_room.setdefault(change[0], []).append(change[1:])

    for room_id, room_changes in by_room.items():
        path = os.path.join(TOOLS_DIR, f"room_geometry_{room_id}.json")
        with open(path, "r", encoding="utf-8") as f:
            room = json.load(f)

        pieces_by_index = {p["sequence_index"]: p for p in room["pieces"]}

        for seq_idx, ptype, param_name, old_value, new_value in room_changes:
            piece = pieces_by_index.get(seq_idx)
            if piece is None:
                print(f"  [{room_id}] sequence_index={seq_idx} not found -- SKIPPED")
                continue
            if piece["type"] != ptype:
                print(f"  [{room_id}] sequence_index={seq_idx} type mismatch: expected {ptype!r}, got {piece['type']!r} -- SKIPPED")
                continue
            actual_old = piece["params"].get(param_name)
            if actual_old != old_value:
                print(f"  [{room_id}] sequence_index={seq_idx} {param_name}: expected old value {old_value}, found {actual_old} -- SKIPPED (stale assumption)")
                continue
            piece["params"][param_name] = new_value
            print(f"  [{room_id}] sequence_index={seq_idx} ({ptype}) {param_name}: {old_value} -> {new_value}")

        with open(path, "w", encoding="utf-8") as f:
            json.dump(room, f, indent=2)

    print("DONE")


if __name__ == "__main__":
    main()
