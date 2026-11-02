"""11-02 去组件：机体用到的组件库零件的自建替代件（Blender 里 import 用）。
用户：「能不能保留大体形状的情况下，去除这些组件」「可以，开始重建吧」。
机体脚本（lookdev/factions/dingshan/ds_common.py、lookdev/robot_lookdev.py）所有组件零件都经过 kit_source(名字, 库, 轴) 进来：
它把原件烘进零件自己的主轴坐标系（x = 长度方向 u、y = v、z = u×v，中心在原点）返回 (网格, 沿 u 的长度)，
调用方再按两端点缩放、按 thick / bend 调粗细和弯度。这里按 tools/blender_kit_measure.py 量的功能尺寸（总长、粗细、销孔 / 缸口位置、
弯曲摆幅）从零建一份同样坐标系、同样长度的网格，**不复制原件网格**；patch(模块) 把模块里的 kit_source 换成这里的。

替代件：
  Hydraulics_T1_02  液压杆：活塞杆端耳座（销孔 0.13）+ 活塞杆 + 缸口（0.55）+ 缸筒（带一道箍）+ 缸底耳座（销孔 1.72），销轴沿 y。
                    减震（robot_lookdev.kit_coilover_meshes）在 0.55 处把它切成两段、活塞杆 0.42 以内整体外移、缸筒 1.10 以外整体内移，
                    所以 0.42 / 1.10 两侧都留了顶点环。
  Pipes_07          双管线缆：两根并排的管（在弯曲面的法向上错开）+ 两头接头，中心线照原件的弯（中段往 -y 鼓 0.67）。
  Pipes_11          波纹管：一根粗管 + 一圈圈的箍（只在非游戏版预览里用）。
材质：都用 MI_Kit_Hydraulics / MI_Kit_Pipes 这两个槽名（游戏里涂装 / 材质按槽名对应），这里新建纯色金属 / 橡胶，不带原件贴图。
"""
import math

import bmesh
import bpy
from mathutils import Matrix, Vector

T1_LEN = 1.8629          # 原件沿 u 的总长（素材单位），调用方按它算缩放
P07_LEN = 2.3773
P11_LEN = 2.2429

_CACHE = {}


def material(name, color, metal, rough):
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        b = m.node_tree.nodes.get('Principled BSDF')
        if b is not None:
            b.inputs['Base Color'].default_value = (*color, 1.0)
            b.inputs['Metallic'].default_value = metal
            b.inputs['Roughness'].default_value = rough
    m.diffuse_color = (*color, 1.0)
    m['kitfree'] = True
    return m


def _smooth_sides(bm, axis):
    """侧面（法线垂直于轴）平滑、端面平"""
    a = Vector(axis)
    for f in bm.faces:
        f.smooth = abs(f.normal.dot(a)) < 0.3


def cyl(bm, p0, p1, r0, r1=None, segs=16, cap=True):
    """p0 → p1 一段圆柱 / 圆台（加进 bm）"""
    r1 = r0 if r1 is None else r1
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    L = d.length
    tmp = bmesh.new()
    bmesh.ops.create_cone(tmp, cap_ends=cap, cap_tris=False, segments=segs, radius1=r0, radius2=r1, depth=L)
    _smooth_sides(tmp, (0, 0, 1))
    rot = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_matrix().to_4x4()
    bmesh.ops.transform(tmp, matrix=Matrix.Translation((p0 + p1) / 2) @ rot, verts=tmp.verts)
    me = bpy.data.meshes.new('_t')
    tmp.to_mesh(me)
    tmp.free()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)


def box(bm, lo, hi, bevel=0.0):
    tmp = bmesh.new()
    bmesh.ops.create_cube(tmp, size=1.0)
    lo, hi = Vector(lo), Vector(hi)
    s = hi - lo
    bmesh.ops.transform(tmp, matrix=Matrix.Translation((lo + hi) / 2) @ Matrix.Diagonal((s.x, s.y, s.z, 1)), verts=tmp.verts)
    if bevel > 0:
        bmesh.ops.bevel(tmp, geom=list(tmp.edges), offset=bevel, segments=1, affect='EDGES')
    me = bpy.data.meshes.new('_t')
    tmp.to_mesh(me)
    tmp.free()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)


def _finish(bm, name, mat):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    for p in me.polygons:
        p.material_index = 0
    me.update()
    return me


def hydraulics_t1():
    """液压杆，x 沿长度（0 在活塞杆端，最后整体挪到中心在原点），y = 销轴"""
    bm = bmesh.new()
    X = lambda u: u - T1_LEN / 2  # noqa: E731
    # 活塞杆端耳座：销轴凸台（沿 y）+ 耳板 + 销
    cyl(bm, (X(0.13), -0.105, 0), (X(0.13), 0.105, 0), 0.10, segs=16)
    box(bm, (X(0.02), -0.075, -0.085), (X(0.24), 0.075, 0.085), bevel=0.012)
    cyl(bm, (X(0.13), -0.1345, 0), (X(0.13), 0.1345, 0), 0.04, segs=10)
    # 活塞杆：0.22 → 0.60，0.30 / 0.50 留环（减震拉长活塞杆时 0.42 两侧都要有顶点）
    for a, b in ((0.22, 0.30), (0.30, 0.50), (0.50, 0.60)):
        cyl(bm, (X(a), 0, 0), (X(b), 0, 0), 0.068, segs=16, cap=(a == 0.22))
    cyl(bm, (X(0.205), 0, 0), (X(0.235), 0, 0), 0.085, segs=16)        # 杆端螺母
    # 缸口：密封压盖 + 一道细环
    cyl(bm, (X(0.55), 0, 0), (X(0.65), 0, 0), 0.133, segs=20)
    cyl(bm, (X(0.65), 0, 0), (X(0.70), 0, 0), 0.124, segs=20)
    # 缸筒：0.70 → 1.52，1.15 留环（减震缩短缸筒时 1.10 两侧都要有顶点）；1.43–1.48 一道箍
    for a, b in ((0.70, 1.15), (1.15, 1.43), (1.48, 1.52)):
        cyl(bm, (X(a), 0, 0), (X(b), 0, 0), 0.118, segs=20)
    cyl(bm, (X(1.43), 0, 0), (X(1.48), 0, 0), 0.1255, segs=20)
    cyl(bm, (X(1.52), 0, 0), (X(1.56), 0, 0), 0.125, 0.112, segs=20)   # 缸底
    # 缸底耳座：叉形耳板（两片，夹着销轴凸台）+ 销
    for s in (-1, 1):
        box(bm, (X(1.55), s * 0.13 - 0.03, -0.075), (X(1.82), s * 0.13 + 0.03, 0.075), bevel=0.01)
        box(bm, (X(1.55), s * 0.13 - 0.03, -0.09), (X(1.62), s * 0.13 + 0.03, 0.09), bevel=0.01)
    cyl(bm, (X(1.72), -0.10, 0), (X(1.72), 0.10, 0), 0.075, segs=16)
    cyl(bm, (X(1.72), -0.1345, 0), (X(1.72), 0.1345, 0), 0.035, segs=10)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    return _finish(bm, '液压杆（自建）', material('MI_Kit_Hydraulics', (0.25, 0.26, 0.27), 0.85, 0.38))  # 和原件在预览里的灰度一样（原件 diffuse 0.25）


def _catmull(pts, n):
    """Catmull-Rom 过点，返回 n 个点"""
    P = [pts[0]] + pts + [pts[-1]]
    out = []
    for i in range(n):
        t = i / (n - 1) * (len(pts) - 1)
        k = min(int(t), len(pts) - 2)
        f = t - k
        p0, p1, p2, p3 = (Vector(P[k + j]) for j in range(4))
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * f + (2 * p0 - 5 * p1 + 4 * p2 - p3) * f * f + (-p0 + 3 * p1 - 3 * p2 + p3) * f ** 3))
    return out


def sweep(bm, path, r, segs=10, cap=True):
    """沿折线 path（Vector 列表）扫一根圆管"""
    rings = []
    for i, p in enumerate(path):
        t = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]).normalized()
        side = t.cross(Vector((0, 0, 1)))
        if side.length < 1e-4:
            side = t.cross(Vector((0, 1, 0)))
        side.normalize()
        up = side.cross(t).normalized()
        rings.append([bm.verts.new(p + r * (math.cos(2 * math.pi * k / segs) * side + math.sin(2 * math.pi * k / segs) * up)) for k in range(segs)])
    for a, b in zip(rings, rings[1:]):
        for k in range(segs):
            f = bm.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
            f.smooth = True
    if cap:
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[-1])


def _pipe_path(length, n=28):
    # 中心线：两端在 v≈0，中段往 -v 鼓（最深 0.67，在 1/3 处），w 方向略往 -w 偏；按原件的弯量取的几个控制点
    ctrl = [(0.0, -0.04, 0.0), (0.25, -0.29, 0.01), (0.45, -0.57, -0.02), (0.74, -0.666, -0.06), (1.24, -0.52, -0.088),
            (1.73, -0.18, -0.066), (2.03, -0.027, -0.053), (2.33, -0.05, -0.077)]
    k = length / 2.33
    return [Vector((x * k - length / 2, y, z)) for x, y, z in ctrl], n


def pipes_07():
    bm = bmesh.new()
    ctrl, n = _pipe_path(P07_LEN)
    mid = _catmull(ctrl, n)
    for s in (-1, 1):
        path = [p + Vector((0, 0, s * 0.058)) for p in mid]
        sweep(bm, path, 0.056, segs=10)
    for p, q in ((mid[0], mid[1]), (mid[-1], mid[-2])):
        d = (q - p).normalized()
        cyl(bm, p - d * 0.02, p + d * 0.10, 0.085, segs=14)          # 接头
        cyl(bm, p + d * 0.10, p + d * 0.13, 0.072, segs=14)
    return _finish(bm, '双管线缆（自建）', material('MI_Kit_Pipes', (0.08, 0.085, 0.09), 0.0, 0.62))


def pipes_11():
    bm = bmesh.new()
    ctrl, n = _pipe_path(P11_LEN, 36)
    mid = _catmull([Vector((c.x, c.y * 1.6, c.z)) for c in ctrl], n)
    sweep(bm, mid, 0.11, segs=12)
    for i in range(2, n - 2, 2):
        d = (mid[i + 1] - mid[i - 1]).normalized()
        cyl(bm, mid[i] - d * 0.012, mid[i] + d * 0.012, 0.13, segs=12)
    return _finish(bm, '波纹管（自建）', material('MI_Kit_Pipes', (0.08, 0.085, 0.09), 0.0, 0.62))


BUILDERS = {'Hydraulics_T1_02': (hydraulics_t1, T1_LEN), 'Pipes_07': (pipes_07, P07_LEN), 'Pipes_11': (pipes_11, P11_LEN)}


def kit_source(name, lib, axes=None):
    """和 ds_common / robot_lookdev 的 kit_source 同样的签名和返回值"""
    if name not in BUILDERS:
        raise RuntimeError(f'[kitfree] 没有 {name} 的自建替代件（{lib}）：先在 tools/kitfree_parts.py 加上')
    key = (name, axes)
    if key not in _CACHE:
        fn, L = BUILDERS[name]
        _CACHE[key] = (fn(), L)
        print(f'[kitfree] 自建 {name}：{len(_CACHE[key][0].polygons)} 面，长 {L}')
    return _CACHE[key]


def spring_mesh(turns=7, wire=0.06, segs=8, steps_per_turn=16):
    """减震弹簧：直径 1、沿 +x 从 0 到 spring_len（robot_lookdev.kit_coilover 按 spring_len 缩放）"""
    bm = bmesh.new()
    length = turns * 0.17
    path = []
    n = turns * steps_per_turn
    for i in range(n + 1):
        a = 2 * math.pi * i / steps_per_turn
        path.append(Vector((length * i / n, 0.5 * math.cos(a), 0.5 * math.sin(a))))
    sweep(bm, path, wire, segs=segs)
    return bm, length


def patch(module, label=''):
    """把模块里的 kit_source 换成自建件；顺便清掉它已经缓存的原件"""
    if hasattr(module, 'KIT_CACHE'):
        module.KIT_CACHE.clear()
    module.kit_source = kit_source
    print(f'[kitfree] 已替换 {label or module.__name__}.kit_source')
