"""装备模型清单（只读）：枪、刀、肩部、框架每件的模型、材质槽有没有挂着默认材质、Nanite，以及 SM_Blade / SM_Rifle 这类没进目录的模型有没有资源在引用。结果写 Saved/equipment_census.txt
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_equipment_census.py" -unattended -nosplash
"""
import unreal

eas = unreal.EditorAssetLibrary
reg = unreal.AssetRegistryHelpers.get_asset_registry()
out = []


def base(m):
    while isinstance(m, unreal.MaterialInstance):
        m = m.get_editor_property('parent')
    return m


def is_default(mi):
    return mi is None or mi.get_name() in ('WorldGridMaterial', 'DefaultMaterial') or (base(mi) and base(mi).get_path_name().startswith('/Engine/'))


def refs(pkg):
    opt = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True, include_searchable_names=False, include_soft_management_references=False, include_hard_management_references=False)
    return [str(r) for r in (reg.get_referencers(pkg, opt) or [])]


for p in sorted(eas.list_assets('/Game/Weapons/Meshes', recursive=False)):
    a = eas.load_asset(p)
    if not isinstance(a, unreal.StaticMesh):
        continue
    bad = []
    names = []
    for s in a.get_editor_property('static_materials'):
        mi = s.get_editor_property('material_interface')
        names.append(mi.get_name() if mi else 'None')
        if is_default(mi):
            bad.append(str(s.get_editor_property('material_slot_name')))
    pkg = p.split('.')[0]
    b = a.get_bounding_box()
    d = b.max - b.min
    out.append(f'{a.get_name()}\t{d.x:.0f}x{d.y:.0f}x{d.z:.0f}\tNanite {a.get_editor_property("nanite_settings").get_editor_property("enabled")}\t材质 {names}\t默认材质槽 {bad}\t被引用 {refs(pkg)}')

out.append('--- 框架')
for p in ['/Game/Robot/Meshes/SKM_Robot_v3'] + [f'/Game/Robot/Factions/SKM_{k}' for k in ('NEC_M', 'NEC_H', 'VHI_M', 'VHI_L', 'VHI_H', 'PHASE_M', 'PHASE_L', 'PHASE_H')]:
    a = eas.load_asset(p)
    if not a:
        out.append(f'{p} 不存在')
        continue
    bad = []
    for s in a.get_editor_property('materials'):
        mi = s.get_editor_property('material_interface')
        if is_default(mi):
            bad.append(str(s.get_editor_property('material_slot_name')))
    out.append(f'{a.get_name()}\t槽 {len(a.get_editor_property("materials"))}\t默认材质槽 {bad}')
open('D:/RobotSlice/Saved/equipment_census.txt', 'w', encoding='utf-8').write(chr(10).join(out))
unreal.log('[census] 完成')
