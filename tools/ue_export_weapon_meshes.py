"""把击毁播报要用的武器模型导出成 OBJ（只读，不改工程），给 tools/make_weapon_icons.py 画剪影。流程见 tools/make_weapon_icons.sh。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_export_weapon_meshes.py" -AllowCommandletRendering -RenderOffscreen -unattended
"""
import os
import sys

import unreal

sys.path.insert(0, 'D:/RobotSlice/tools')
import weapon_icon_table as table

os.makedirs(table.MESH_DIR, exist_ok=True)
done = 0
items = table.items()
for name, path in items:
    mesh = unreal.load_asset(path)
    if not mesh:
        unreal.log_warning(f'[weaponicon] 找不到模型 {path}（{name}）')
        continue
    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = f'{table.MESH_DIR}/{name}.obj'
    task.exporter = unreal.StaticMeshExporterOBJ()
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    ok = unreal.Exporter.run_asset_export_task(task)
    done += 1 if ok else 0
    unreal.log(f'[weaponicon] {name} {path} -> {task.filename}: {ok}')
unreal.log(f'[weaponicon] 导出完成 {done}/{len(items)}')
