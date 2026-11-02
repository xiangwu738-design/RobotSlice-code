"""只读（11-01）：查肩部无人机用的网格开没开 Nanite、挂了什么材质；打包版日志报 MI_Drone_*_Paint missing usage flag Nanite 时用。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_drone_material_usage_probe.py" -unattended -nosplash
结果写到 Saved/drone_usage_probe.txt
"""
import unreal

eas = unreal.EditorAssetLibrary
out = []
for path in eas.list_assets('/Game/Weapons/Meshes', recursive=True, include_folder=False):
    a = eas.load_asset(path)
    if not isinstance(a, unreal.StaticMesh):
        continue
    mats = [str(s.get_editor_property('material_interface').get_name()) if s.get_editor_property('material_interface') else 'None' for s in a.get_editor_property('static_materials')]
    if not any('Drone' in m for m in mats):
        continue
    nan = a.get_editor_property('nanite_settings').get_editor_property('enabled')
    out.append(f'{a.get_name()} nanite={nan} mats={mats}')
base = eas.load_asset('/Game/Weapons/Materials/M_DronePBR')
out.append('M_DronePBR used_with_nanite=' + str(base.get_editor_property('used_with_nanite')))
open('D:/RobotSlice/Saved/drone_usage_probe.txt', 'w', encoding='utf-8').write('\n'.join(out))
