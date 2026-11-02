"""把一个资源导出成 FBX（只读，不改工程）：export_fbx.py 资源路径 输出.fbx"""
import sys

import unreal

asset_path, out = sys.argv[1], sys.argv[2]
asset = unreal.load_asset(asset_path)
task = unreal.AssetExportTask()
task.object = asset
task.filename = out
task.automated = True
task.prompt = False
task.replace_identical = True
opts = unreal.FbxExportOption()
opts.collision = False
opts.level_of_detail = False
opts.export_morph_targets = False
task.options = opts
ok = unreal.Exporter.run_asset_export_task(task)
unreal.log(f'[export] {asset_path} -> {out}: {ok}')
