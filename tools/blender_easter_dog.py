"""相位电子机械狗（彩蛋，11-02）：Spot Mini 式底子 → 清理、缩放、腿缩短 → 套相位机体零件外壳（大一点的头、两盏眼灯、尾巴天线）→ 自建骨架 → 导出。
  blender -b --factory-startup -P tools/blender_easter_dog.py -- [键=值 ...] [--config 别的.json] [--no-render] [--no-anim]
参数全在 tools/easter_dog.json（零件来源 / 摆法 / 尺寸 / 材质分槽规则 / 动作名字对应），命令行 键=值 临时改，例：
  leg_length_scale=0.8   pieces.0.size=0.9   forward=+Y   body_bottom_m=0.42
输出：
  Inbox/_easter/export/SK_PDog.fbx（骨骼网格，厘米制，骨头 root/body/head/tail_antenna/leg_{fl,fr,bl,br}_{upper,lower}，材质槽 Shell/Frame/Accent）
  Inbox/_easter/export/A_PDog_{Idle,Walk,Run,Sit}.fbx（底子有对应动作才有；没有的话代码走程序化步态）
  Inbox/_easter/export/PDog_build.json（骨头位置（UE 坐标）、尺寸、面数、动作、零件位置 —— 写代码 / 查问题用）
  Inbox/_easter/build/PDog_build.blend（拼好的场景，可以打开手动改）、Inbox/_easter/preview/dog/*.png + _sheet.json
做法（全是现有资源）：底子按高度剖面分出身体和四条腿（每条腿按膝盖高度分上下段），腿往身体底方向压短；
外壳零件按 pieces 依次从阵营机体（lookdev/rig/factions/PHASE_*.blend，按骨头 / 材质挑）或组件库 / FBX 取来，按目标盒子缩放、摆放；
每块零件整块绑一根骨头（刚性），底子的顶点按分段绑。底子带骨架和动作时，按「每段的顶点主要跟哪根底子骨头动」对应过来，逐帧把旋转烤到新骨架上。
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import easter_blender_lib as L  # noqa: E402

NAME = 'PDog'
MESH = 'SK_PDog'


def shorten_fn(bb, s):
    """腿缩短：身体底以下的高度 × s，身体底以上整体往下挪 bb × (1 - s)（连续）"""
    return lambda z: z * s if z < bb else z - bb * (1.0 - s)


def leg_bones():
    return [f'leg_{k}_{s}' for k in L.LEG_KEYS for s in ('upper', 'lower')]


def classify(work, srcs, cfg):
    coords_all = [v for w in work for v in L.mesh_coords(w.data)]
    bb, knee, cx, cy = L.dog_split(coords_all, cfg)
    classes, votes = [], {}
    for w, src in zip(work, srcs):
        cl = [L.dog_class(v, bb, knee, cx, cy) for v in L.mesh_coords(w.data)]
        classes.append(cl)
        for c, sid in zip(cl, src):
            votes.setdefault(c, {})
            votes[c][sid] = votes[c].get(sid, 0) + 1
    drivers = {}
    for c in ['body'] + leg_bones():
        cnt = votes.get(c)
        if not cnt:
            L.warn(f'分段 {c} 没有顶点')
            drivers[c] = None
            continue
        tot = sum(cnt.values())
        sid, n = max(cnt.items(), key=lambda kv: kv[1])
        drivers[c] = sid if n >= 0.4 * tot else None
        top = sorted(cnt.items(), key=lambda kv: -kv[1])[:3]
        L.log(f'  {c}：{tot} 顶点，主要来自 ' + '、'.join(f'{k[1]}（{v / tot:.0%}）' for k, v in top) + ('' if drivers[c] else '（没有过四成的，不对应）'))
    # 同一个底子来源当了好几段的主来源（比如整个底子是一个物体）：只留顶点最多的那段
    seen = {}
    for c, sid in drivers.items():
        if sid:
            seen.setdefault(sid, []).append(c)
    for sid, cs in seen.items():
        if len(cs) > 1:
            keep = max(cs, key=lambda c: votes[c][sid])
            for c in cs:
                if c != keep:
                    drivers[c] = None
            L.log(f'  {sid[1]} 同时是 {cs} 的主来源，只对应 {keep}')
    if cfg.get('refine_by_rig', True):
        drv_to_cls = {sid: c for c, sid in drivers.items() if sid and sid[0] == 'bone'}
        moved = 0
        for cl, src in zip(classes, srcs):
            for i, sid in enumerate(src):
                c = drv_to_cls.get(sid)
                if c and cl[i] != c:
                    cl[i] = c
                    moved += 1
        if drv_to_cls:
            L.log(f'按底子骨架修正分段：{moved} 个顶点换了段（跟着自己的骨头走，膝盖处不撕开）')
    return bb, knee, cx, cy, classes, drivers


def driver_world(sid):
    """底子来源（骨头 / 物体）现在的世界矩阵"""
    kind, name = sid
    if kind == 'bone':
        an, bn = name.split('/', 1)
        a = bpy.data.objects[an]
        return a.matrix_world @ a.pose.bones[bn].matrix
    return bpy.data.objects[name].matrix_world.copy()


def driver_joint(sid, end, fz):
    if not sid or sid[0] != 'bone':
        return None
    an, bn = sid[1].split('/', 1)
    a = bpy.data.objects[an]
    b = a.data.bones[bn]
    p = a.matrix_world @ (b.head_local if end == 'head' else b.tail_local)
    return Vector((p.x, p.y, fz(p.z)))


def inside(p, box, pad):
    return box and all(box.lo[i] - pad <= p[i] <= box.hi[i] + pad for i in range(3))


def leg_joints(boxes, drivers, fz, bb_s, body):
    """每条腿：髋（上段骨头头）、膝（下段骨头头）、脚（下段骨头尾）。底子骨头位置合理就用，否则按分段盒子"""
    out = {}
    for k in L.LEG_KEYS:
        ub, lb = boxes.get(f'leg_{k}_upper'), boxes.get(f'leg_{k}_lower')
        sx = 1 if k[0] == 'f' else -1
        sy = 1 if k[1] == 'l' else -1
        if not ub and not lb:
            L.warn(f'腿 {k} 两段都是空的，骨头按身体四角放')
            c = body.at((0.75 * sx, 0.6 * sy, -1))
            out[k] = (Vector((c.x, c.y, bb_s)), Vector((c.x, c.y, bb_s * 0.5)), Vector((c.x, c.y, 0.0)))
            continue
        ub = ub or lb
        lb = lb or ub
        hip = Vector((ub.center.x, ub.center.y, bb_s))
        knee = Vector((lb.center.x, lb.center.y, lb.hi.z))
        foot = Vector((lb.center.x, lb.center.y, lb.lo.z))
        pad = 0.25 * max(ub.size.length, 1.0)
        dh = driver_joint(drivers.get(f'leg_{k}_upper'), 'head', fz)
        dk = driver_joint(drivers.get(f'leg_{k}_lower'), 'head', fz)
        how = '分段盒子'
        if dh and inside(dh, ub.union(body), pad):
            hip = dh
            how = '底子骨头'
        if dk and inside(dk, ub.union(lb), pad):
            knee = dk
        if (knee - hip).length < 1.0:
            knee = hip + Vector((0, 0, -max(1.0, (hip.z - foot.z) * 0.5)))
        if (foot - knee).length < 1.0:
            foot = knee + Vector((0, 0, -max(1.0, knee.z)))
        out[k] = (hip, knee, foot)
        L.log(f'  腿 {k}（{how}）：髋 {L.fmt(hip)} 膝 {L.fmt(knee)} 脚 {L.fmt(foot)} 米')
    return out


def piece_bone_ends(cfg, boxes, bone, default):
    """头 / 尾巴骨头：配置里 defines_bone 的零件，按 pivot / tip（零件盒子里的 -1..1 位置）定"""
    for spec in cfg.get('pieces', []):
        if spec.get('defines_bone') == bone and spec['name'] in boxes:
            b = boxes[spec['name']]
            if spec.get('mirror'):
                b = boxes.get(spec['name'] + '_0', b)
            h, t = b.at(spec.get('pivot', (0, 0, -1))), b.at(spec.get('tip', (0, 0, 1)))
            L.log(f'  骨头 {bone} 按零件 {spec["name"]}：头 {L.fmt(h)} 尾 {L.fmt(t)} 米')
            return h, t
    L.warn(f'没有零件定义骨头 {bone}（defines_bone），用默认位置')
    return default


def make_armature(joints, body, head_ends, tail_ends):
    for o in bpy.data.objects:
        if o.name == 'Armature' and not o.get('easter_arm'):
            o.name = 'Armature_base'
    ad = bpy.data.armatures.new('Armature')
    arm = bpy.data.objects.new('Armature', ad)
    arm['easter_arm'] = True
    bpy.context.scene.collection.objects.link(arm)
    L.select_only([arm])
    bpy.ops.object.mode_set(mode='EDIT')
    eb = ad.edit_bones

    def bone(name, h, t, parent=None):
        b = eb.new(name)
        h, t = Vector(h), Vector(t)
        if (t - h).length < 0.5:
            t = h + Vector((0, 0, 5.0))
        b.head, b.tail = h, t
        b.roll = 0.0
        b.use_deform = True
        if parent:
            b.parent = eb[parent]
            b.use_connect = False
        return b

    bone('root', (0, 0, 0), (15.0, 0, 0))
    bc = body.center
    bone('body', bc, bc + Vector((body.size.x * 0.4, 0, 0)), 'root')
    bone('head', head_ends[0], head_ends[1], 'body')
    bone('tail_antenna', tail_ends[0], tail_ends[1], 'body')
    for k, (hip, knee, foot) in joints.items():
        bone(f'leg_{k}_upper', hip, knee, 'body')
        bone(f'leg_{k}_lower', knee, foot, f'leg_{k}_upper')
    bpy.ops.object.mode_set(mode='OBJECT')
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    L.log(f'骨架 {len(ad.bones)} 根：{[b.name for b in ad.bones]}')
    return arm


def add_groups(o, per_vertex=None, bone=None):
    if per_vertex is None:
        g = o.vertex_groups.get(bone) or o.vertex_groups.new(name=bone)
        g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
        return
    by = {}
    for i, c in enumerate(per_vertex):
        by.setdefault(c, []).append(i)
    for c, ids in by.items():
        g = o.vertex_groups.get(c) or o.vertex_groups.new(name=c)
        g.add(ids, 1.0, 'REPLACE')


# ---------------------------------------------------------------- 动作
def match_clips(cfg, acts):
    import re
    out = []
    used = set()
    for name, pat in cfg.get('anims', {}).items():
        hit = next((a for a in acts if a.name not in used and re.search(pat, a.name)), None)
        if hit:
            used.add(hit.name)
            out.append((name, hit))
            L.log(f'  动作 {hit.name} → {name}')
        else:
            L.log(f'  {name}：底子里没有名字匹配 {pat!r} 的动作（代码用程序化步态）')
    rest = [a.name for a in acts if a.name not in used]
    if rest:
        L.log(f'  没用上的底子动作：{rest}（要用就改配置 anims 的正则）')
    return out


def bake_clip(cfg, arm, base_arm, act, out_name, drivers, rest_w):
    sc = bpy.context.scene
    ad = L.assign_action(base_arm, act)
    ad.use_nla = False
    base_arm.data.pose_position = 'POSE'
    f0, f1 = int(math.floor(act.frame_range[0])), int(math.ceil(act.frame_range[1]))
    if f1 <= f0:
        f1 = f0 + 1
    bones = {b: drivers[b] for b in ['body'] + leg_bones() if drivers.get(b) and drivers[b][0] == 'bone' and drivers[b][1].startswith(base_arm.name + '/')}
    if 'body' not in bones:
        L.warn(f'{out_name}：身体没有对应的底子骨头，动作里身体不动')
    O0 = {b: arm.matrix_world @ arm.data.bones[b].matrix_local for b in bones}
    inv = arm.matrix_world.inverted()

    def desired(b):
        return driver_world(bones[b]) @ rest_w[bones[b]].inverted() @ O0[b]

    drift = Vector((0, 0, 0))
    if 'body' in bones:
        sc.frame_set(f0)
        p0 = desired('body').translation.copy()
        sc.frame_set(f1)
        p1 = desired('body').translation.copy()
        drift = p1 - p0
        drift.z = 0
    secs = (f1 - f0) / max(sc.render.fps, 1)
    speed = drift.length / max(secs, 1e-6)
    strip = cfg.get('anim_strip_drift', True) and drift.length > 1.0
    L.log(f'  {out_name}：帧 {f0}..{f1}（{secs:.2f} 秒），对应骨头 {sorted(bones)}，身体水平移动 {drift.length / L.M:.2f} 米（{speed / L.M:.2f} 米/秒）'
          f'{"，去掉（原地动作，代码移动角色）" if strip else ""}')
    out = bpy.data.actions.new(out_name)
    out['easter_out'] = True
    oad = L.assign_action(arm, out)
    oad.use_nla = False
    levels = [['body'], [b for b in bones if b.endswith('_upper')], [b for b in bones if b.endswith('_lower')]]
    prev = {}
    for f in range(f0, f1 + 1):
        sc.frame_set(f)
        for lvl in levels:
            for b in lvl:
                if b not in bones:
                    continue
                pb = arm.pose.bones[b]
                m = desired(b)
                if b == 'body' and strip:
                    m = Matrix.Translation(-drift * ((f - f0) / (f1 - f0))) @ m
                pb.matrix = inv @ m
                if b != 'body':
                    pb.location = (0.0, 0.0, 0.0)
                q = pb.rotation_quaternion.copy()
                if b in prev:
                    q.make_compatible(prev[b])
                pb.rotation_quaternion = q
                prev[b] = q
                pb.keyframe_insert('rotation_quaternion', frame=f)
                if b == 'body':
                    pb.keyframe_insert('location', frame=f)
            bpy.context.view_layer.update()
    base_arm.data.pose_position = 'REST'
    return out, (f0, f1), speed / L.M, sorted(bones)


# ---------------------------------------------------------------- 主流程
def build():
    args = L.script_args()
    cfg = L.load_config('dog', args)
    flags = cfg['_flags']
    path = L.find_base('dog', cfg.get('base_file', ''))
    objs = L.load_base(path, cfg.get('pose_frame'))
    for o in objs:
        if o.name == 'Armature':
            o.name = 'Armature_base'
    meshes, arms, info = L.prepare_base(objs, cfg, 'dog')
    rest_w = {}
    for a in arms:
        for b in a.data.bones:
            rest_w[('bone', f'{a.name}/{b.name}')] = a.matrix_world @ b.matrix_local
    for o in objs:
        if o.type != 'ARMATURE' and o.name in bpy.data.objects:
            rest_w[('object', o.name)] = o.matrix_world.copy()
    work, srcs = L.bake_base(meshes)

    # ---- 分段、缩短腿
    bb, knee, cx, cy, classes, drivers = classify(work, srcs, cfg)
    s = cfg.get('leg_length_scale', 1.0)
    fz = shorten_fn(bb, s)
    for w, cl in zip(work, classes):
        co = L.mesh_coords(w.data)
        flat = []
        for v in co:
            flat += [v.x, v.y, fz(v.z)]
        w.data.vertices.foreach_set('co', flat)
        w.data.update()
        add_groups(w, per_vertex=cl)
    bb_s = fz(bb)
    L.log(f'腿缩短 ×{s}：身体底 {bb / L.M:.3f} → {bb_s / L.M:.3f} 米')
    boxes = {}
    pts = {}
    for w, cl in zip(work, classes):
        for v, c in zip(L.mesh_coords(w.data), cl):
            pts.setdefault(c, []).append(v)
    for c, p in pts.items():
        boxes[c] = L.Box.of(p)
        L.log(f'  盒子 {c}：{boxes[c]}')
    if 'body' not in boxes:
        raise L.EasterError('分不出身体（body_bottom_m 设得太高？）')
    boxes['all_base'] = L.Box.of(v for p in pts.values() for v in p)
    L.decimate_to(work, cfg.get('base_max_tris'), '底子')
    colors = cfg.get('preview_colors', {})
    L.log('底子分槽：')
    for w in work:
        L.log(f'  {w.name}（原 {w.get("easter_src_name")}）')
        L.apply_slots(w, cfg['base_slot_rules'], colors)

    # ---- 外壳零件
    pieces = L.build_pieces(cfg, boxes, is_bone=lambda t: t in L.DOG_BONES)
    for o, spec, bone in pieces:
        add_groups(o, bone=bone)
    body = boxes['body']
    head_def = (body.at((0.9, 0, 0.6)), body.at((1.3, 0, 1.4)))
    tail_def = (body.at((-1, 0, 1)), body.at((-1.4, 0, 2.0)))
    head_ends = piece_bone_ends(cfg, boxes, 'head', head_def)
    tail_ends = piece_bone_ends(cfg, boxes, 'tail_antenna', tail_def)
    joints = leg_joints(boxes, drivers, fz, bb_s, body)

    sheet = []
    out_dir = os.path.join(L.DIRS['preview'], 'dog')
    legend = []
    if '--no-render' not in flags:
        try:
            groups = [('底子', work)] + [(o.get('easter_piece', o.name), [o]) for o, _s, _b in pieces]
            merged = {}
            for label, ob in groups:
                merged.setdefault(label, []).extend(ob)
            legend = L.color_legend(list(merged.items()))
            L.render_views(work + [o for o, _s, _b in pieces], out_dir, 'dog_零件', ['front34', 'side', 'top'], sheet, color_type='OBJECT')
        except Exception as ex:
            L.warn(f'零件图渲染失败：{ex}')

    # ---- 骨架 + 合并
    arm = make_armature(joints, body, head_ends, tail_ends)
    mesh = L.join_objects(work + [o for o, _s, _b in pieces], MESH)
    mesh.parent = arm
    mesh.parent_type = 'OBJECT'
    mesh.matrix_parent_inverse = Matrix.Identity(4)
    mod = mesh.modifiers.new('Armature', 'ARMATURE')
    mod.object = arm
    unknown = [g.name for g in mesh.vertex_groups if g.name not in arm.data.bones]
    if unknown:
        L.warn(f'有顶点组不是骨头（这些顶点不会动）：{unknown}')
    missing_slots = [sname for i, sname in enumerate(L.SLOTS) if not any(p.material_index == i for p in mesh.data.polygons)]
    if missing_slots:
        L.warn(f'材质槽 {missing_slots} 没有面（UE 导入会丢掉这个槽，代码按槽名找要能处理缺槽）')
    tris = L.tris_of(mesh.data)
    full = L.obj_box(mesh)
    head_box = boxes.get(next((p['name'] for p in cfg['pieces'] if p.get('defines_bone') == 'head'), ''), None)
    L.log(f'合并：{MESH} {tris} 三角，包围盒 {full}')
    if head_box:
        L.log(f'头 / 身体：头高 {head_box.size.z / L.M:.2f} 米、身体高 {body.size.z / L.M:.2f} 米（比 {head_box.size.z / max(body.size.z, 1):.2f}）；'
              f'头宽 / 身体宽 {head_box.size.y / max(body.size.y, 1):.2f}')
    if tris > cfg.get('max_tris', 80000):
        L.warn(f'总三角 {tris} 超过 max_tris {cfg.get("max_tris")}：调 base_max_tris 或少几块零件')

    # ---- 动作
    clips = []
    if arms and '--no-anim' not in flags:
        base_arm = max(arms, key=lambda a: len(a.data.bones))
        acts = L.actions_for(base_arm)
        L.log(f'底子骨架 {base_arm.name} 的动作：{[a.name for a in acts]}')
        for out_name, act in match_clips(cfg, acts):
            try:
                clips.append((out_name,) + bake_clip(cfg, arm, base_arm, act, out_name, drivers, rest_w))
            except Exception as ex:
                L.warn(f'{out_name} 烤动作失败：{ex}')
        L.assign_action(arm, None)
        for pb in arm.pose.bones:
            pb.matrix_basis = Matrix.Identity(4)
    elif not arms:
        L.log('底子没有骨架：不导动作（代码用程序化步态）')

    # ---- 预览
    if '--no-render' not in flags:
        try:
            L.render_views([mesh], out_dir, 'dog', ['front34', 'side', 'front', 'top', 'rear34'], sheet, color_type='MATERIAL',
                           note=f'{tris} 三角 · 长 {full.size.x / L.M:.2f} 高 {full.size.z / L.M:.2f} 米')
            test_pose(arm)
            L.render_views([mesh], out_dir, 'dog_摆姿势', ['front34', 'side'], sheet, color_type='MATERIAL', note='检查绑定：歪头、摇尾、抬左前腿')
            for pb in arm.pose.bones:
                pb.matrix_basis = Matrix.Identity(4)
            for out_name, act, (f0, f1), _sp, _b in clips:
                L.assign_action(arm, act)
                for k, f in enumerate((f0, (2 * f0 + f1) // 3, (f0 + 2 * f1) // 3)):
                    bpy.context.scene.frame_set(f)
                    L.render_views([mesh], out_dir, f'dog_{out_name}_{k}', ['side'], sheet, color_type='MATERIAL', note=f'第 {f} 帧')
            L.assign_action(arm, None)
            for pb in arm.pose.bones:
                pb.matrix_basis = Matrix.Identity(4)
        except Exception as ex:
            L.warn(f'预览渲染失败（不影响导出）：{ex}')
        L.write_sheet(out_dir, sheet, f'相位机械狗 SK_PDog（{os.path.basename(path)}）', legend,
                      [f'腿缩短 ×{s}', f'三角 {tris}', f'动作 {[c[0] for c in clips] or "无"}'])

    # ---- 导出
    exp = L.DIRS['export']
    L.assign_action(arm, None)
    L.export_fbx(os.path.join(exp, f'{MESH}.fbx'), [arm, mesh], {'ARMATURE', 'MESH'})
    anims = []
    for out_name, act, (f0, f1), speed, bones in clips:
        L.assign_action(arm, act)
        sc = bpy.context.scene
        sc.frame_start, sc.frame_end = f0, f1
        L.export_fbx(os.path.join(exp, f'{out_name}.fbx'), [arm, mesh], {'ARMATURE', 'MESH'}, anim=True)
        anims.append({'name': out_name, 'source': act.name, 'frames': [f0, f1], 'fps': sc.render.fps,
                      'seconds': round((f1 - f0) / sc.render.fps, 3), 'root_speed_mps': round(speed, 3), 'bones': bones})
    L.assign_action(arm, None)
    report = {
        'asset': '/Game/Easter/Dog/SK_PDog', 'fbx': f'{MESH}.fbx', 'source': L.rel(path), 'note': '坐标 = UE 网格空间厘米 (x 前, y 右, z 上)',
        'scale_from_source': round(info['scale'], 5), 'forward': info['forward'], 'leg_length_scale': s, 'tris': tris,
        'size_m': [round(x / L.M, 3) for x in full.size], 'box_ue_cm': {'min': L.ue_vec(full.lo), 'max': L.ue_vec(full.hi)},
        'bones': [{'name': b.name, 'parent': b.parent.name if b.parent else None, 'head': L.ue_vec(b.head_local), 'tail': L.ue_vec(b.tail_local)}
                  for b in arm.data.bones],
        'slots': {sname: sum(len(p.vertices) - 2 for p in mesh.data.polygons if p.material_index == i) for i, sname in enumerate(L.SLOTS)},
        'boxes': {k: {'min': L.ue_vec(v.lo), 'max': L.ue_vec(v.hi)} for k, v in boxes.items()},
        'anims': anims, 'warnings': list(L.LOG.warnings),
    }
    # UE 里 y 取反后 min/max 的 y 对调
    for v in [report['box_ue_cm']] + list(report['boxes'].values()):
        v['min'][1], v['max'][1] = min(v['min'][1], v['max'][1]), max(v['min'][1], v['max'][1])
    jp = os.path.join(exp, f'{NAME}_build.json')
    json.dump(report, open(jp, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    L.log(f'报告 {L.rel(jp)}')
    L.save_blend(os.path.join(L.DIRS['build'], f'{NAME}_build.blend'))
    L.log(f'完成：SK_PDog {tris} 三角，动作 {len(anims)} 个；下一步 py -3 tools/easter_sheet.py Inbox/_easter/preview/dog，再跑 UE 导入')


def test_pose(arm):
    """摆一个检查用的姿势：歪头、尾巴摆、左前腿抬起（看零件有没有跟错骨头）"""
    def rot(name, axis, deg):
        pb = arm.pose.bones.get(name)
        if pb:
            pb.rotation_quaternion = Matrix.Rotation(math.radians(deg), 3, axis).to_quaternion()
    rot('head', 'Y', 18)
    rot('tail_antenna', 'X', 30)
    rot('leg_fl_upper', 'X', -35)
    rot('leg_fl_lower', 'X', 55)
    bpy.context.view_layer.update()


if __name__ == '__main__':
    L.run_main('easter', 'dog', build)
