"""11-02 去组件 · 换进游戏（kind=py，commandlet）：
① 备份现在游戏用的九台导出（lookdev/rig/factions/<台>.fbx / .blend / _export.json）到 D:/归档/RobotSlice_去组件前_20261102（已有就不覆盖）；
② 把 lookdev/rig/factions/kitfree/fix/ 里去组件 + 法线重算的九台拷过去；
③ 跑现成的 tools/ue_import_factions.py（NEC_L + all = 九台；NEC_L 覆盖 /Game/Robot/Meshes/SKM_Robot_v3）；
④ 素材件材质换成自己的：MI_Kit_Hydraulics 的母材质改成 MI_Robot_Frame（骨架金属）、MI_Kit_Pipes 改成 MI_Robot_Rubber（橡胶），清掉贴图参数；
   原来的组件贴图 T_Kit_*（lookdev/kit/tex 从组件库烤出来的）没人引用了就删掉。日志 [kfdeploy]。"""
import os
import shutil
import sys

import unreal

ROOT = 'D:/RobotSlice'
FDIR = f'{ROOT}/lookdev/rig/factions'
NEW = f'{FDIR}/kitfree/fix'
BACK = 'D:/归档/RobotSlice_去组件前_20261102'
KEYS = ['NEC_L', 'NEC_M', 'NEC_H', 'VHI_L', 'VHI_M', 'VHI_H', 'PHASE_L', 'PHASE_M', 'PHASE_H']
log = lambda *a: unreal.log_warning('[kfdeploy] ' + ' '.join(str(x) for x in a))

missing = [k for k in KEYS if not os.path.exists(f'{NEW}/{k}.fbx')]
if missing:
    log('错误：kitfree/fix 里缺', missing, '——不换')
    raise SystemExit
os.makedirs(BACK, exist_ok=True)
for k in KEYS:
    for ext in ('.fbx', '.blend', '_export.json'):
        src, dst = f'{FDIR}/{k}{ext}', f'{BACK}/{k}{ext}'
        if os.path.exists(src) and not os.path.exists(dst):
            shutil.copy2(src, dst)
        new = f'{NEW}/{k}{ext}'
        if os.path.exists(new):
            shutil.copy2(new, src)
log('备份到', BACK, '；九台已换成去组件版')

# ③ 导入（照搬命令行参数的方式跑现成脚本）
imp = f'{ROOT}/tools/ue_import_factions.py'
saved = sys.argv
sys.argv = [imp, 'NEC_L', 'all']
try:
    exec(compile(open(imp, encoding='utf-8').read(), imp, 'exec'), {'__name__': '__main__', '__file__': imp})
finally:
    sys.argv = saved
log('导入跑完（详细见 [factions] 行）')

# ④ 材质
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
for slot, parent in (('MI_Kit_Hydraulics', 'MI_Robot_Frame'), ('MI_Kit_Pipes', 'MI_Robot_Rubber')):
    mi = unreal.load_asset(f'/Game/Robot/Materials/{slot}')
    par = unreal.load_asset(f'/Game/Robot/Materials/{parent}')
    if mi is None or par is None:
        log('没有', slot, '或', parent)
        continue
    old = mi.get_editor_property('parent')
    mel.clear_all_material_instance_parameters(mi)
    mi.set_editor_property('parent', par)
    mel.update_material_instance(mi)
    eal.save_loaded_asset(mi, False)
    log(slot, '母材质', old.get_name() if old else None, '→', parent)
ar = unreal.AssetRegistryHelpers.get_asset_registry()
for a in ar.get_assets_by_path('/Game/Robot/Textures', recursive=True):
    n = str(a.asset_name)
    if not n.startswith('T_Kit_'):
        continue
    pkg = str(a.package_name)
    refs = [str(r) for r in (ar.get_referencers(pkg, unreal.AssetRegistryDependencyOptions(True, True, True, True, True)) or []) if str(r) != pkg]
    if refs:
        log('留着', pkg, '还有人引用', refs[:4])
    else:
        log('删掉组件贴图', pkg, eal.delete_asset(pkg))
log('完成')
