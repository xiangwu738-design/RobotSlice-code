"""在草稿工程 D:/RobotSlice_AnimLab 里跑（重定向结果只在内存里，不保存任何资源）：
把 UE4 商城动作包里现成的 AnimSequence（.uasset，UE4 人形骨架）重定向到 UE5 人形（SKM_Manny_Simple，和本工程模板人形同一套骨架），
导出 FBX + 动作信息 locomotion.json —— 做法和输出格式照抄 gasp_retarget_ue4_fbx.py，只是把「导入 FBX」换成「在给定 /Game 路径下找出所有 AnimSequence」。

重定向器现建：源 = IK_UE4_Mannequin_Retarget（从示例拷来的），目标 = IK_UE5_Mannequin_Retarget，链按名字自动对应、目标姿势按源自动对齐。
源网格用和这些动作同一个骨架的骨骼网格（通常是包自带的 SK_Mannequin）；同一个包里动作分属不同骨架就按骨架分组，各建一个重定向器。
包里 InPlace 和 Root(Motion) 两版目录都有时，两版目录里的动作一律加后缀 _InPlace / _Root，两版都导出；其他同名冲突按目录名加后缀。
根运动操作明确指定 root → root（默认会取 IK Rig 的重定向根 pelvis，root 骨不动、位移跑进 pelvis）。
加性动作（瞄准偏移姿势）按完整姿势重定向、去掉加性标记再导出，FBX 里是完整姿势。

UnrealEditor-Cmd "D:/RobotSlice_AnimLab/AnimLab.uproject" -run=pythonscript -script="D:/RobotSlice/tools/animlab_retarget_uasset.py <包的 /Game 路径> <输出目录> [名字过滤，逗号分隔的通配符，如 Dual_*]" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
（Git Bash 里要加 MSYS_NO_PATHCONV=1，路径不能带空格）

另外把每个动作的抽查数据（UE4 源和 UE5 结果在 1/4、1/2、3/4 处 hand_r / hand_l / foot_l / foot_r / head 的组件空间位置）写到 <草稿工程>/_qa/<输出目录名>.json。
"""
import collections
import fnmatch
import json
import os
import re
import sys

import unreal

SRC = sys.argv[1].rstrip('/')
OUT = sys.argv[2]
PATTERNS = [p for p in sys.argv[3].split(',') if p] if len(sys.argv) > 3 else ['*']
os.makedirs(OUT, exist_ok=True)
TAG = os.path.basename(os.path.normpath(OUT))
QA_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), '_qa')
os.makedirs(QA_DIR, exist_ok=True)
TMP = '/Game/__RobotSliceUE4'
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.AnimationLibrary
SoT = unreal.RetargetSourceOrTarget
ar = unreal.AssetRegistryHelpers.get_asset_registry()


def log(msg):
    unreal.log(f'[animlab] {msg}')


mesh5 = unreal.load_asset('/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple')
ik4 = unreal.load_asset('/Game/Characters/UE4_Mannequin/Rigs/IK_UE4_Mannequin_Retarget')
ik5 = unreal.load_asset('/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget')
assert mesh5 and ik4 and ik5, (mesh5, ik4, ik5)

# ---- 找出包里的 AnimSequence（蒙太奇、混合空间、动画蓝图不要）
ar.scan_paths_synchronous([SRC], True)
all_datas = ar.get_assets_by_path(SRC, True)
cls = lambda d: str(d.asset_class_path.asset_name)
seq_datas = [d for d in all_datas if cls(d) == 'AnimSequence']
match = lambda name: any(fnmatch.fnmatchcase(name, p) for p in PATTERNS)
picked = {str(d.package_name): d for d in seq_datas if match(str(d.asset_name))}
if PATTERNS != ['*']:
    # 过滤时：名字匹配的混合空间 / 瞄准偏移用到的原始姿势序列也带上
    by_pkg = {str(d.package_name): d for d in seq_datas}
    opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
    for d in all_datas:
        if cls(d) in ('AnimSequence',) or not match(str(d.asset_name)):
            continue
        for dep in ar.get_dependencies(d.package_name, opts) or []:
            if str(dep) in by_pkg and str(dep) not in picked:
                picked[str(dep)] = by_pkg[str(dep)]
                log(f'{d.asset_name}（{cls(d)}）用到的 {by_pkg[str(dep)].asset_name} 一并导出')
log(f'{SRC} 下 AnimSequence {len(seq_datas)} 个，选中 {len(picked)} 个（过滤 {PATTERNS}）；其他类型：'
    + ', '.join(f'{k} {v}' for k, v in collections.Counter(cls(d) for d in all_datas if cls(d) != 'AnimSequence').most_common()))

failed = {}  # 源包名 -> 原因
src_seqs = {}  # 源包名 -> AnimSequence
for pkg, d in sorted(picked.items()):
    seq = d.get_asset()
    if not isinstance(seq, unreal.AnimSequence) or not seq.get_skeleton():
        failed[pkg] = '加载失败或没有骨架'
        continue
    src_seqs[pkg] = seq

# ---- 导出名：同名动作分在不同目录时按目录加后缀（InPlace / RootMotion 两版都要）
def place_marker(folder):
    """目录路径里（从里往外）第一个 InPlace / Root 目录 → 后缀；像 Battle/Root/Dodge 这种嵌套的也认"""
    for part in reversed(folder.split('/')):
        if re.fullmatch(r'(?i)in_?place', part):
            return '_InPlace'
        if re.fullmatch(r'(?i)root(_?motion)?', part):
            return '_Root'
    return None


def folder_suffixes(pkgs):
    folders = [p.rsplit('/', 1)[0] for p in pkgs]
    marks = [place_marker(f) for f in folders]
    if all(marks) and len(set(marks)) == len(marks):
        return marks
    # 否则用各自路径里不相同的、最靠里的那一级目录名（路径是别人前缀的那个不加，如 Battle/Root 与 Battle/Root/Combo_Sample）
    parts = [f.split('/') for f in folders]
    out = []
    for i, ps in enumerate(parts):
        diff = [x for j, x in enumerate(ps) if any(j >= len(o) or o[j] != x for k, o in enumerate(parts) if k != i)]
        out.append('_' + diff[-1] if diff else '')
    if len(set(out)) < len(out):
        out = [s + f'_{i}' for i, s in enumerate(out)]
    return out


names = {}
by_name = collections.defaultdict(list)
marks_in_pack = {place_marker(p.rsplit('/', 1)[0]) for p in src_seqs}
both = '_InPlace' in marks_in_pack and '_Root' in marks_in_pack
for pkg in src_seqs:
    base = pkg.rsplit('/', 1)[1]
    m = place_marker(pkg.rsplit('/', 1)[0]) if both else None
    # 包里 InPlace / Root 两版都有：两版目录里的动作一律加后缀，名字一看就知道是哪版
    by_name[base + m if m and not base.lower().endswith(m.lower()) else base].append(pkg)
for base, pkgs in by_name.items():
    if len(pkgs) == 1:
        names[pkgs[0]] = base
    else:
        for pkg, suf in zip(pkgs, folder_suffixes(pkgs)):
            names[pkg] = base + suf
dup = [n for n, c in collections.Counter(names.values()).items() if c > 1]
assert not dup, f'导出名仍重复：{dup}'


# ---- 按骨架分组，找同骨架的骨骼网格当源网格
def mesh_for_skeleton(skel):
    cands = []
    for root in (SRC, '/Game'):
        for d in ar.get_assets_by_path(root, True):
            if cls(d) != 'SkeletalMesh':
                continue
            tag = str(d.get_tag_value('Skeleton') or '')
            if tag and skel.get_path_name() not in tag:
                continue
            m = d.get_asset()
            if m and m.skeleton == skel:
                cands.append(m)
        if cands:
            break
    if not cands:
        try:
            pm = skel.get_preview_mesh()
            if pm:
                cands.append(pm)
        except Exception:
            pass
    # 优先叫 SK_Mannequin 的
    cands.sort(key=lambda m: (m.get_name() != 'SK_Mannequin', m.get_path_name()))
    return cands[0] if cands else None


groups = collections.defaultdict(list)  # 骨架路径 -> [源包名]
skels = {}
for pkg, seq in src_seqs.items():
    sk = seq.get_skeleton()
    skels[sk.get_path_name()] = sk
    groups[sk.get_path_name()].append(pkg)

QA_BONES = ['hand_r', 'hand_l', 'foot_l', 'foot_r', 'head', 'pelvis']


def comp_pos(seq, mesh, t):
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property('optional_skeletal_mesh', mesh)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, t, o)
    out = {}
    for b in QA_BONES:
        p = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD).translation
        out[b] = [round(p.x, 2), round(p.y, 2), round(p.z, 2)]
    return out


meta = {}
qa = {}
report = {'src': SRC, 'patterns': PATTERNS, 'anim_sequences_in_path': len(seq_datas), 'picked': len(picked), 'groups': []}
for gi, (skpath, pkgs) in enumerate(sorted(groups.items())):
    skel = skels[skpath]
    mesh4 = mesh_for_skeleton(skel)
    log(f'骨架 {skpath}：{len(pkgs)} 个动作，源网格 {mesh4.get_path_name() if mesh4 else "没找到"}')
    report['groups'].append({'skeleton': skpath, 'mesh': mesh4.get_path_name() if mesh4 else None, 'count': len(pkgs)})
    if not mesh4:
        for p in pkgs:
            failed[p] = '找不到同骨架的骨骼网格'
        continue
    try:  # 两套人形参考姿势下的骨骼位置（看身高比例、脚踝高度这些系统性差别）
        for k, sk in (('ref_ue4', skel), ('ref_ue5', mesh5.skeleton)):
            rp = unreal.AnimPoseExtensions.get_reference_pose(sk)
            report['groups'][-1][k] = {b: [round(v, 2) for v in (lambda t: (t.x, t.y, t.z))(
                unreal.AnimPoseExtensions.get_bone_pose(rp, b, unreal.AnimPoseSpaces.WORLD).translation)] for b in QA_BONES}
    except Exception as e:
        log(f'参考姿势读取出错：{e}')

    # ---- 现建 UE4 → UE5 重定向器（源预览网格用包自带的网格）
    rtg = tools.create_asset(f'RTG_UE4_to_UE5_{gi}', TMP, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.set_ik_rig(SoT.SOURCE, ik4)
    c.set_ik_rig(SoT.TARGET, ik5)
    c.set_preview_mesh(SoT.SOURCE, mesh4)
    c.set_preview_mesh(SoT.TARGET, mesh5)
    c.add_default_ops()
    c.assign_ik_rig_to_all_ops(SoT.SOURCE, ik4)
    c.assign_ik_rig_to_all_ops(SoT.TARGET, ik5)
    c.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    c.auto_align_all_bones(SoT.TARGET)
    # 坑：根运动操作默认从 IK Rig 取「根」，两边 IK Rig 的重定向根都是 pelvis，结果 root 骨一动不动、整段位移跑进了 pelvis。
    # 这里明确指定 root → root（目标盆骨 pelvis），根运动才会留在 root 骨上（导出 FBX 后 root_delta 才对）
    for i in range(c.get_num_retarget_ops()):
        oc = c.get_op_controller(i)
        if isinstance(oc, unreal.IKRetargetRootMotionController):
            oc.set_source_root_bone('root')
            oc.set_target_root_bone('root')
            oc.set_target_pelvis_bone('pelvis')
            log(f'根运动操作：源根 {oc.get_source_root_bone()} → 目标根 {oc.get_target_root_bone()}，目标盆骨 {oc.get_target_pelvis_bone()}')
    log(f'重定向器 {gi}：操作 {c.get_num_retarget_ops()} 个')

    # ---- 按源目录分批（同名的 InPlace / RootMotion 两版不能进同一个输出目录）
    by_folder = collections.defaultdict(list)
    for p in pkgs:
        by_folder[p.rsplit('/', 1)[0]].append(p)
    for fi, (folder, fpkgs) in enumerate(sorted(by_folder.items())):
        inputs = unreal.IKRetargetBatchOperationInputs()
        inputs.set_editor_property('assets_to_retarget', [picked[p] for p in fpkgs])
        inputs.set_editor_property('source_mesh', mesh4)
        inputs.set_editor_property('target_mesh', mesh5)
        inputs.set_editor_property('ik_retarget_asset', rtg)
        inputs.set_editor_property('target_path', f'{TMP}/Out/g{gi}_f{fi}')
        inputs.set_editor_property('include_referenced_assets', False)
        inputs.set_editor_property('overwrite_existing_files', True)
        inputs.set_editor_property('retain_additive_flags', False)  # 加性姿势按完整姿势导出
        results = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
        out_by_name = {}
        for d in results:
            a = d.get_asset()
            if isinstance(a, unreal.AnimSequence):
                out_by_name[str(a.get_name())] = a
        log(f'  {folder}：{len(fpkgs)} 个 → 重定向出 {len(out_by_name)} 个')
        for p in fpkgs:
            base = p.rsplit('/', 1)[1]
            seq = out_by_name.get(base)
            if seq is None:
                failed[p] = '重定向没有产出'
                continue
            src = src_seqs[p]
            name = names[p]
            length = lib.get_sequence_length(seq)
            r0 = lib.get_bone_pose_for_time(seq, 'root', 0.0, False).translation
            r1 = lib.get_bone_pose_for_time(seq, 'root', length, False).translation
            task = unreal.AssetExportTask()
            task.object = seq
            task.filename = f'{OUT}/{name}.fbx'
            task.automated = True
            task.prompt = False
            task.replace_identical = True
            task.exporter = unreal.AnimSequenceExporterFBX()
            opt = unreal.FbxExportOption()
            opt.set_editor_property('export_preview_mesh', False)
            task.options = opt
            ok = unreal.Exporter.run_asset_export_task(task)
            if not ok or not os.path.isfile(f'{OUT}/{name}.fbx'):
                failed[p] = 'FBX 导出失败'
                continue
            meta[name] = {'length': length, 'root_delta': [r1.x - r0.x, r1.y - r0.y, r1.z - r0.z], 'speed': 0.0, 'markers': []}
            # ---- 抽查数据：源（UE4）和结果（UE5）同一时刻的骨骼组件空间位置
            try:
                slen = lib.get_sequence_length(src)
                s0 = lib.get_bone_pose_for_time(src, 'root', 0.0, False).translation
                s1 = lib.get_bone_pose_for_time(src, 'root', slen, False).translation
                q = {'src': p, 'length': length, 'src_length': slen,
                     'root_delta': [round(v, 2) for v in meta[name]['root_delta']],
                     'src_root_delta': [round(s1.x - s0.x, 2), round(s1.y - s0.y, 2), round(s1.z - s0.z, 2)],
                     'additive_src': str(src.get_editor_property('additive_anim_type')),
                     'frames': {}}
                for frac in (0.25, 0.5, 0.75):
                    t = length * frac
                    q['frames'][str(frac)] = {'t': round(t, 3), 'ue4': comp_pos(src, mesh4, min(t, slen)), 'ue5': comp_pos(seq, mesh5, t)}
                qa[name] = q
            except Exception as e:
                log(f'  抽查 {name} 出错：{e}')
            log(f'  {name}: {length:.2f}s 导出成功')

json.dump(meta, open(f'{OUT}/locomotion.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
report['failed'] = failed
report['exported'] = len(meta)
json.dump({'report': report, 'anims': qa}, open(os.path.join(QA_DIR, f'{TAG}.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
for p, why in failed.items():
    log(f'失败 {p}：{why}')
log(f'完成：选中 {len(picked)} 个，导出 {len(meta)} 个，失败 {len(failed)} 个 → {OUT}（不保存任何资源）')
