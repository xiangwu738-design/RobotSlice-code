# 补材质用途标记（11-01）：打包版日志里「missing usage flag ... Default Material will be used in game」的材质。
# 编辑器里缺标记会现场补上，所以只在打包版里露馅：飞刀（KnifeMeshes 实例化网格，MI_Sword03）、街道的盖布汽车、园区全息牌都变成默认灰格子。
# 标记要加在实例的母材质上。改前先把要保存的 .uasset 拷到 D:\归档\RobotSlice_材质用途_20261101。
# 跑法：UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script=D:/RobotSlice/tools/ue_fix_material_usage.py -unattended -nosplash
import os
import shutil
import unreal

FIXES = [
    ('/Game/Weapons/Materials/MI_Sword03', unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES),
    ('/Game/Env/PolyHaven/covered_car/covered_car_2k/Materials/covered_car', unreal.MaterialUsage.MATUSAGE_NANITE),
    ('/Game/Env/PolyHaven/covered_car/covered_car_2k/Materials/covered_car1', unreal.MaterialUsage.MATUSAGE_NANITE),
    ('/Game/Env/TechPark/Materials/MI_HTS_HoloInfoA', unreal.MaterialUsage.MATUSAGE_NANITE),
]
BACKUP = 'D:/归档/RobotSlice_材质用途_20261101'


def base_material(asset):
    m = asset
    while isinstance(m, unreal.MaterialInstance):
        m = m.get_editor_property('parent')
    return m


def disk_path(pkg):
    return 'D:/RobotSlice/Content/' + pkg[len('/Game/'):] + '.uasset'


done = set()
for path, usage in FIXES:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        unreal.log_warning(f'[usage] 找不到 {path}')
        continue
    base = base_material(asset)
    if base is None:
        unreal.log_warning(f'[usage] {path} 没有母材质')
        continue
    pkg = base.get_path_name().split('.')[0]
    if not pkg.startswith('/Game/'):
        # 盖布汽车的母材质是 Interchange 插件的 /InterchangeAssets/gltf/M_Default：引擎 / 插件的资源不改
        unreal.log_warning(f'[usage] {path} 的母材质不在工程里（{pkg}），不改')
        continue
    src = disk_path(pkg)
    dst = os.path.join(BACKUP, pkg[len('/Game/'):] + '.uasset')
    if not os.path.exists(dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
    need = unreal.MaterialEditingLibrary.set_material_usage(base, usage)
    unreal.log(f'[usage] {path} → 母材质 {pkg} 加 {usage}（要重编：{need}）')
    done.add(pkg)

for pkg in sorted(done):
    ok = unreal.EditorAssetLibrary.save_asset(pkg, only_if_is_dirty=False)
    unreal.log(f'[usage] 保存 {pkg}：{ok}')
