import unreal

TEX_PATH = "/Game/Characters/DeathMetalCat/Textures/T_DeathMetalCat_SpriteSheet"

tex = unreal.EditorAssetLibrary.load_asset(TEX_PATH)
pkg = tex.get_package()
reloaded, err = unreal.EditorLoadingAndSavingUtils.reload_packages(
    [pkg], unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE
)
print("reload_packages -> reloaded:", reloaded, " error:", err)

tex_fresh = unreal.EditorAssetLibrary.load_asset(TEX_PATH)
print(f"Texture size: {tex_fresh.blueprint_get_size_x()}x{tex_fresh.blueprint_get_size_y()} (expect 1182x1331)")

export_path = r"C:\Users\calvi\AppData\Local\Temp\claude\C--Users-calvi-Desktop-Projects-PythonTest\cc25d889-fa20-481a-98ff-6b5c09b0f224\scratchpad\ue_texture_export.png"
task = unreal.AssetExportTask()
task.object = tex_fresh
task.filename = export_path
task.automated = True
task.replace_identical = True
task.prompt = False
task.exporter = unreal.TextureExporterPNG()
result = unreal.Exporter.run_asset_export_task(task)
print("export result:", result, " ->", export_path)
