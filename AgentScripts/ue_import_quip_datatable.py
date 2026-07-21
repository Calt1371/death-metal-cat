"""
ue_import_quip_datatable.py

Imports the curated Quip Generator output into a DataTable asset (row struct FQuipDataTableRow,
see Source/PythonTest/QuipTypes.h) -- the Quip Generator agent's GDD data flow ("JSON, keyed by
trigger type") landing in the engine, matching the Level & Encounter Designer's planned
JSON-in-Python-import-script pattern. Reads straight from Tools/quip_batch_curation.json (the
hand-curated output of Tools/quip_generator.py, which calls the Claude API directly -- see that
script's own header comment for how it was produced). No live API call happens here at all.

Re-runnable: Unreal's FillDataTableFromJSONString empties the DataTable before repopulating (see
UDataTable::CreateTableFromJSONString -> FDataTableImporterJSON::ReadTable, which calls
EmptyTable() first), so re-running this after re-curating the JSON is safe -- it won't leave
stale rows behind from a larger/different previous import.

Invoke via:
    python send_to_ue.py "exec(open(r'C:\\Users\\calvi\\Desktop\\Projects\\PythonTest\\AgentScripts\\ue_import_quip_datatable.py').read())"
"""

import json
import unreal

CURATED_JSON_PATH = r"C:\Users\calvi\Desktop\Projects\PythonTest\Tools\quip_batch_curation.json"
DEST_PATH = "/Game/Data"
TABLE_NAME = "DT_Quips"

with open(CURATED_JSON_PATH, "r", encoding="utf-8") as f:
    curated = json.load(f)

# Flatten the {trigger_type: [quip, ...]} shape into DataTable JSON-import rows -- "Name" is
# Unreal's reserved row-name key, everything else maps directly onto FQuipDataTableRow's
# properties. TriggerType is written as the plain enumerator name (capitalized trigger_type key,
# e.g. "kill" -> "Kill") to match EQuipTriggerType's enumerator names exactly -- if those
# enumerator names ever change in QuipTypes.h, this capitalization mapping needs to change too.
rows = []
for trigger_type, quips in curated.items():
    enum_name = trigger_type.capitalize()
    for i, quip in enumerate(quips):
        rows.append({
            "Name": f"{enum_name}_{i:02d}",
            "TriggerType": enum_name,
            "Line": quip["line"],
            "SoundTag": quip["sound_tag"],
        })

json_string = json.dumps(rows)

full_path = f"{DEST_PATH}/{TABLE_NAME}"
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

if unreal.EditorAssetLibrary.does_asset_exist(full_path):
    data_table = unreal.EditorAssetLibrary.load_asset(full_path)
else:
    factory = unreal.DataTableFactory()
    factory.struct = unreal.QuipDataTableRow.static_struct()
    data_table = asset_tools.create_asset(TABLE_NAME, DEST_PATH, unreal.DataTable, factory)

if data_table is None:
    raise RuntimeError(f"Failed to create/load DataTable at {full_path}")

success = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(data_table, json_string)
if not success:
    raise RuntimeError(
        "fill_data_table_from_json_string reported failure -- check the Output Log above this "
        "line for the specific row-parse error"
    )

unreal.EditorAssetLibrary.save_loaded_asset(data_table)
unreal.log(f"[quip import] {full_path}: imported {len(rows)} rows across {len(curated)} trigger types")
unreal.log("=== QUIP DATATABLE IMPORT COMPLETE ===")
