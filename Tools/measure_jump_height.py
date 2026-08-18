#!/usr/bin/env python3
"""
measure_jump_height.py

One-off calibration script -- empirically measures ADeathMetalCatCharacter's real max standing-
jump height in a live PIE session, the same "TestJumpDistance is formula-derived no longer, it's
now measured" precedent that produced room_geometry_designer.py's MAX_JUMP_DISTANCE/
MAX_JUMP_DISTANCE_RUNNING constants. Confirmed necessary this run: foundation_extractor.py's
formula (JumpZVelocity^2 / (2*GravityZ) = 305uu with the current live JumpZVelocity=773.22) flagged
a real, user-confirmed-reachable platform-to-platform climb (470uu height gain) as physically
impossible -- meaning the ballistic formula alone understates real in-game jump height by a wide
margin (a running-input hold, AirControl>1.0 steering, or some other movement-component
interaction the simple formula doesn't model).

Requires NO C++ changes -- reuses the existing TestJumpDistance("Standing") Exec console command
(DeathMetalCatCharacter.cpp) as the jump trigger, and measures its actual peak height purely by
polling GetActorLocation().Z from the OUTSIDE across real wall-clock time (a single UE-side exec()
call can't poll across frames -- it would block the whole engine -- so this keeps ONE remote
connection open and issues repeated short queries with real time.sleep() between them, letting the
PIE session's own clock keep advancing between each poll).

REQUIRES: PIE already running, player character grounded on a flat surface with NO platform
directly overhead within jump range (an overhead OneWayPlatform's underside is one-way -- it lets
the player pass through while ascending below it, but BLOCKS the instant the player's feet reach
its top surface, capping the measured jump artificially low; see OneWayPlatform's Tick logic).
Confirmed clear spot for Room1: Floor_ROOM1_00_FlatRun2's far-left region (nothing overhead until
OneWayPlatform3/4 start around X=-2169) -- this script teleports the player there itself.

USAGE:
    python measure_jump_height.py
"""

import os
import sys
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(TOOLS_DIR, "..", "AgentScripts"))

from remote_execution import RemoteExecution

# Confirmed clear of every OneWayPlatform in Room1 (nearest overlap starts at X=-2169) and well
# within Floor_ROOM1_00_FlatRun2's footprint (X=-3464.5..-2064.5, top_z=494.0).
TEST_X = -3200.0
TEST_Y = 0.0
FLOOR_TOP_Z = 494.0
TELEPORT_Z_CLEARANCE = 150.0  # drop the player in from slightly above the floor, let gravity settle it
SETTLE_WAIT_SECONDS = 1.5
POLL_INTERVAL_SECONDS = 0.08
POLL_TIMEOUT_SECONDS = 3.0

_TELEPORT_AND_RESET = f"""
import unreal
pie_worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
world = pie_worlds[0]
player_char = unreal.GameplayStatics.get_player_character(world, 0)
move_comp = player_char.get_component_by_class(unreal.CharacterMovementComponent)
move_comp.set_editor_property("velocity", unreal.Vector(0, 0, 0))
player_char.set_actor_location(unreal.Vector({TEST_X}, {TEST_Y}, {FLOOR_TOP_Z + TELEPORT_Z_CLEARANCE}), False, True)
unreal.log_warning("[JUMP HEIGHT CAL] teleported player to test spot")
"""

_CHECK_GROUNDED = """
import unreal
pie_worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
world = pie_worlds[0]
player_char = unreal.GameplayStatics.get_player_character(world, 0)
move_comp = player_char.get_component_by_class(unreal.CharacterMovementComponent)
loc = player_char.get_actor_location()
unreal.log_warning(f"GROUNDED={move_comp.is_moving_on_ground()} Z={loc.z:.3f}")
"""

_START_JUMP_TEST = """
import unreal
pie_worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
world = pie_worlds[0]
player_char = unreal.GameplayStatics.get_player_character(world, 0)
player_controller = unreal.GameplayStatics.get_player_controller(world, 0)
start_z = player_char.get_actor_location().z
unreal.SystemLibrary.execute_console_command(world, "TestJumpDistance Standing", player_controller)
unreal.log_warning(f"START_Z={start_z:.3f}")
"""

_POLL_Z = """
import unreal
pie_worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
world = pie_worlds[0]
player_char = unreal.GameplayStatics.get_player_character(world, 0)
move_comp = player_char.get_component_by_class(unreal.CharacterMovementComponent)
loc = player_char.get_actor_location()
unreal.log_warning(f"Z={loc.z:.3f} FALLING={move_comp.is_falling()} ON_GROUND={move_comp.is_moving_on_ground()}")
"""


def run_command(remote_exec, script_body):
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".py", prefix="measure_jump_height_")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(script_body)
    try:
        return remote_exec.run_command(f"exec(open(r'{path}').read())", unattended=True, exec_mode="ExecuteStatement")
    finally:
        os.remove(path)


def extract_print_line(result, prefix):
    for entry in result.get("output", []):
        text = entry.get("output", "")
        if prefix in text:
            return text.strip()
    return None


def main() -> int:
    remote_exec = RemoteExecution()
    remote_exec.start()
    try:
        waited = 0.0
        while not remote_exec.remote_nodes and waited < 5.0:
            time.sleep(0.25)
            waited += 0.25
        if not remote_exec.remote_nodes:
            print("ERROR: No UE5 editor instance found.")
            return 1

        node_id = remote_exec.remote_nodes[0]["node_id"]
        remote_exec.open_command_connection(node_id)

        print("Teleporting player to a clear test spot...")
        result = run_command(remote_exec, _TELEPORT_AND_RESET)
        if not result.get("success"):
            print(f"ERROR: teleport failed:\n{result}")
            return 1

        print(f"Waiting {SETTLE_WAIT_SECONDS}s for the player to settle on the floor...")
        time.sleep(SETTLE_WAIT_SECONDS)

        result = run_command(remote_exec, _CHECK_GROUNDED)
        line = extract_print_line(result, "GROUNDED=")
        print(f"[CHECK] {line}")
        if not line or "GROUNDED=True" not in line:
            print("ERROR: player is not grounded after settling -- aborting rather than testing an airborne jump.")
            return 1

        result = run_command(remote_exec, _START_JUMP_TEST)
        line = extract_print_line(result, "START_Z=")
        print(f"[START] {line}")
        start_z = float(line.split("START_Z=")[1])

        max_z = start_z
        waited = 0.0
        landed_after_leaving_ground = False
        has_left_ground = False
        while waited < POLL_TIMEOUT_SECONDS:
            time.sleep(POLL_INTERVAL_SECONDS)
            waited += POLL_INTERVAL_SECONDS
            result = run_command(remote_exec, _POLL_Z)
            line = extract_print_line(result, "Z=")
            if not line:
                continue
            parts = dict(p.split("=") for p in line.split())
            z = float(parts["Z"])
            falling = parts["FALLING"] == "True"
            on_ground = parts["ON_GROUND"] == "True"
            max_z = max(max_z, z)
            if falling:
                has_left_ground = True
            if has_left_ground and on_ground:
                landed_after_leaving_ground = True
                print(f"[POLL] t={waited:.2f}s Z={z:.2f} -- landed")
                break
            print(f"[POLL] t={waited:.2f}s Z={z:.2f} falling={falling}")

        remote_exec.close_command_connection()

        if not landed_after_leaving_ground:
            print("WARNING: poll loop timed out before detecting landing -- max_z may be an underestimate if the jump was still airborne.")

        measured_height = max_z - start_z
        print(f"\nMEASURED max jump height: {measured_height:.2f} units (start_z={start_z:.2f}, peak_z={max_z:.2f})")
        return 0
    finally:
        remote_exec.stop()


if __name__ == "__main__":
    sys.exit(main())
