"""导入并入肩部的四件装备的无人机机身和地雷本体（lookdev/make_shoulder_tactical.py 的输出）→ /Game/Weapons/Meshes，再跑 ue_drone_materials.py 挂材质。可重复跑。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_shoulder_tactical.py" -unattended -nosplash
"""
import os

import unreal

SRC = 'D:/RobotSlice/Inbox/_肩部候选/游戏模型_战术'
DEST = '/Game/Weapons/Meshes'
NAMES = ['SM_BurstPack', 'SM_LaunchSpring', 'SM_ShockRing', 'SM_MineLayer', 'SM_Mine']
eas = unreal.EditorAssetLibrary

ui = unreal.FbxImportUI()
ui.set_editor_property('import_mesh', True)
ui.set_editor_property('import_as_skeletal', False)
ui.set_editor_property('import_materials', False)
ui.set_editor_property('import_textures', False)
ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
data = ui.static_mesh_import_data
data.set_editor_property('combine_meshes', True)
data.set_editor_property('auto_generate_collision', False)
try:
    data.set_editor_property('build_nanite', False)
except Exception as e:
    unreal.log_warning(f'[tactical] 导入选项没有 build_nanite：{e}')

tasks = []
for n in NAMES:
    t = unreal.AssetImportTask()
    t.filename = f'{SRC}/{n}.fbx'
    t.destination_path = DEST
    t.destination_name = n
    t.automated = True
    t.replace_existing = True
    t.save = True
    t.options = ui
    assert os.path.exists(t.filename), t.filename
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

lines = []
for n in NAMES:
    mesh = eas.load_asset(f'{DEST}/{n}')
    assert mesh, f'没导进来 {n}'
    # 不开 Nanite：同其他肩部件（跟着机体动的小件用不上，M_DronePBR 也没勾 Nanite 用途）
    ns = mesh.get_editor_property('nanite_settings')
    if ns.get_editor_property('enabled'):
        ns.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', ns)
        mesh.post_edit_change()
        lines.append(f'{n} 关掉 Nanite')
    eas.save_asset(f'{DEST}/{n}', only_if_is_dirty=False)
    b = mesh.get_bounding_box()
    d = b.max - b.min
    lines.append(f'{n} 尺寸 {d.x:.1f}x{d.y:.1f}x{d.z:.1f} 厘米，Nanite {mesh.get_editor_property("nanite_settings").get_editor_property("enabled")}，顶点 {mesh.get_num_vertices(0)}，槽 {[str(s.get_editor_property("material_slot_name")) for s in mesh.get_editor_property("static_materials")]}')
    assert 15.0 < max(d.x, d.y, d.z) < 100.0, f'{n} 尺寸不对，FBX 单位错了？'
open('D:/RobotSlice/Saved/drone_tactical_import.txt', 'w', encoding='utf-8').write(chr(10).join(lines))
unreal.log('[tactical] 导入完成')

exec(open('D:/RobotSlice/tools/ue_drone_materials.py', encoding='utf-8').read())
exec(open('D:/RobotSlice/tools/ue_shield_fix.py', encoding='utf-8').read())
