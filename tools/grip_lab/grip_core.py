# 握姿原型的公共部分（grip_proto2.py 里 exec）。数据和 Source/RobotSlice/MechWeapons.cpp 的 GunTable / BladeTable 一致（改了要一起改）。Blender 骨架空间（厘米）。和 C++ UMechMeshComponent 的握持一一对应。
import bpy
import math
from mathutils import Vector, Matrix

arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
S3 = Matrix.Diagonal((1, -1, 1))
FINGERS = ('index', 'middle', 'ring', 'pinky')
HALF_T = {'thumb': (1.15, 1.0, 0.9), 'index': (0.95, 0.88, 0.8), 'middle': (0.95, 0.88, 0.8), 'ring': (0.93, 0.85, 0.78),
          'pinky': (0.85, 0.8, 0.73)}
TIP = {'thumb': 3.1, 'index': 2.6, 'middle': 2.5, 'ring': 2.3, 'pinky': 2.1}
LIMIT = {'finger': ((-15, 95), (-10, 105), (-10, 80)), 'thumb': ((-40, 55), (-30, 85), (-30, 80))}
PALM_N = (1.35, -1.75)
CAV_K = -0.8          # 空腔中心在指根线上的位置（k）
CAV_GAP = 0.1         # 柄离手心
D_KN = 8.575          # k = CAV_K 处指根线的 d
CAV_BACK = 1.8        # 柄的后沿最少离手腕多远（别压进腕关节）


def rest(b):
    return arm.matrix_world @ arm.data.bones[b].matrix_local


def hand_frame_rest(s):
    H = rest('hand_' + s).translation.copy()
    hd = lambda b: rest(b).translation
    d = (hd('middle_01_' + s) - H).normalized()
    n = d.cross(hd('pinky_01_' + s) - hd('index_01_' + s)).normalized()
    if n.dot(hd('thumb_03_' + s) - H) < 0:
        n = -n
    k = d.cross(n)
    if k.dot(hd('thumb_01_' + s) - H) < 0:
        k = -k
    return H, k, n, d


KNUCKLES = ((2.65, 9.35), (0.0, 9.52), (-2.4, 8.89), (-4.2, 7.81))   # 四个指根 (k, d)


def cav_nd(half_d, half_n, tilt=0.225):
    """柄心在手坐标系的 n、d（k = CAV_K）。tilt = 柄心线沿 k 每厘米 d 变多少（柄轴的 d/k）：
    每根手指的指根到柄心线（沿 d）至少 half_d + 1.05，取最紧的那根；柄后沿离手腕至少 CAV_BACK。"""
    dc = min(dk - half_d - 1.05 - (kk - CAV_K) * tilt for kk, dk in KNUCKLES)
    return PALM_N[0] + half_n + CAV_GAP, max(dc, CAV_BACK + half_d)


class Handle:
    def __init__(self, C, A, U, hu, hv, rc, a0, a1):
        self.C, self.A = C, A.normalized()
        self.U = (U - self.A * U.dot(self.A)).normalized()
        self.V = self.A.cross(self.U)
        self.hu, self.hv, self.rc, self.a0, self.a1 = hu, hv, min(rc, hu, hv), a0, a1

    def sdf(self, p):
        q = p - self.C
        al = q.dot(self.A)
        dx = abs(q.dot(self.U)) - (self.hu - self.rc)
        dy = abs(q.dot(self.V)) - (self.hv - self.rc)
        out = math.hypot(max(dx, 0), max(dy, 0)) + min(max(dx, dy), 0) - self.rc
        return max(out, self.a0 - al, al - self.a1)


def palm_sdf(H, k, n, d):
    def f(p):
        q = p - H
        x, y, z = q.dot(k), q.dot(n), q.dot(d)
        dx = abs(x - (-0.7)) - 4.3
        dy = abs(y - (PALM_N[0] + PALM_N[1]) / 2) - (PALM_N[0] - PALM_N[1]) / 2
        dz = abs(z - 5.1) - 4.4
        return math.sqrt(max(dx, 0) ** 2 + max(dy, 0) ** 2 + max(dz, 0) ** 2) + min(max(dx, dy, dz), 0)
    return f


def rot_about(p, axis, ang):
    return Matrix.Translation(p) @ Matrix.Rotation(ang, 4, axis) @ Matrix.Translation(-p)


def solve(s, handle, trigger=False, v1=False):
    """v1=True：10-31 第一版拇指（C++ 的 -MechGripV1）；托护木（handle.support）两版一样。"""
    H, k, n, d = hand_frame_rest(s)
    palm = palm_sdf(H, k, n, d)
    out, log = {}, []
    caps = []      # 解好的食指、中指、无名指各节（线段两端 + 半厚）：拇指第二版的障碍
    for f in FINGERS:
        bones = [f'{f}_0{j}_{s}' for j in (1, 2, 3)]
        R = [rest(b) for b in bones]
        P = [m.translation.copy() for m in R]
        T = P[2] + (P[2] - P[1]).normalized() * TIP[f]
        ends = [P[1], P[2], T]
        axes = [(ends[j] - P[j]).normalized().cross(n).normalized() for j in range(3)]
        D = [Matrix.Identity(4) for _ in range(3)]
        angs = []
        tsign = 1.0
        for j in range(3):
            pivot = D[j] @ P[j]
            if f == 'thumb':
                axis = handle.A.copy()
                if j == 0:
                    end = D[j] @ ends[j]
                    cp = handle.C + handle.A * (pivot - handle.C).dot(handle.A)
                    tsign = 1.0 if axis.cross(end - pivot).dot(cp - pivot) > 0 else -1.0
                axis = axis * tsign
                lim = LIMIT['thumb'][j]
            else:
                axis = (D[j].to_3x3() @ axes[j]).normalized()
                lim = LIMIT['finger'][j]
            ht = HALF_T[f][j]

            def hit(a):
                Rm = rot_about(pivot, axis, math.radians(a))
                e = Rm @ (D[j] @ ends[j])
                for t in (0.3, 0.5, 0.7, 0.85, 1.0):
                    pt = pivot.lerp(e, t)
                    v = handle.sdf(pt)
                    if j > 0 or f == 'thumb':
                        v = min(v, palm(pt))
                    if v < ht:
                        return True
                return False

            if trigger and f == 'index':
                a = (10.0, 15.0, 8.0)[j]
            else:
                lo_, hi_ = lim
                a, prev = 0.0, 0.0
                if hit(0.0):
                    while a > lo_ and hit(a):
                        prev, a = a, a - 4.0
                    a = max(a, lo_)
                else:
                    while a < hi_ and not hit(a):
                        prev, a = a, a + 4.0
                    a = min(a, hi_)
                    if hit(a):
                        x0, x1 = prev, a
                        for _ in range(4):
                            m_ = (x0 + x1) / 2
                            x0, x1 = (x0, m_) if hit(m_) else (m_, x1)
                        a = x0
            angs.append(round(a))
            Rm = rot_about(pivot, axis, math.radians(a))
            for kk in range(j, 3):
                D[kk] = Rm @ D[kk]
        for j in range(3):
            out[bones[j]] = D[j] @ R[j]
        if f in ('index', 'middle', 'ring'):
            caps += [(D[j] @ P[j], D[j] @ ends[j], HALF_T[f][j]) for j in range(3)]
        log.append(f'{f}:{angs}')
    if v1 or getattr(handle, 'support', False):
        out_t, log_t = solve_thumb(s, handle, H, k, n, d, palm)
    else:
        out_t, log_t = solve_thumb_v2(s, handle, H, k, n, d, palm, caps)
    out.update(out_t)
    log.append(log_t)
    return out, ' '.join(log)


def seg_clear(handle, palm, a, b, ht, use_palm):
    """线段 a→b 上几个点离柄（和手掌）最近多少，减去半厚；负数 = 穿进去了。"""
    m = 1e9
    for t in (0.3, 0.5, 0.7, 0.85, 1.0):
        pt = a.lerp(b, t)
        v = handle.sdf(pt)
        if use_palm:
            v = min(v, palm(pt))
        m = min(m, v - ht)
    return m


def solve_thumb(s, handle, H, k, n, d, palm):
    """拇指：第一节绕柄轴转 α（包过去）、绕 d 转 β（对掌），网格搜；二、三节绕柄轴弯到碰上。
    目标：拇指尖落在柄的另一侧（刀 / 握把：压在四指外面；托护木：沿护木靠手腕那一侧）。"""
    bones = [f'thumb_0{j}_{s}' for j in (1, 2, 3)]
    R = [rest(b) for b in bones]
    P = [m.translation.copy() for m in R]
    T = P[2] + (P[2] - P[1]).normalized() * TIP['thumb']
    ends = [P[1], P[2], T]
    A = handle.A
    cp = handle.C + A * (P[0] - handle.C).dot(A)
    sgn = 1.0 if A.cross(ends[0] - P[0]).dot(cp - P[0]) > 0 else -1.0
    At = A * sgn
    t_ = (1.0 - (handle.C - H).dot(k)) / A.dot(k) if abs(A.dot(k)) > 0.3 else 0.0
    pax = handle.C + A * t_
    if getattr(handle, 'support', False):
        v = -(d - A * A.dot(d)).normalized()
        target = pax + v * (handle.hu + 1.2)
    else:
        v = (n - A * A.dot(n)).normalized()
        target = pax + v * (handle.hv + 2.6)
    best = None
    for al in range(-90, 91, 10):
        for be in range(-70, 71, 10):
            M0 = rot_about(P[0], At, math.radians(al)) @ rot_about(P[0], d, math.radians(be))
            D = [M0, M0, M0]
            pen = min(0.0, seg_clear(handle, palm, P[0], M0 @ ends[0], HALF_T['thumb'][0], False))
            angs = [al, be]
            for j in (1, 2):
                pivot = D[j] @ P[j]
                lo_, hi_ = LIMIT['thumb'][j]
                hit = lambda a_: seg_clear(handle, palm, pivot, rot_about(pivot, At, math.radians(a_)) @ (D[j] @ ends[j]), HALF_T['thumb'][j], True) < 0
                a_, prev = 0.0, 0.0
                if hit(0.0):
                    while a_ > lo_ and hit(a_):
                        prev, a_ = a_, a_ - 5.0
                    a_ = max(a_, lo_)
                else:
                    while a_ < hi_ and not hit(a_):
                        prev, a_ = a_, a_ + 5.0
                    a_ = min(a_, hi_)
                    if hit(a_):
                        a_ = prev
                Rm = rot_about(pivot, At, math.radians(a_))
                for kk in range(j, 3):
                    D[kk] = Rm @ D[kk]
                pen = min(pen, seg_clear(handle, palm, pivot, D[j] @ ends[j], HALF_T['thumb'][j], False))
                angs.append(a_)
            tip = D[2] @ T
            score = (tip - target).length + 30.0 * (-pen)
            if best is None or score < best[0]:
                best = (score, [m.copy() for m in D], angs, pen)
    out = {bones[j]: best[1][j] @ R[j] for j in range(3)}
    global LAST_TARGET
    LAST_TARGET = target
    return out, f'thumb:{best[2]} 离目标{best[0]:.1f} 穿{best[3]:.2f}'


def thumb_target(handle, H, k, n, d, P0):
    A = handle.A
    t_ = (1.0 - (handle.C - H).dot(k)) / A.dot(k) if abs(A.dot(k)) > 0.3 else 0.0
    pax = handle.C + A * t_
    if getattr(handle, 'support', False):
        return pax - (d - A * A.dot(d)).normalized() * (handle.hu + 1.2)
    return pax + (n - A * A.dot(n)).normalized() * (handle.hv + 2.6)


def solve_thumb_v2(s, handle, H, k, n, d, palm, caps):
    """10-31 拇指第二版（和 C++ UMechMeshComponent::SolveGrip 里「拇指第二版」一一对应，注释见那里）：
    二、三节绕拇指自己的弯曲轴屈；第一节 α（绕柄轴）/ β（绕 d）/ 扭（绕自身）；五个角粗搜取前三再细搜；四指也是障碍；
    打分 = 拇指肚离目标 + 30 × 穿深 + 0.04 × 反关节度数 + 0.6 × 第二、三节中点悬空。"""
    bones = [f'thumb_0{j}_{s}' for j in (1, 2, 3)]
    R = [rest(b) for b in bones]
    P = [m.translation.copy() for m in R]
    T = P[2] + (P[2] - P[1]).normalized() * TIP['thumb']
    ends = [P[1], P[2], T]
    A = handle.A
    cp = handle.C + A * (P[0] - handle.C).dot(A)
    At = A * (1.0 if A.cross(ends[0] - P[0]).dot(cp - P[0]) > 0 else -1.0)
    target = thumb_target(handle, H, k, n, d, P[0])
    flex = (P[1] - P[0]).cross(P[2] - P[1]).normalized()
    meta = (P[1] - P[0]).normalized()
    ht = HALF_T['thumb']

    def seg_dist(p, a, b):
        ab = b - a
        t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.dot(ab), 1e-6)))
        return (p - (a + ab * t)).length

    def obstacle(p, use_palm):
        v = handle.sdf(p)
        for a, b, h in caps:
            v = min(v, seg_dist(p, a, b) - h)
        return min(v, palm(p)) if use_palm else v

    def clear(a, b, half, use_palm):
        return min(obstacle(a.lerp(b, t), use_palm) - half for t in (0.3, 0.5, 0.7, 0.85, 1.0))

    def hang(a, b, half):
        return max(0.0, obstacle(a.lerp(b, 0.5), False) - half)

    def joint0(tw, al, be):
        M0 = rot_about(P[0], At, math.radians(al)) @ rot_about(P[0], d, math.radians(be)) @ rot_about(P[0], meta, math.radians(tw))
        return M0, min(0.0, clear(P[0], M0 @ ends[0], ht[0], False))

    def joint_n(j, prev, deg):
        pv = prev @ P[j]
        nxt = rot_about(pv, (prev.to_3x3() @ flex).normalized(), math.radians(deg)) @ prev
        return nxt, min(0.0, clear(pv, nxt @ ends[j], ht[j], True))

    hyper = lambda deg: 0.04 * max(0, -deg)

    def score(D1, D2, pen, a2, a3):
        pad = (D2 @ P[2]).lerp(D2 @ ends[2], 0.6)
        hanging = hang(D1 @ P[1], D1 @ P[2], ht[1]) + hang(D2 @ P[2], D2 @ ends[2], ht[2])
        return (pad - target).length - 30.0 * pen + hyper(a2) + hyper(a3) + 0.6 * hanging

    lo2, hi2 = int(LIMIT['thumb'][1][0]), int(LIMIT['thumb'][1][1])
    lo3, hi3 = int(LIMIT['thumb'][2][0]), int(LIMIT['thumb'][2][1])
    top = [(1e9, 0, 0, 0, 0, 0)] * 3          # (分, 扭, α, β, a2, a3)，从好到差

    def keep(c):
        if c[0] >= top[2][0]:
            return
        i = 2
        while i > 0 and top[i - 1][0] > c[0]:
            top[i] = top[i - 1]
            i -= 1
        top[i] = c

    for tw in range(-60, 61, 30):
        for al in range(-90, 91, 20):
            for be in range(-70, 71, 20):
                M0, pen0 = joint0(tw, al, be)
                if -30.0 * pen0 >= top[2][0]:
                    continue
                for a2 in range(lo2, hi2 + 1, 15):
                    D1, p1 = joint_n(1, M0, a2)
                    pen1 = min(pen0, p1)
                    if -30.0 * pen1 + hyper(a2) >= top[2][0]:
                        continue
                    for a3 in range(lo3, hi3 + 1, 15):
                        D2, p2 = joint_n(2, D1, a3)
                        keep((score(D1, D2, min(pen1, p2), a2, a3), tw, al, be, a2, a3))
    best = None
    for c in top:
        if c[0] >= 1e8:
            continue
        _, tw, al0, be0, a20, a30 = c
        for al in range(max(-90, al0 - 10), min(90, al0 + 10) + 1, 5):
            for be in range(max(-70, be0 - 10), min(70, be0 + 10) + 1, 5):
                M0, pen0 = joint0(tw, al, be)
                if best and -30.0 * pen0 >= best[0]:
                    continue
                for a2 in range(max(lo2, a20 - 10), min(hi2, a20 + 10) + 1, 5):
                    D1, p1 = joint_n(1, M0, a2)
                    pen1 = min(pen0, p1)
                    if best and -30.0 * pen1 + hyper(a2) >= best[0]:
                        continue
                    for a3 in range(max(lo3, a30 - 10), min(hi3, a30 + 10) + 1, 5):
                        D2, p2 = joint_n(2, D1, a3)
                        pen = min(pen1, p2)
                        sc = score(D1, D2, pen, a2, a3)
                        if best is None or sc < best[0]:
                            best = (sc, [M0, D1, D2], (al, be, tw, a2, a3), pen)
    D = best[1]
    tip = D[2] @ ends[2]
    pad = (D[2] @ P[2]).lerp(tip, 0.6)
    out = {bones[j]: D[j] @ R[j] for j in range(3)}
    global LAST_TARGET
    LAST_TARGET = target
    al, be, tw, a2, a3 = best[2]
    return out, (f'thumb2:[α{al} β{be} 扭{tw} {a2} {a3}] 分{best[0]:.1f} 拇指肚离目标{(pad - target).length:.1f} '
                 f'尖{(tip - target).length:.1f} 穿{best[3]:.2f}')


def apply_pose(mats):
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    for b in sorted(mats, key=lambda b: len(arm.data.bones[b].parent_recursive)):
        arm.pose.bones[b].matrix = arm.matrix_world.inverted() @ mats[b]
        bpy.context.view_layer.update()


def load_weapon(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    w = [o for o in bpy.data.objects if o not in before and o.type == 'MESH'][0]
    w.parent = None
    return w


def model_to_blender(Xb, Zb):
    """UE 模型坐标 → Blender：X → Xb、Z → Zb（单位向量），Y = -(Z × X)（镜像）。"""
    Xb = Xb.normalized()
    Zb = (Zb - Xb * Zb.dot(Xb)).normalized()
    Yb = -(Zb.cross(Xb))
    return Matrix((Xb, Yb, Zb)).transposed()


def weapon_world(Rbm, T):
    m = (Rbm @ S3).to_4x4() @ Matrix.Scale(100, 4)
    m.translation = T
    return m


def place_canonical(s, c):
    """按标准握法摆武器：返回（武器物体世界矩阵, 柄的形状）。"""
    H, k, n, d = hand_frame_rest(s)
    if c['kind'] == 'blade':
        a = c['dir']
        A = (k * a[0] + n * a[1] + d * a[2]).normalized()
        E = (d - A * d.dot(A)).normalized()
        Rbm = model_to_blender(A, E * (-1.0 if c.get('edge', -1) < 0 else 1.0))
        r = c['r']
        nc, dc = cav_nd(r, r, (A.dot(d) / A.dot(k)) if abs(A.dot(k)) > 0.3 else 0.0)
        cav = H + k * CAV_K + n * nc + d * dc
        Cm = Vector((c['gx'], c.get('yc', 0.0), c.get('zc', 0.0)))
        T = cav - Rbm @ Cm
        x0, x1 = c['range']
        shape = Handle(cav, A, Rbm @ Vector((0, 1, 0)), r, r, r, x0 - c['gx'], x1 - c['gx'])
    elif c['kind'] == 'gun':
        ux, uz = c['up']
        L = math.hypot(ux, uz)
        ux, uz = ux / L, uz / L
        tl = c.get('tilt', 0.225)          # 握把在手里斜多少（C++ FGun::GripHandDir 的 Z）
        kk_ = (k + d * tl).normalized()
        dd_ = (d - k * tl).normalized()
        Xb = dd_ * uz + kk_ * ux
        Zb = -dd_ * ux + kk_ * uz
        Rbm = model_to_blender(Xb, Zb)
        hd_, hn_ = c['half']
        nc, dc = cav_nd(hd_, hn_, tl)
        cav = H + k * CAV_K + n * nc + d * dc
        a0, a1 = c['along']
        ac = a1 - 4.6
        Cm = Vector((c['C'][0] + ux * ac, 0, c['C'][1] + uz * ac))
        T = cav - Rbm @ Cm
        shape = Handle(cav, kk_, dd_, hd_, hn_, c['corner'], a0 - ac, a1 - ac)
    else:   # fore：左手托护木
        kk_ = (k + d * 0.225).normalized()
        Rbm = model_to_blender(kk_, n)
        hy, hz = c['half']
        nc, dc = cav_nd(hy, hz)
        cav = H + k * CAV_K + n * nc + d * dc
        Cm = Vector((c['x'], 0, c['z']))
        T = cav - Rbm @ Cm
        shape = Handle(cav, kk_, d, hy, hz, min(hy, hz) * 0.6, -8, 8)
        shape.support = True
    return weapon_world(Rbm, T), shape


def setup():
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'MATERIAL'
    sc.display.shading.show_cavity = True
    sc.render.resolution_x = 640
    sc.render.resolution_y = 640
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.lens = 70
    cam.data.clip_start = 1
    cam.data.clip_end = 3000
    return cam


def shoot(cam, target, direction, dist, path):
    direction = Vector(direction).normalized()
    cam.location = target + direction * dist
    cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (-direction).to_track_quat('-Z', 'Y')
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


SW = 'swords/'
GN = 'guns/'
CASES = [
    dict(name='07双手刀_右', side='r', fbx=SW + 'SM_Sword07', kind='blade', dir=(0.93, -0.08, 0.37), r=1.5, gx=4.5, range=(-9, 9.5)),
    dict(name='07双手刀_左', side='l', fbx=SW + 'SM_Sword07', kind='blade', dir=(0.93, -0.08, 0.37), r=1.5, gx=-4.5, range=(-9, 9.5)),
    dict(name='11快刀_右', side='r', fbx=SW + 'SM_Sword11', kind='blade', dir=(0.93, -0.02, 0.37), r=1.5, gx=1.5, zc=-1.84, range=(-12, 7)),
    dict(name='11快刀_左', side='l', fbx=SW + 'SM_Sword11', kind='blade', dir=(0.93, -0.02, 0.37), r=1.5, gx=-7.0, zc=-1.84, range=(-12, 7)),
    dict(name='13单手刀_右', side='r', fbx=SW + 'SM_Sword13', kind='blade', dir=(0.78, 0.2, 0.59), r=1.6, gx=-3.0, zc=0.4, range=(-15, 3)),
    dict(name='14大剑_右', side='r', fbx=SW + 'SM_Sword14', kind='blade', dir=(0.97, 0.14, 0.19), r=2.6, gx=3.5, zc=0.8, range=(-22.5, 10)),
    dict(name='14大剑_左', side='l', fbx=SW + 'SM_Sword14', kind='blade', dir=(0.97, 0.14, 0.19), r=2.6, gx=-12.0, zc=1.2, range=(-22.5, 10)),
    dict(name='16双刀_右', side='r', fbx=SW + 'SM_Sword16', kind='blade', dir=(0.95, -0.04, 0.32), r=2.0, gx=2.0, zc=0.42, range=(-17, 15)),
    dict(name='16双刀_左', side='l', fbx=SW + 'SM_Sword16', kind='blade', dir=(0.95, -0.04, 0.32), r=2.0, gx=2.0, zc=0.42, range=(-17, 15)),
    dict(name='20钝器_右', side='r', fbx=SW + 'SM_Sword20', kind='blade', dir=(0.96, 0.18, 0.19), edge=1, r=2.0, gx=1.0, yc=0.1, zc=0.93, range=(-34, 5.5)),
    dict(name='20钝器_左', side='l', fbx=SW + 'SM_Sword20', kind='blade', dir=(0.96, 0.18, 0.19), edge=1, r=2.0, gx=-20.0, yc=0.1, zc=0.93, range=(-34, 5.5)),
    dict(name='03匕首_右', side='r', fbx=SW + 'SM_Sword03', kind='blade', dir=(-0.84, -0.21, -0.5), r=0.9, gx=-1.5, zc=0.4, range=(-7.5, 3)),
    dict(name='步枪_右', side='r', fbx=GN + 'SM_Gun3', kind='gun', C=(-1.8, 0.7), up=(0.2, 0.98), half=(4.4, 2.2), corner=1.6, along=(-6.2, 7.0), trigger=True),
    dict(name='步枪_左托', side='l', fbx=GN + 'SM_Gun3', kind='fore', x=30, z=11.5, half=(1.6, 2.5)),
    dict(name='冲锋枪_右', side='r', fbx=GN + 'SM_Gun6', kind='gun', C=(0.64, 2.3), up=(0.5, 0.86), half=(1.9, 1.6), corner=1.2, along=(-3.5, 4.5), trigger=True),
    dict(name='冲锋枪_左托', side='l', fbx=GN + 'SM_Gun6', kind='fore', x=22, z=4.2, half=(2.2, 1.9)),
    dict(name='霰弹枪_右', side='r', fbx=GN + 'SM_Gun11', kind='gun', C=(-6.45, 2.0), up=(0.91, 0.41), half=(3.2, 3.4), corner=2.2, along=(-12, 6), trigger=True),
    dict(name='霰弹枪_左托', side='l', fbx=GN + 'SM_Gun11', kind='fore', x=34, z=7.5, half=(3.3, 2.5)),
    dict(name='充能步枪_右', side='r', fbx=GN + 'SM_Gun2', kind='gun', C=(-8.0, 2.6), up=(-0.5, 0.87), half=(1.6, 2.1), corner=1.2, along=(-3.5, 3.5), trigger=True,
         tilt=-0.2),   # 10-31 第二版：斜杆反着斜过手心（FGun::GripHandDir），第一版 0.225
    dict(name='充能步枪_左托', side='l', fbx=GN + 'SM_Gun2', kind='fore', x=40, z=-0.3, half=(2.7, 3.0)),
    dict(name='狙击枪_右', side='r', fbx=GN + 'SM_Gun9', kind='gun', C=(1.1, 1.45), up=(0.2, 0.98), half=(2.1, 1.5), corner=1.0, along=(-3, 3.5), trigger=True),
    dict(name='狙击枪_左托', side='l', fbx=GN + 'SM_Gun9', kind='fore', x=44, z=4.1, half=(2.3, 3.0)),
    dict(name='双持手枪_右', side='r', fbx=GN + 'SM_Pistol7', kind='gun', C=(-0.6, -2.5), up=(0.2, 0.98), half=(3.0, 2.1), corner=1.5, along=(-4, 5.5), trigger=True),
    dict(name='双持手枪_左', side='l', fbx=GN + 'SM_Pistol7', kind='gun', C=(-0.6, -2.5), up=(0.2, 0.98), half=(3.0, 2.1), corner=1.5, along=(-4, 5.5), trigger=True),
]
