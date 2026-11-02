"""肩甲擦头（10-31）：给一台阵营机体做「头 = 几个球、肩 = 一把采样点」的简化碰撞体（游戏里每帧按它把上臂 / 锁骨往外推开，见 MechShoulderClear.cpp），
再用同一套推法（甩肘 → 锁骨 + 手臂 IK → 歪头，和 C++ 一样）在 Blender 里把动作 FBX 套上去验证：推之前 / 推之后各有几帧头和肩真的穿插（BVH 三角面相交），推了多少度。
blender -b lookdev/rig/factions/<台>.blend -P tools/shoulder_head_proxy.py -- <输出.json> <每秒取几帧> <动作.fbx 通配符> [...]
输出 JSON：head_spheres [[x,y,z,r]...]、arm[l|r] [[x,y,z,骨头]...]（都是 UE 网格空间绑定姿势坐标，厘米：Blender (x,y,z) → UE (x,−y,z)）、验证结果。
tools/shoulder_head_table.py 把九台的 JSON 拼成 C++ 表。"""
import glob
import json
import math
import sys
from collections import defaultdict

import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

args = sys.argv[sys.argv.index('--') + 1:]
OUT = args[0]
FPS_TAKE = float(args[1])
files = []
for pat in args[2:]:
    files += sorted(glob.glob(pat, recursive=True))
MARGIN = 1.5          # 推到离头表面留多少（厘米）
N_HEAD = 4            # 头用几个球
N_ARM = 160           # 每边肩 / 上臂采样几个点（按面积撒点后最远点采样，约 3 厘米一个）
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == 'ARMATURE')
robot = next(o for o in scene.objects if o.type == 'MESH' and o.parent == arm)
me = robot.data
me.calc_loop_triangles()
gname = {g.index: g.name for g in robot.vertex_groups}
vbone = [gname[max(v.groups, key=lambda g: g.weight).group] if v.groups else '' for v in me.vertices]
rest = [robot.matrix_world @ v.co for v in me.vertices]
SIDES = {'l': ('clavicle_l', 'upperarm_l'), 'r': ('clavicle_r', 'upperarm_r')}


def kmeans_spheres(pts, k):
    """点云分 k 团，每团一个包住所有点的球（圆心 = 团的包围盒中心）"""
    pts = list(pts)
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    # 初始中心：沿最长轴均匀摆
    ax = max(range(3), key=lambda i: hi[i] - lo[i])
    cs = []
    for j in range(k):
        c = (lo + hi) * 0.5
        c[ax] = lo[ax] + (hi[ax] - lo[ax]) * (j + 0.5) / k
        cs.append(c)
    for _ in range(30):
        groups = [[] for _ in cs]
        for p in pts:
            groups[min(range(len(cs)), key=lambda j: (p - cs[j]).length_squared)].append(p)
        cs = [sum(g, Vector()) / len(g) if g else cs[j] for j, g in enumerate(groups)]
    out = []
    for g in groups:
        if not g:
            continue
        glo = Vector([min(p[i] for p in g) for i in range(3)])
        ghi = Vector([max(p[i] for p in g) for i in range(3)])
        c = (glo + ghi) * 0.5
        out.append((c, max((p - c).length for p in g)))
    return out


def far_sample(idx, n):
    """最远点采样：均匀铺满这组顶点"""
    idx = list(idx)
    if len(idx) <= n:
        return idx
    pick = [max(idx, key=lambda i: rest[i].z)]
    d = {i: (rest[i] - rest[pick[0]]).length for i in idx}
    while len(pick) < n:
        j = max(idx, key=lambda i: d[i])
        pick.append(j)
        for i in idx:
            d[i] = min(d[i], (rest[i] - rest[j]).length)
    return pick


head_idx = [i for i, b in enumerate(vbone) if b == 'head']
spheres = kmeans_spheres([rest[i] for i in head_idx], N_HEAD)
import random
random.seed(7)
arm_pts = {}
for s_, bones in SIDES.items():
    tris = [t for t in me.loop_triangles if vbone[t.vertices[0]] in bones]
    cand = []  # (位置, 骨头)
    for t in tris:
        a_, b_, c_ = (rest[i] for i in t.vertices)
        area = (b_ - a_).cross(c_ - a_).length * 0.5
        k = max(1, int(area / 6.0))  # 约每 6 平方厘米一个点
        for _ in range(k):
            u, v = random.random(), random.random()
            if u + v > 1:
                u, v = 1 - u, 1 - v
            cand.append((a_ + (b_ - a_) * u + (c_ - a_) * v, vbone[t.vertices[0]]))
        for i in t.vertices:
            cand.append((rest[i], vbone[i]))
    # 最远点采样到 N_ARM 个
    pick = [max(range(len(cand)), key=lambda i: cand[i][0].z)]
    d = [(c[0] - cand[pick[0]][0]).length for c in cand]
    while len(pick) < min(N_ARM, len(cand)):
        j = max(range(len(cand)), key=lambda i: d[i])
        pick.append(j)
        pj = cand[j][0]
        d = [min(d[i], (cand[i][0] - pj).length) for i in range(len(cand))]
    arm_pts[s_] = [cand[i] for i in pick]
print('[proxy] 头球', [(tuple(round(x, 1) for x in c), round(r, 1)) for c, r in spheres], '肩点', {s: len(v) for s, v in arm_pts.items()})

# ---------------------------------------------------------------- 推法（和 MechShoulderClear.cpp 一样）
pb_by = {b.name: b for b in arm.pose.bones}


def bone_mat(name):
    return arm.matrix_world @ pb_by[name].matrix


def rest_mat(name):
    return arm.matrix_world @ arm.data.bones[name].matrix_local


def cur_points(s):
    out = []
    for p0, b in arm_pts[s]:
        out.append((bone_mat(b) @ rest_mat(b).inverted() @ p0, b))
    return out


def cur_spheres():
    m = bone_mat('head') @ rest_mat('head').inverted()
    return [(m @ c, r) for c, r in spheres]


def rotate_bone(name, axis, ang):
    """绕这根骨头的关节（骨头起点）在骨架空间转 ang 弧度（子骨头跟着走）"""
    pb = pb_by[name]
    m = arm.matrix_world @ pb.matrix
    piv = m.translation.copy()
    R = Matrix.Translation(piv) @ Matrix.Rotation(ang, 4, axis) @ Matrix.Translation(-piv)
    pb.matrix = arm.matrix_world.inverted() @ (R @ m)
    bpy.context.view_layer.update()


def chest_axes():
    """胸（spine_05）在骨架空间的前方、左方（按绑定姿势：Blender 里机体朝 -Y、左 +X）"""
    m = bone_mat('spine_05') @ rest_mat('spine_05').inverted()
    f = (m.to_3x3() @ Vector((0, -1, 0))).normalized()
    l = (m.to_3x3() @ Vector((1, 0, 0))).normalized()
    return f, l


def penetration(s):
    sph = cur_spheres()
    best = 0.0
    for p, b in cur_points(s):
        for c, r in sph:
            best = max(best, r + MARGIN - (p - c).length)
    return best


def solve_1d(s, bone, axis, max_deg):
    """绕胸的前方轴把这根骨头往外转，二分找刚好不穿的最小角度（max_deg 内）；返回转了多少度"""
    if penetration(s) <= 0:
        return 0.0
    rotate_bone(bone, axis, math.radians(max_deg))
    if penetration(s) > 0:
        return max_deg  # 转满也不够：留着转满，交给下一步
    lo, hi = 0.0, max_deg
    cur = max_deg
    for _ in range(8):
        mid = (lo + hi) * 0.5
        rotate_bone(bone, axis, math.radians(mid - cur))
        cur = mid
        if penetration(s) > 0:
            lo = mid
        else:
            hi = mid
    rotate_bone(bone, axis, math.radians(hi - cur))
    return hi


ARM_MAX = 40.0
CLAV_MAX = 20.0
HEAD_MAX = 15.0


def swivel(s, ang):
    """绕「肩 → 手腕」这条线转上臂（肘绕着转），手腕再反着转回来：手和武器的位置、朝向都不变"""
    ua, hd = 'upperarm_' + s, 'hand_' + s
    S = bone_mat(ua).translation.copy()
    H = bone_mat(hd).translation.copy()
    ax = (H - S)
    if ax.length < 1e-3:
        return
    ax.normalize()
    hm = bone_mat(hd).copy()
    rotate_bone(ua, ax, ang)
    pb = pb_by[hd]
    pb.matrix = arm.matrix_world.inverted() @ hm
    bpy.context.view_layer.update()


def solve_swivel(s, max_deg):
    if penetration(s) <= 0:
        return 0.0
    ua = 'upperarm_' + s
    S = bone_mat(ua).translation
    E = bone_mat('lowerarm_' + s).translation
    H = bone_mat('hand_' + s).translation
    hc = bone_mat('head').translation
    ax = (H - S).normalized()
    # 肘往离头远的那边甩
    sign = 1.0 if ax.cross(E - S).dot(E - hc) > 0 else -1.0
    cur = 0.0

    def go(deg):
        nonlocal cur
        swivel(s, math.radians(sign * (deg - cur)))
        cur = deg
    go(max_deg)
    if penetration(s) > 0:
        return max_deg
    lo, hi = 0.0, max_deg
    for _ in range(8):
        mid = (lo + hi) * 0.5
        go(mid)
        if penetration(s) > 0:
            lo = mid
        else:
            hi = mid
    go(hi)
    return hi


def deepest(s):
    sph = cur_spheres()
    best = (-1e9, None, None)
    for p, b in cur_points(s):
        for c, r in sph:
            d = r + MARGIN - (p - c).length
            if d > best[0]:
                best = (d, p, c)
    return best


def two_bone(root, joint, end, target, l1, l2):
    """和 AnimationCore::SolveTwoBoneIK 一样（不拉伸）：肘在 root-joint-end 原来那个平面里"""
    d = target - root
    dist = min(d.length, l1 + l2 - 1e-3)
    dirv = d.normalized()
    pole = joint + (joint - (root + end) * 0.5)
    pn = pole - root
    pn = (pn - dirv * pn.dot(dirv))
    pn = pn.normalized() if pn.length > 1e-6 else Vector((0, 0, 1))
    a = (l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist)
    a = max(-1.0, min(1.0, a))
    return root + dirv * (l1 * a) + pn * (l1 * math.sqrt(max(0.0, 1 - a * a)))


def clear(s):
    if penetration(s) <= 0:
        return 0.0
    ua, la, hd, cl = 'upperarm_' + s, 'lowerarm_' + s, 'hand_' + s, 'clavicle_' + s
    hm0 = bone_mat(hd).copy()
    H0 = hm0.translation.copy()
    deg = 0.0
    # ① 甩肘
    deg += solve_swivel(s, ARM_MAX)
    # ② 锁骨 + IK
    if penetration(s) > 0:
        d, p, c = deepest(s)
        Cp = bone_mat(cl).translation.copy()
        S = bone_mat(ua).translation.copy()
        E = bone_mat(la).translation.copy()
        H = bone_mat(hd).translation.copy()
        Ac = (p - Cp).cross(p - c)
        if Ac.length > 1e-6:
            Ac.normalize()
            l1, l2 = (E - S).length, (H - E).length

            def apply(dg):
                R = Matrix.Rotation(math.radians(dg), 4, Ac)
                rotate_bone(cl, Ac, math.radians(dg))
                S1 = bone_mat(ua).translation.copy()
                E1 = bone_mat(la).translation.copy()
                H1 = bone_mat(hd).translation.copy()
                E2 = two_bone(S1, E1, H1, H0, l1, l2)
                q = (E1 - S1).normalized().rotation_difference((E2 - S1).normalized())
                ax, an = q.to_axis_angle()
                if an > 1e-6:
                    rotate_bone(ua, ax, an)
                EN = bone_mat(la).translation.copy()
                q2 = (bone_mat(hd).translation - EN).normalized().rotation_difference((H0 - EN).normalized())
                ax2, an2 = q2.to_axis_angle()
                if an2 > 1e-6:
                    rotate_bone(la, ax2, an2)
                pb_by[hd].matrix = arm.matrix_world.inverted() @ Matrix.Translation(bone_mat(hd).translation) @ hm0.to_3x3().to_4x4()
                bpy.context.view_layer.update()
            # 二分（每次从当前姿势试，试完复原：存下四根骨头的 matrix_basis）
            saved = {n: pb_by[n].matrix_basis.copy() for n in (cl, ua, la, hd)}

            def restore():
                for n, m in saved.items():
                    pb_by[n].matrix_basis = m
                bpy.context.view_layer.update()

            def tryd(dg):
                apply(dg)
                v = penetration(s)
                restore()
                return v
            if tryd(CLAV_MAX) > 0:
                best = CLAV_MAX
            else:
                lo, hi = 0.0, CLAV_MAX
                for _ in range(7):
                    mid = (lo + hi) * 0.5
                    if tryd(mid) > 0:
                        lo = mid
                    else:
                        hi = mid
                best = hi
            apply(best)
            deg += best
    # ③ 歪头：离最深那一点远的方向
    if penetration(s) > 0:
        d, p, c = deepest(s)
        nk = bone_mat('neck_01').translation.copy()
        Ah = (c - nk).cross(c - p)
        if Ah.length > 1e-6:
            Ah.normalize()
            saved = pb_by['neck_01'].matrix_basis.copy()

            def tryh(dg):
                rotate_bone('neck_01', Ah, math.radians(dg))
                v = penetration(s)
                pb_by['neck_01'].matrix_basis = saved
                bpy.context.view_layer.update()
                return v
            if tryh(HEAD_MAX) > 0:
                best = HEAD_MAX
            else:
                lo, hi = 0.0, HEAD_MAX
                for _ in range(7):
                    mid = (lo + hi) * 0.5
                    if tryh(mid) > 0:
                        lo = mid
                    else:
                        hi = mid
                best = hi
            rotate_bone('neck_01', Ah, math.radians(best))
            deg += best
    return deg


# ---------------------------------------------------------------- BVH 验证
groups = defaultdict(list)
for t in me.loop_triangles:
    b = vbone[t.vertices[0]]
    if b == 'head':
        groups['头'].append(tuple(t.vertices))
    elif b in SIDES['l']:
        groups['l'].append(tuple(t.vertices))
    elif b in SIDES['r']:
        groups['r'].append(tuple(t.vertices))
used = sorted({i for k in groups for tri in groups[k] for i in tri})


def overlaps():
    dg = bpy.context.evaluated_depsgraph_get()
    ev = robot.evaluated_get(dg)
    m = ev.to_mesh()
    co = {i: m.vertices[i].co.copy() for i in used}
    ev.to_mesh_clear()
    t = {}
    for k, tris in groups.items():
        idx = sorted({i for tr in tris for i in tr})
        remap = {i: j for j, i in enumerate(idx)}
        t[k] = BVHTree.FromPolygons([co[i] for i in idx], [[remap[i] for i in tr] for tr in tris], all_triangles=True)
    return {s: len(t['头'].overlap(t[s])) if s in t and '头' in t else 0 for s in ('l', 'r')}


arm.animation_data_create()
for pb in arm.pose.bones:
    pb.matrix_basis.identity()
bpy.context.view_layer.update()
base = overlaps()
result = {'clips': {}}
tot = {'before': 0, 'after': 0, 'frames': 0, 'pushed': 0, 'max_deg': 0.0}
for f in files:
    before_objs = set(bpy.data.objects)
    try:
        bpy.ops.import_scene.fbx(filepath=f, automatic_bone_orientation=False)
    except Exception as e:
        print('[proxy] 读不了', f, e)
        continue
    new = [o for o in bpy.data.objects if o not in before_objs]
    src = next((o for o in new if o.type == 'ARMATURE'), None)
    act = src.animation_data.action if src and src.animation_data else None
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    if act is None:
        continue
    arm.animation_data.action = act
    if hasattr(arm.animation_data, 'action_slot') and getattr(act, 'slots', None):
        arm.animation_data.action_slot = act.slots[0]
    f0, f1 = act.frame_range
    src_fps = scene.render.fps / scene.render.fps_base
    name = f.replace('\\', '/').split('/')[-1][:-4]
    n = int((f1 - f0) / src_fps * FPS_TAKE)
    cb = ca = 0
    worst = []
    for i in range(n + 1):
        fr = f0 + i / FPS_TAKE * src_fps
        scene.frame_set(int(fr), subframe=fr - int(fr))
        o1 = overlaps()
        hit1 = any(o1[s] > base[s] for s in o1)
        degs = {s: clear(s) for s in ('l', 'r')}
        o2 = overlaps()
        hit2 = any(o2[s] > base[s] for s in o2)
        # 推过的骨头下一帧会被动作重新覆盖（frame_set 重新求值），不用复原
        cb += hit1
        ca += hit2
        tot['frames'] += 1
        tot['pushed'] += any(v > 0.01 for v in degs.values())
        tot['max_deg'] = max(tot['max_deg'], *degs.values())
        if hit1 or hit2 or any(v > 0.01 for v in degs.values()):
            worst.append((round(i / FPS_TAKE, 3), o1, o2, {k: round(v, 1) for k, v in degs.items()}))
    tot['before'] += cb
    tot['after'] += ca
    result['clips'][name] = {'frames': n + 1, 'before': cb, 'after': ca, 'detail': worst}
    print('[proxy] %-34s 擦头帧 %d → %d / %d；推了 %d 帧 最多 %.1f 度；仍穿 %s' % (
        name, cb, ca, n + 1, sum(1 for w in worst if any(v > 0.01 for v in w[3].values())),
        max([max(w[3].values()) for w in worst] or [0]), [(w[0], w[2]) for w in worst if any(w[2][s] > base[s] for s in w[2])][:4]))
    bpy.data.actions.remove(act)
print('[proxy] 合计 擦头帧 %d → %d / %d，推了 %d 帧，最多 %.1f 度' % (tot['before'], tot['after'], tot['frames'], tot['pushed'], tot['max_deg']))


def ue(v):
    return [round(v.x, 2), round(-v.y, 2), round(v.z, 2)]


result['head_spheres'] = [ue(c) + [round(r, 2)] for c, r in spheres]
result['arm'] = {s: [ue(p0) + [b] for p0, b in pts] for s, pts in arm_pts.items()}
result['total'] = tot
result['blend'] = bpy.data.filepath
json.dump(result, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
