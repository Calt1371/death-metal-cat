# Stress Tester -- Scope Document (Part 3, not yet built)

Status: **scoped, no code written**. This document is for review/approval before any
implementation starts.

## One-line role

Given a room that's marked *finished*, Stress Tester answers two questions Reachability
Verifier's binary pass/fail doesn't: **"is this room's PASS still trustworthy right now"** (has
the ground truth it was checked against moved since biome_spec.json was written) and **"is a
PASS actually safe to trust on formula alone, or does it need a human to confirm it in-game"**
(the OneWayPlatform11->15 lesson: the formula can say reachable while the tightest cases still
deserve a real playtest before anyone builds on top of that assumption).

It reuses, imports, and never reimplements:
- `foundation_extractor.compute_gaps()` (Part 2's shared gap/reachability math)
- `foundation_extractor.query_live_movement_constants()` (Part 1's live-CDO query)
- `reachability_verifier.query_live_platform_data()` (Part 2's live platform query)

No new math is proposed anywhere in this scope. Everything below is either a comparison of
numbers these functions already produce, or a reporting-layer categorization on top of fields
`compute_gaps()` already returns (`reachable_by`, `tolerance`).

## Input / Output

**Input:** `--room <RoomID> --biome <Biome>`, reading:
- `biome_spec_<Biome>.json` (the recorded movement_constants + gaps at spec-generation time)
- The room's live state in the currently-open editor (platform actors, current CDO values) --
  same RemoteExecution bridge every other Tools/ script uses
- The new room-status tracking file (see below)

**Output:** `Tools/stress_test_<Room>.json` + a printed report, containing:
- The room's status-gate result (tested, or skipped-and-why)
- Drift findings, if any (which constants moved, by how much, and which specific gaps changed
  verdict as a result -- not just "something drifted")
- A three-way gap categorization: `comfortable_pass` / `tight_pass_needs_playtest` / `fail`

Deliberately NOT in scope for this agent's output: fixing anything, auto-updating biome_spec.json,
or auto-flipping a room's status. It reports; a human decides what to do with the report --
consistent with the GDD's own "final review and hand-tweaking is a human gate" principle
(Section 4.3).

## 1. Drift re-validation

**Detect:** compare `biome_spec_<Biome>.json`'s recorded `movement_constants` against a *fresh*
live CDO query (reusing `query_live_movement_constants()` verbatim -- no new query logic). Same
relative-tolerance comparison *pattern* Part 1 established (percentage delta, not absolute units,
per the WallJumpForceVertical false-positive lesson) -- but note this is a **different
comparison** than Part 1's, not a call to the same function:
- Part 1 (`compare_constants`) diffs **C++ header defaults vs. live CDO** (a fixed set of raw
  UPROPERTY names, `CPP_VS_LIVE_CONSTANT_MAP`).
- Stress Tester diffs **biome_spec.json's recorded snapshot vs. live CDO right now**, and
  biome_spec.json's `movement_constants` includes *derived* quantities (`max_jump_distance`,
  `max_jump_height`, `wall_jump_max_distance`, `wall_jump_max_height`, `dodge_distance`), not just
  raw properties.

  Proposed refactor to avoid a second formula copy: extract the raw-values-to-derived-quantities
  formula (currently inlined once in Foundation Extractor's embedded template) into its own
  small `compute_derived_movement_constants(raw_values)` function, mirroring exactly how
  `compute_gaps()` was extracted in Part 2. Foundation Extractor's template would call it instead
  of inlining the formula; Stress Tester calls it to turn a fresh raw CDO query into the same
  derived shape biome_spec.json already stores, so the two sides of the diff are apples-to-apples
  without hand-copying the height/distance formulas a second time. **Flagging this as a decision
  point** -- it's a small refactor beyond exactly what was asked, so I want it confirmed before
  Part 3's build rather than doing it unannounced.

  A new relative-tolerance constant would gate this comparison -- reusing Part 1's own
  `MOVEMENT_CONSTANT_MISMATCH_TOLERANCE_FRACTION` (1%) rather than inventing a second number, since
  it's the same category of check (is this delta real tuning or float noise) against the same
  underlying constants.

**React to drift:** unlike Part 1 (which halts *before* ever measuring, since nothing has been
trusted yet), a drift found here means content *already exists* and was already validated against
now-stale numbers. So the reaction isn't a halt -- it's: recompute gaps fresh (live platform query
via `reachability_verifier.query_live_platform_data()` + `compute_gaps()` with the *current* live
constants), then diff the fresh gap verdicts against biome_spec.json's stored verdicts. The report
names exactly which gaps flipped (`comfortable`->`tight`, `pass`->`fail`, etc.), not just "some
constant moved by N%" -- a vague "drift happened" signal isn't actionable, a specific "this gap is
no longer safe" signal is.

## 2. Tight-margin flag for manual playtest

No new computation -- `compute_gaps()` already classifies every gap `tight` or `comfortable`
(`TIGHT_TOLERANCE_FRACTION = 0.85`, already in `foundation_extractor.py`). Reusing that exact
constant, not a second one, since it's the same "how much margin is enough to trust" question.

The gap Stress Tester actually closes: **Reachability Verifier's own pass/fail is binary** --
`passed = bool(reachable_by)`, so a gap that's technically reachable but only just (85%+ of max
range) currently reports as a plain PASS, indistinguishable from a comfortable one. That's exactly
the OneWayPlatform11->15 situation: formula said reachable, margin was tight, and it took an actual
playtest to confirm reality agreed with the formula before anyone should have trusted it blindly.

Stress Tester's reporting layer re-splits Reachability Verifier's PASS bucket into two:
- `comfortable_pass` -- formula says reachable with real margin to spare, trust it
- `tight_pass_needs_playtest` -- formula says reachable, margin is thin, confirm in-game before
  relying on it (mirrors OneWayPlatform11->15 exactly)
- `fail` -- unchanged from Reachability Verifier, unreachable by any measured method

## 3. In-progress vs. finished room status

**Recommendation: a new small tracking file, `Tools/room_status.json`, nested by biome** -- the
same top-level-key-per-biome shape `biome_spec_<Biome>.json`/`asset_catalog_<Biome>.json` already
use, so it scales the same way once a second biome exists:

```json
{
  "AssassinCity": {
    "Room1": "finished",
    "Room2": "finished",
    "Room3": "in_progress",
    "Room4A": "not_started"
  }
}
```

Three states: `not_started` / `in_progress` / `finished`. Stress Tester's `--biome` argument
selects which top-level key to look under. A room with no entry, OR a biome with no entry at all,
both default to `not_started` -- the safe direction either way, so a room never gets
stress-tested by accident just because someone forgot to add it (or forgot to add its whole
biome) to the file.

**Why this over the alternatives you listed:**
- *Not `gdd_reference.json`* -- that file is a snapshot of what the GDD *document* states (room
  roles, agent roster, the C++-vs-CDO constants check), regenerated by re-reading the GDD. Room
  construction status has nothing to do with GDD content and changes on a totally different
  cadence (every time you finish a room, not every time the GDD is edited). Conflating the two
  would mean either the status gets silently clobbered on every Foundation Extractor re-run, or
  Foundation Extractor has to grow logic to preserve it across runs -- unnecessary complexity for
  an unrelated concern.
- *Not a property on `RoomShell` itself* -- a real alternative (status colocated with the actual
  room actor, edited the same way you already hand-tweak generated rooms in the Details panel),
  but it requires a new UPROPERTY + a rebuild, and makes the status unreadable without a live
  editor connection. `room_status.json` can be checked in a fraction of a second with zero UE
  dependency, before ever deciding whether it's worth opening a RemoteExecution connection at all
  for a room that turns out to be `not_started`. If you'd rather have it live on RoomShell instead,
  say so and I'll scope that version instead -- noting the UPROPERTY route is a heavier, C++-side
  change for the same information a JSON file gives for free.
- Matches the project's existing pattern of one small dedicated JSON file per concern
  (`biome_spec_<Biome>.json`, `gdd_reference.json`, `encounter_population_<Room>.json`,
  `room_geometry_<Room>.json`) rather than folding a new concern into an existing file.

**How it's checked:** Stress Tester's very first step, before touching the editor at all --
`room_status.json[args.biome].get(args.room, "not_started")`. If the result isn't `finished`, it
emits a short, explicit `"SKIPPED -- Room3 marked in_progress, not stress-tested"` line and exits
cleanly (not an error) -- no drift check, no gap recompute, nothing that would produce a false
alarm against known-incomplete geometry (directly addresses the Room3 situation from Part 2). A
biome key missing entirely from the file behaves identically to a room missing from a present
biome -- both just fall through to `not_started`.

**Who maintains it:** manually, by you -- "finished" is a human judgment call about a specific
room, the same way "final room review and hand-tweaking" is already an explicit human gate in the
GDD (Section 4.3). No script auto-flips a room to `finished`; Stress Tester only ever *reads* this
file, never writes to it.

## Explicitly out of scope for Part 3

- No auto-fixing of drifted constants, failed gaps, or anything else -- report only.
- No auto-marking of room status.
- Doesn't re-validate `not_started`/`in_progress` rooms at all (by design, per #3).
- Doesn't touch Room1/Room2's already-confirmed-correct state -- this is a read-only checking
  tool, same as Foundation Extractor and Reachability Verifier before it.

## Open decision before building

Whether to extract `compute_derived_movement_constants()` as proposed in section 1 (a small,
Part-2-style refactor to Foundation Extractor) or keep that formula inline and accept a second,
duplicated copy of it inside Stress Tester. Everything else above is ready to build as soon as
you confirm scope + the room_status.json design.
