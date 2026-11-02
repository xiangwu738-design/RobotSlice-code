"""城区图（L_City）材质用法标记补齐（10-31 灰块）：关卡里 Nanite 网格用到的材质，母材质没勾「Used with Nanite」的，
游戏里会换成引擎默认材质（WorldGridMaterial，一整块灰格子；日志 LogMaterial: Warning: ... missing usage flag Nanite! Default Material will be used in game）。
用法标记在母材质上（实例跟着母材质走）：
  - 母材质在工程里（/Game/）：直接勾上、重编、存盘。
  - 母材质在引擎 / 插件里（比如 Poly Haven 盖布车 glTF 导入的实例，母材质是 /InterchangeAssets/gltf/M_Default）：不改引擎目录，
    把这条父链（中间的实例 + 母材质）复制一份到用它的实例旁边（名字加 _Nanite），复制的母材质勾上，工程里的实例改挂到复制的链上。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript "-script=D:/RobotSlice/tools/ue_city_fix_usage.py [check]" -AllowCommandletRendering -RenderOffscreen
参数 check = 只列不改。
"""
import sys

import unreal

CHECK = 'check' in sys.argv[1:]
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log(f'[usage] {msg}')


def path_of(o):
    return o.get_path_name().split('.')[0]


unreal.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_City')
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
need = {}     # 母材质路径 → 缺的标记
users = {}    # 母材质路径 → 用到它的材质（组件上那一层）
for actor in sub.get_all_level_actors():
    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = c.get_editor_property('static_mesh')
        if not mesh:
            continue
        nanite = mesh.get_editor_property('nanite_settings').enabled
        ism = isinstance(c, unreal.InstancedStaticMeshComponent)
        for i in range(c.get_num_materials()):
            m = c.get_material(i)
            if not m:
                continue
            base = m.get_base_material()
            if not base or path_of(base).startswith('/Engine/EngineMaterials'):
                continue
            key = path_of(base)
            flags = need.setdefault(key, set())
            if nanite and not base.get_editor_property('used_with_nanite'):
                flags.add('used_with_nanite')
            if ism and not base.get_editor_property('used_with_instanced_static_meshes'):
                flags.add('used_with_instanced_static_meshes')
            users.setdefault(key, {})[path_of(m)] = m
fix = {k: v for k, v in need.items() if v}
log(f'关卡用到的母材质 {len(need)} 个，缺用法标记的 {len(fix)} 个：{fix}')
for key, flags in fix.items():
    log(f'  {key} 缺 {sorted(flags)}，用到它的：{sorted(users[key])}')
if CHECK:
    raise SystemExit

for key, flags in fix.items():
    if key.startswith('/Game/'):
        base = eas.load_asset(key)
        for f in flags:
            base.set_editor_property(f, True)
        mel.recompile_material(base)
        log(f'  {key} 勾上 {sorted(flags)}，存盘 {"成功" if eas.save_loaded_asset(base, False) else "失败"}')
        continue
    # 引擎 / 插件的母材质：复制父链到工程里
    for mp, m in users[key].items():
        if not mp.startswith('/Game/') or not isinstance(m, unreal.MaterialInstanceConstant):
            log(f'  {mp} 不在工程里或不是材质实例，跳过')
            continue
        folder = mp.rsplit('/', 1)[0]
        chain = []          # 从 m 的父开始往上，直到母材质
        p = m.get_editor_property('parent')
        while p is not None:
            chain.append(p)
            p = p.get_editor_property('parent') if isinstance(p, unreal.MaterialInstance) else None
        copies = []
        for anc in chain:
            ap = path_of(anc)
            if ap.startswith('/Game/'):
                copies.append(anc)     # 工程里的就直接用（链在这里接回去）
                break
            dst = f'{folder}/{ap.rsplit("/", 1)[1]}_Nanite'
            cp = eas.load_asset(dst) if eas.does_asset_exist(dst) else eas.duplicate_asset(ap, dst)
            copies.append(cp)
        for a, b in zip(copies, copies[1:]):
            if isinstance(a, unreal.MaterialInstanceConstant) and path_of(a).startswith(folder):
                mel.set_material_instance_parent(a, b)
                eas.save_loaded_asset(a, False)
        top = copies[-1]
        if isinstance(top, unreal.Material) and path_of(top).startswith(folder):
            for f in flags:
                top.set_editor_property(f, True)
            mel.recompile_material(top)
            eas.save_loaded_asset(top, False)
        mel.set_material_instance_parent(m, copies[0])
        ok = eas.save_loaded_asset(m, False)
        log(f'  {mp}：父链 {[path_of(x) for x in chain]} 复制成 {[path_of(x) for x in copies]}，母材质勾上 {sorted(flags)}，实例存盘 {"成功" if ok else "失败"}')
