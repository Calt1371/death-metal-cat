"""
send_to_ue.py

Runner script for Claude Code to send Python commands to a running
Unreal Engine 5 editor session via the Remote Execution protocol.

USAGE:
    python send_to_ue.py "unreal.log('hello from claude code')"

REQUIREMENTS:
    - remote_execution.py must be in the same folder as this script
      (copy it from [UE5 Install]/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python/)
    - UE5 editor must be running with:
        Project Settings -> Plugins -> Python -> Enable Remote Execution (checked)
    - This script and the editor must be on the same machine/network segment
      (multicast discovery does not cross most VPNs or subnets cleanly)

NOTE: Method names below match the common remote_execution.py implementation
shipped with recent UE5 versions. If Epic has changed the API in your
specific engine version, open remote_execution.py and check the RemoteExecution
class for the exact method signatures -- this is the most likely thing to
need adjusting.
"""

import sys
import time
from remote_execution import RemoteExecution


def send_command(command: str, timeout_seconds: float = 5.0):
    remote_exec = RemoteExecution()
    remote_exec.start()

    try:
        waited = 0.0
        poll_interval = 0.25
        while not remote_exec.remote_nodes and waited < timeout_seconds:
            time.sleep(poll_interval)
            waited += poll_interval

        if not remote_exec.remote_nodes:
            print("ERROR: No UE5 editor instance found. Is the editor running "
                  "with Remote Execution enabled, and on the same machine/network?")
            sys.exit(1)

        node_id = remote_exec.remote_nodes[0]["node_id"]
        remote_exec.open_command_connection(node_id)

        result = remote_exec.run_command(command, unattended=True, exec_mode="ExecuteStatement")
        print("Result from UE5:")
        print(result)

        remote_exec.close_command_connection()

    finally:
        remote_exec.stop()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print('Usage: python send_to_ue.py "<python command to run in UE5>"')
        sys.exit(1)

    command_to_run = sys.argv[1]
    send_command(command_to_run)
