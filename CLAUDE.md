# Death Metal Cat — UE5 project notes

## UE5 Python remote-execution bridge (`AgentScripts/`)

Commands are sent to the live, already-open editor via `AgentScripts/send_to_ue.py`:

```
python send_to_ue.py "exec(open(r'C:\Users\calvi\Desktop\Projects\PythonTest\AgentScripts\ue_some_script.py').read())"
```

`send_to_ue.py` compiles its argument as a single Python statement, so multi-line logic must
live in its own `.py` file and be run via `exec(open(...).read())` — a literal multi-line string
passed directly on the command line fails with `SyntaxError: multiple statements found while
compiling a single statement`.

If a command comes back with `ERROR: No UE5 editor instance found`, it's usually just remote-execution
discovery (UDP multicast) not having found the editor yet on the first try — retry once before
assuming the editor isn't running or Remote Execution is disabled in Project Settings.

### API quirks discovered debugging DeathBot placement (2026-08-18)

- `unreal.EditorActorSubsystem()` / `unreal.EditorLevelLibrary.get_editor_world()` etc. (direct
  construction) are deprecated in this engine version and print a `DeprecationWarning` on every
  call. Use `unreal.get_editor_subsystem(unreal.EditorActorSubsystem)` and
  `unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()` instead.
- `unreal.TraceTypeQuery.TRACE_TYPE_QUERY1` is deprecated too — use
  `unreal.TraceTypeQuery.ECC_VISIBILITY`.
- `unreal.SystemLibrary.line_trace_single(...)` returns a `HitResult` struct directly (not a
  `(bool, HitResult)` tuple). Its fields are **not** exposed as plain Python attributes and
  `get_editor_property("b_blocking_hit")` / `get_editor_property("blocking_hit")` both fail with
  a property-not-found error — the reliable way to read a `HitResult` is `hit.to_dict()`, which
  returns a plain dict keyed by the snake_case field names (`blocking_hit`, `location`,
  `hit_actor`, `hit_component`, etc.).
- A straight-down line trace against Room1's climbing section will happily hit the *first*
  `OneWayPlatform` it passes through top-down rather than the room's actual ground floor — Room1
  has stacked one-way platforms well above the real floor level in the middle/climbing portion of
  the room, so "trace down from high up" is not a reliable way to find true ground there. Only the
  far-left and far-right stretches (`Floor_ROOM1_00_FlatRun`/`FlatRun2`) are plain flat floor.

## Room1 in the live level

Room1 (`RoomShell_ROOM1`) is hand-built directly in the editor with real art, not generated via
the `room_geometry_designer.py` → `import_room_geometry.py` pipeline used for later rooms. It has
**no live `EncounterSpawnMarker` actors**, and running `import_room_geometry.py`/
`spawn_encounter_actors.py` against it would delete the real hand-placed floor geometry (their
idempotent-cleanup step matches on the same `Floor_ROOM1_*`/`SpawnedEnemy_ROOM1_*` label prefixes
Room1's real content already uses). Enemy placement in Room1 is manual (see
`AgentScripts/ue_spawn_deathbot_room1.py`), not pipeline-driven.
