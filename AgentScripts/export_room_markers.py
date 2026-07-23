"""
export_room_markers.py

Queries every AEncounterSpawnMarker attached (directly or via a nested child, e.g. under a
room's floor) to a given RoomShell in the currently-open UE5 editor level, and exports
{marker_id, marker_type} for each to a JSON file -- the format the Level & Encounter Designer
agent's JSON-driven population pass expects. Reads whatever's actually placed in the room right
now; there's no hand-typed marker list to keep in sync with the level.

REQUIREMENTS: same as send_to_ue.py -- UE5 editor running with Remote Execution enabled
(Project Settings -> Plugins -> Python), and this script on the same machine as the editor.

USAGE:
    python export_room_markers.py --room Room1 --output markers_room1.json
"""

import argparse
import os
import sys
import tempfile
import time

from remote_execution import RemoteExecution

VALID_ROOMS = ['Room1', 'Room2', 'Room3', 'Room4A', 'Room4B', 'Room5', 'Room6', 'Room7', 'Room8']

# The editor-side script writes the JSON file itself (it's on the same machine as the calling
# terminal -- same assumption send_to_ue.py already documents), rather than trying to pass
# structured data back over the remote execution protocol. Placeholders are substituted via
# plain string replacement, not an f-string, so the generated source's own braces/quotes never
# need escaping.
_QUERY_TEMPLATE = """
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
    unreal.log_error("[EXPORT MARKERS] No RoomShell_" + room_id_name + " found in the level")
else:
    attached = room_shell.get_attached_actors(False, True)
    markers = [a for a in attached if isinstance(a, unreal.EncounterSpawnMarker)]

    type_names = {
        unreal.EncounterMarkerType.ENEMY_SPAWN: "EnemySpawn",
        unreal.EncounterMarkerType.PICKUP_SPAWN: "PickupSpawn",
    }

    rows = []
    for m in markers:
        marker_type = m.get_editor_property("marker_type")
        rows.append({
            "marker_id": m.get_editor_property("marker_id"),
            "marker_type": type_names.get(marker_type, str(marker_type)),
        })

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(rows, f, indent=2)

    unreal.log_warning("[EXPORT MARKERS] Wrote " + str(len(rows)) + " marker(s) from " + room_id_name + " to " + output_path)
"""


def build_query_command(room_id_name: str, output_path: str) -> str:
    return (
        _QUERY_TEMPLATE
        .replace("__ROOM_ID_NAME__", room_id_name)
        .replace("__OUTPUT_PATH__", output_path)
    )


def main():
    parser = argparse.ArgumentParser(description="Export AEncounterSpawnMarker actors from a room to JSON.")
    parser.add_argument("--room", required=True, help=f"Room to export markers from. One of: {', '.join(VALID_ROOMS)}")
    parser.add_argument("--output", required=True, help="Output JSON file path (relative paths resolve against the current directory).")
    parser.add_argument("--timeout", type=float, default=5.0, help="Seconds to wait for the editor to respond to discovery.")
    args = parser.parse_args()

    room_lookup = {name.lower(): name for name in VALID_ROOMS}
    canonical_room = room_lookup.get(args.room.lower())
    if canonical_room is None:
        print(f"ERROR: '{args.room}' is not a valid room. Must be one of: {', '.join(VALID_ROOMS)}")
        sys.exit(1)

    room_id_name = canonical_room.upper()  # 'Room1' -> 'ROOM1', 'Room4A' -> 'ROOM4A'
    output_path = os.path.abspath(args.output)

    script_body = build_query_command(room_id_name, output_path)

    # ExecuteStatement only accepts a single statement, not a multi-line script -- write the real
    # script to a temp file and send a one-line exec(open(...).read()) instead, same pattern as
    # every other AgentScripts/*.py invoked via send_to_ue.py in this project.
    temp_fd, temp_path = tempfile.mkstemp(suffix=".py", prefix="export_room_markers_")
    with os.fdopen(temp_fd, "w", encoding="utf-8") as f:
        f.write(script_body)

    command = f"exec(open(r'{temp_path}').read())"

    remote_exec = RemoteExecution()
    remote_exec.start()

    try:
        waited = 0.0
        poll_interval = 0.25
        while not remote_exec.remote_nodes and waited < args.timeout:
            time.sleep(poll_interval)
            waited += poll_interval

        if not remote_exec.remote_nodes:
            print("ERROR: No UE5 editor instance found. Is the editor running with Remote "
                  "Execution enabled, and on the same machine/network?")
            sys.exit(1)

        node_id = remote_exec.remote_nodes[0]["node_id"]
        remote_exec.open_command_connection(node_id)
        result = remote_exec.run_command(command, unattended=True, exec_mode="ExecuteStatement")
        remote_exec.close_command_connection()
    finally:
        remote_exec.stop()
        os.remove(temp_path)

    if not result.get("success"):
        print(f"ERROR: editor reported failure:\n{result}")
        sys.exit(1)

    # Surface the editor's own log lines so failures (e.g. "no RoomShell found") are visible
    # even though the remote execution call itself reports success.
    had_error = False
    for entry in result.get("output", []):
        print(f"[UE5] {entry.get('type')}: {entry.get('output')}")
        if entry.get("type") == "Error":
            had_error = True

    if had_error or not os.path.exists(output_path):
        print(f"ERROR: export did not produce {output_path} -- check the messages above.")
        sys.exit(1)

    print(f"Exported markers for {canonical_room} to {output_path}")


if __name__ == "__main__":
    main()
