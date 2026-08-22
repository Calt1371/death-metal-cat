# The "Golden Room" Import Scripts -- What They Actually Do

Two scripts turn the content-pipeline agents' JSON *decisions* into real actors in the live UE5
level. Neither is an agent itself -- both are deterministic editor-scripting bridges, same
Remote Execution pattern as everything else under `AgentScripts/`.

## `Tools/import_room_geometry.py`

Input: `Tools/room_geometry_<RoomID>.json` (Room Geometry Designer's output -- an ordered
sequence of pieces + spawn markers).

**Requires an existing `RoomShell_<RoomID>` actor already in the level** -- it does not create
or position one. It walks the piece list left-to-right along X, starting from the RoomShell's
own location, and places one plain grey blockout mesh per piece:

| Piece type | What gets placed |
|---|---|
| `flat_run`, `ledge_step`, `enemy_arena`, `drop_down` | A floor: a `StaticMeshActor` using `/Engine/BasicShapes/Cube.Cube`, scaled to the piece's width/length. `ledge_step` and `drop_down` also shift the running Z height up/down before placing. |
| `gap` | No geometry at all -- just advances the running X position by the gap's width. |
| `wall_jump_shaft` | A vertical wall (same cube mesh, scaled tall) at a fixed nominal width, height matching the piece's `wall_height`. |

After all pieces, it places one `AEncounterSpawnMarker` per `spawn_markers` entry, centered on
the piece it references (`after_piece_index`), with its `marker_id`/`marker_type` properties set
directly from the JSON.

**Idempotent by construction**: every actor it creates is labeled `Floor_<RoomID>_##_...`,
`Wall_<RoomID>_##_...`, or `Marker_<RoomID>_<marker_id>`. Re-running it for a room first deletes
every existing attached actor whose label starts with one of those three prefixes, so
regenerating a room's JSON and re-importing never leaves duplicates behind. It never touches
`ExitTrigger_*`/`BiomeEndMarker_*` or anything with a different label.

**This is why it's dangerous to run against Room1-Room6**: those rooms' real floor tiles are
literally named `Floor_ROOM<N>_00_FlatRun` (the established convention), which matches this
script's own deletion filter exactly. Running it there would delete the real hand-placed art
geometry and replace it with grey blockout cubes.

**What it does NOT do**: no sprite art, no background dressing, no rubble/cables, no
RoomShell creation/positioning, no exit trigger. It's a blockout-geometry step only.

## `Tools/spawn_encounter_actors.py`

Input: `Tools/encounter_population_<RoomID>.json` (Level & Encounter Designer's output -- a
population decision per marker).

Closes the gap between a *decision* ("this marker is an enemy") and an actual actor. For each
populated marker:

- **`marker_type: "enemy"`** -- spawns `BP_EnemyBase` at the marker's exact transform. If
  `enemy_count > 1`, extra instances are offset along X (80 units apart, centered on the marker)
  so their capsules don't spawn stacked.
- **`marker_type: "pickup"`** -- logged and **skipped**, not faked. No pickup actor class exists
  anywhere in this project yet (confirmed by search, not assumed), so this is an honest gap, not
  an oversight.
- **`marker_type: "empty"`** -- nothing to do; `AEncounterSpawnMarker` derives from
  `ATargetPoint` and has no footprint on its own.

It matches JSON entries to live marker actors by the `marker_id` **editor property**
(`import_room_geometry.py` sets this directly from the same JSON string), not by actor label --
`marker_id` has no consistent naming convention across rooms, so label-parsing would be fragile.

Spawned enemies are attached to the room's `RoomShell`, same as floors/markers, since
`ARoomShell::SetRoomActive` recurses over attached actors to hide/disable them when a room
deactivates -- this is required for correct behavior, not just cosmetic parenting.

**Idempotent the same way**: every spawned enemy is labeled `SpawnedEnemy_<RoomID>_<marker_id>_##`;
re-running for a room first deletes every actor with that prefix before respawning.

## Room7: a real, live proof of both scripts

As a follow-up to the sandboxed class-assignment demo (`docs/agent_crew_README.md`), these two
scripts were run for real -- against Room7, the next room in the actual progression that didn't
have a `RoomShell` yet, so nothing real was at risk of being overwritten. Using Room7's
already-committed, previously-validated JSON (not a fresh regeneration, since this pass was about
proving the *placement* pipeline, not re-testing generation):

1. Created `RoomShell_ROOM7` at a live-computed safe offset (5250 units clear of every other
   room's actual current bounding box, same convention as Room3-Room6 -- not assumed from old
   numbers).
2. `import_room_geometry.py --room Room7` placed 7 floors, 1 wall, and 11 spawn markers.
3. `spawn_encounter_actors.py --room Room7` spawned 31 real enemy actors (across 9 `EnemySpawn`
   markers) and correctly skipped the 2 `PickupSpawn` markers.
4. Verified: zero bounding-box overlap with any of the other 7 rooms, and every actor count
   matched the source JSON exactly.

Full trace: `Tools/test_output/golden_script_room7_live_build.json`.

**What Room7 looks like right now**: functionally correct and fully populated with real enemies,
but visually plain grey blockout -- it does not match Room1-Room6's art-dressed standard (no
walkway/rubble/background sprites, no fused floating platforms). Extending these scripts (or
building a parallel art-dressing pass) to translate abstract piece types into the established
art conventions is a separate, not-yet-designed follow-up task -- there's no existing mapping
from e.g. `wall_jump_shaft` to a specific dressed visual, and that mapping needs a real design
decision, not an improvised one.

## Where Foundation Extractor fits in

This is the workflow the project owner actually uses: **hand-build one room exactly right, with
real assets and real measurements (Room1), then run `Tools/foundation_extractor.py` to measure
that finished room and write `Tools/biome_spec_AssassinCity.json`** -- a read-only measurement
pass (it only ever calls `get_*`-style query methods and CDO property reads; it never modifies,
moves, or spawns anything). That JSON captures, all pulled live from the real level/Blueprints
rather than guessed:

- **Movement constants** -- gravity, jump velocity, wall-jump/dodge reach, queried straight off
  `BP_DeathMetalCat`'s CDO.
- **Gap spacing conventions** -- which of Room1's gaps are "tight" vs "comfortable" relative to
  the player's actual reach, plus an explicit reference list of confirmed deliberate hard jumps.
- **Art placement conventions** -- for every sprite/floor in Room1: scale, color, rotation, and
  (for floors specifically) the fixed depth-scale and depth-offset-from-background conventions,
  plus which sprites "dress" a floor (detected by geometry overlap, not by name).

**The intended relationship, straight from the Extractor's own docstring, is exactly what you
described**: it's meant to produce "a contract for downstream room-generation agents to
consume" -- i.e. Room Geometry Designer (and, transitively, the golden import script that
places its output) is supposed to build new rooms in a way that stays consistent with whatever
the Extractor measured off the real golden room.

**Where that relationship stands right now, concretely (not aspirationally)**:

- Room Geometry Designer's movement constraints (`GRAVITY_Z`, `JUMP_Z_VELOCITY`,
  `MAX_JUMP_HEIGHT`, etc.) are **hardcoded constants inside `room_geometry_designer.py` itself**,
  not a live read of `biome_spec_AssassinCity.json` -- there is no `import`/`open()` of that file
  anywhere outside the Extractor script itself (confirmed by search). They were originally
  populated the same way the Extractor measures (querying the real CDO), just captured by hand
  into that script instead of piped in automatically.
- **This has already caused real drift**, documented directly in the Extractor's own comments:
  a live query found `BP_DeathMetalCat`'s actual `JumpZVelocity` at `773.22`, while
  `room_geometry_designer.py` still has `708.652466` hardcoded (itself a correction of an even
  older `600`). So right now, every room Room Geometry Designer generates -- including Room7 --
  is being validated against a jump-height ceiling that's already stale relative to the real,
  current character tuning.
- The Extractor's **art-placement data isn't consumed by anything yet**. Neither
  `import_room_geometry.py` nor `spawn_encounter_actors.py` reads it, which is exactly why Room7
  came out as grey blockout instead of art-dressed -- the scale/color/rotation/depth conventions
  needed to dress a generated room the way Room1 looks already exist in
  `biome_spec_AssassinCity.json`; nothing has been built yet to apply them.

**In short**: your workflow (measure the golden room, let that measurement govern everything
downstream) is the right mental model and is literally what this script was designed to enable
-- it's just currently a human-in-the-loop link (you or I read the Extractor's output and
manually keep the generator's constants in sync) rather than a wired-up automatic one, and the
sync has already lapsed once on the movement constants. Making Room Geometry Designer load
`biome_spec_AssassinCity.json` directly (instead of hardcoding a copy of it) would close that gap
permanently, and would also be the natural place to hand the art-placement data to a future
art-dressing pass for the golden import script.
