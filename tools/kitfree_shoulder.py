"""11-02 去组件：肩部装备（lookdev/make_shoulder_game.py、make_shoulder_tactical.py）用到的组件库零件的自建替代件。
两个脚本取零件都经过 make_shoulder_kits.part(库, 名字, rot, size, axis)：烘进自身变换 → 按 rot 转 → 包围盒中心放原点 → 按 axis 方向缩放到 size，
返回 (物体, 缩放后的尺寸)。这里 part() 同样的签名、同样的约定（直接按「转好以后」的朝向建，中心在原点，再按 size 缩放），
按名字换成从零建的件。part_x / lib_x 会按「哪头细」自动掉头，所以炮管 / 管口这一头的粗细按原脚本的期望做（见各件注释）。
自建件本来就是低面：物体上记 kitfree=True，包装脚本让 use() 遇到它不再减面。"""
import math

import bmesh
import bpy
from mathutils import Matrix, Vector

import kitfree_parts as P

_MAT = None


def _mat():
    global _MAT
    if _MAT is None:
        _MAT = bpy.data.materials.get('kitfree_tmp') or bpy.data.materials.new('kitfree_tmp')
    return _MAT


def lathe_x(bm, prof, segs=24):
    """绕 X 轴的回转体：prof = [(x, 半径)]，两头半径 > 0 时封口"""
    rings = []
    for x, r in prof:
        rings.append([bm.verts.new((x, r * math.cos(2 * math.pi * k / segs), r * math.sin(2 * math.pi * k / segs))) for k in range(segs)])
    for a, b in zip(rings, rings[1:]):
        for k in range(segs):
            f = bm.faces.new((a[k], b[k], b[(k + 1) % segs], a[(k + 1) % segs]))
            f.smooth = True
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])


def ring_z(bm, r_out, r_in, z0, z1, segs=32):
    """平放的圆环（轴 Z）：外圆柱面、内圆柱面、上下环面"""
    def circ(r, z):
        return [bm.verts.new((r * math.cos(2 * math.pi * k / segs), r * math.sin(2 * math.pi * k / segs), z)) for k in range(segs)]
    oa, ob_, ia, ib = circ(r_out, z0), circ(r_out, z1), circ(r_in, z0), circ(r_in, z1)
    for k in range(segs):
        n = (k + 1) % segs
        bm.faces.new((oa[k], oa[n], ob_[n], ob_[k])).smooth = True
        bm.faces.new((ia[n], ia[k], ib[k], ib[n])).smooth = True
        bm.faces.new((ob_[k], ob_[n], ib[n], ib[k]))
        bm.faces.new((oa[n], oa[k], ia[k], ia[n]))


def _obj(name, bm):
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name + '（自建）')
    bm.to_mesh(me)
    bm.free()
    me.materials.append(_mat())
    ob = bpy.data.objects.new(name, me)
    ob['kitfree'] = True
    return ob


# ---------------------------------------------------------------- 钩爪发射器（发射筒沿 Y，筒口 +Y；调用方再转到 +X）
def grapple_tube():
    bm = bmesh.new()
    P.cyl(bm, (0, -0.30, 0), (0, 0.40, 0), 0.085, segs=20)                       # 发射筒
    P.cyl(bm, (0, 0.40, 0), (0, 0.475, 0), 0.105, 0.098, segs=20)               # 筒口加强环
    for y in (-0.05, 0.20):
        P.cyl(bm, (0, y - 0.015, 0), (0, y + 0.015, 0), 0.096, segs=20)         # 两道箍
    P.box(bm, (-0.11, -0.475, -0.10), (0.11, -0.20, 0.10), bevel=0.012)         # 后部机匣
    P.box(bm, (-0.022, -0.22, 0.08), (0.022, 0.30, 0.108), bevel=0.004)         # 顶上导轨
    for s in (-1, 1):
        P.box(bm, (s * 0.11 - 0.012, -0.44, -0.06), (s * 0.11 + 0.012, -0.24, 0.06), bevel=0.003)   # 机匣侧板
    return bm


def winch_drum():
    """绞盘：轴沿 X（高 0.3 = z）"""
    bm = bmesh.new()
    P.cyl(bm, (-0.08, 0, 0), (0.08, 0, 0), 0.10, segs=24)
    for x in (-0.09, 0.09):
        P.cyl(bm, (x - 0.012, 0, 0), (x + 0.012, 0, 0), 0.15, segs=24)          # 两侧挡盘
    for x in (-0.05, -0.017, 0.017, 0.05):
        P.cyl(bm, (x - 0.012, 0, 0), (x + 0.012, 0, 0), 0.113, segs=24)         # 缠着的缆绳
    P.cyl(bm, (-0.125, 0, 0), (0.125, 0, 0), 0.03, segs=12)                     # 轴
    return bm


def claw():
    """三指爪：爪根在 -Y、爪尖朝 +Y，三根爪指在 XZ 平面里 120° 分布、向外张再向里扣"""
    bm = bmesh.new()
    P.cyl(bm, (0, -0.20, 0), (0, -0.06, 0), 0.07, segs=16)                      # 爪座
    P.cyl(bm, (0, -0.24, 0), (0, -0.20, 0), 0.04, segs=12)                      # 缆绳接头
    for k in range(3):
        a = math.radians(90 + 120 * k)
        d = Vector((math.cos(a), 0, math.sin(a)))
        pts = [Vector((0, -0.08, 0)) + d * 0.06, Vector((0, 0.02, 0)) + d * 0.15, Vector((0, 0.12, 0)) + d * 0.17,
               Vector((0, 0.20, 0)) + d * 0.10]
        P.sweep(bm, P._catmull(pts, 10), 0.024, segs=6)
        root = Vector((0, -0.08, 0)) + d * 0.06
        P.cyl(bm, root - Vector((0, 0.02, 0)), root + Vector((0, 0.02, 0)), 0.034, segs=10)   # 爪指根部的关节
    return bm


def net_tube():
    """捕获网发射器：大口径筒，沿 Y，筒口 +Y（筒口内圈半径 0.13 处装四个配重）"""
    bm = bmesh.new()
    P.cyl(bm, (0, -0.30, 0), (0, 0.38, 0), 0.15, segs=24)
    P.cyl(bm, (0, 0.38, 0), (0, 0.45, 0), 0.15, 0.175, segs=24)                 # 喇叭口
    for y in (-0.15, 0.10, 0.30):
        P.cyl(bm, (0, y - 0.018, 0), (0, y + 0.018, 0), 0.162, segs=24)
    P.box(bm, (-0.13, -0.45, -0.13), (0.13, -0.28, 0.13), bevel=0.015)         # 后盖
    P.box(bm, (-0.03, -0.25, 0.14), (0.03, 0.30, 0.17), bevel=0.005)            # 顶脊
    return bm


# ---------------------------------------------------------------- part_x 用的（最长轴就是 X；细的一头在 +X）
def artillery():
    """火炮：后膛箱（粗）+ 驻退筒 + 长炮管 + 炮口制退器（细的一头 +X）"""
    bm = bmesh.new()
    P.box(bm, (-0.50, -0.09, -0.10), (-0.14, 0.09, 0.10), bevel=0.015)
    P.box(bm, (-0.46, -0.10, -0.04), (-0.20, 0.10, 0.04), bevel=0.008)        # 后膛两侧的耳
    P.cyl(bm, (-0.15, 0, 0), (0.06, 0, 0), 0.062, segs=20)
    P.cyl(bm, (-0.14, 0, -0.07), (0.02, 0, -0.07), 0.028, segs=12)              # 下面的驻退筒
    P.cyl(bm, (0.06, 0, 0), (0.42, 0, 0), 0.036, segs=16)
    P.cyl(bm, (0.20, 0, 0), (0.24, 0, 0), 0.044, segs=16)                       # 炮管上一道箍
    P.box(bm, (0.42, -0.048, -0.04), (0.50, 0.048, 0.04), bevel=0.006)          # 制退器
    return bm


def smoke_pod():
    """烟雾弹：三联管装在后部箱体上（后部比管口粗）"""
    bm = bmesh.new()
    P.box(bm, (-0.50, -0.17, -0.11), (-0.10, 0.17, 0.11), bevel=0.015)
    for y in (-0.10, 0.0, 0.10):
        P.cyl(bm, (-0.12, y, 0.0), (0.46, y, 0.0), 0.045, segs=16)
        P.cyl(bm, (0.46, y, 0.0), (0.50, y, 0.0), 0.051, segs=16)               # 管口环
    P.box(bm, (0.05, -0.15, -0.035), (0.10, 0.15, 0.035), bevel=0.005)          # 中段箍板
    return bm


def emp_pod():
    """EMP：双联管，管口张开成喇叭（原脚本 flip=True：要粗的一头在 +X）"""
    bm = bmesh.new()
    P.box(bm, (-0.50, -0.05, -0.05), (-0.30, 0.05, 0.05), bevel=0.008)
    for y in (-0.075, 0.075):
        P.cyl(bm, (-0.32, y, 0), (0.38, y, 0), 0.058, segs=18)
        P.cyl(bm, (0.38, y, 0), (0.50, y, 0), 0.058, 0.09, segs=18)
        for x in (-0.12, 0.15):
            P.cyl(bm, (x - 0.015, y, 0), (x + 0.015, y, 0), 0.066, segs=18)
    P.box(bm, (-0.20, -0.11, -0.03), (0.30, 0.11, 0.03), bevel=0.005)          # 两管之间的连板
    return bm


def shield_ring():
    """护盾发生器：竖着的圆环（薄的方向是 X）+ 十字支架 + 中间轮毂"""
    bm = bmesh.new()
    pts = [Vector((0, 0.45 * math.cos(2 * math.pi * k / 40), 0.45 * math.sin(2 * math.pi * k / 40))) for k in range(41)]
    P.sweep(bm, pts, 0.05, segs=8, cap=False)
    pts2 = [Vector((0.01, 0.30 * math.cos(2 * math.pi * k / 32), 0.30 * math.sin(2 * math.pi * k / 32))) for k in range(33)]
    P.sweep(bm, pts2, 0.022, segs=6, cap=False)
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        d = Vector((0, math.cos(a), math.sin(a)))
        P.cyl(bm, d * 0.07, d * 0.43, 0.018, segs=8)
    P.cyl(bm, (-0.05, 0, 0), (0.05, 0, 0), 0.08, segs=20)
    return bm


# ---------------------------------------------------------------- 肩部四件
def thruster():
    """推进器：最长轴 X，前头（+X）收细成锥、后头粗（lib_x thin_front=True）；喷口钟罩原脚本另外加在后头"""
    bm = bmesh.new()
    lathe_x(bm, [(-0.50, 0.095), (-0.42, 0.10), (-0.40, 0.092), (0.15, 0.092), (0.17, 0.098), (0.22, 0.098), (0.24, 0.088),
                 (0.42, 0.06), (0.50, 0.035)], segs=20)
    for s in (-1, 1):
        P.box(bm, (-0.30, s * 0.07 - 0.012, 0.06), (0.10, s * 0.07 + 0.012, 0.11), bevel=0.004)   # 顶上两道散热鳍
    return bm


def pack_plate():
    """推进包背板：X 0.28 × Y 0.2 × Z 0.07，带两道凸筋"""
    bm = bmesh.new()
    P.box(bm, (-0.14, -0.10, -0.035), (0.14, 0.10, 0.02), bevel=0.01)
    for x in (-0.07, 0.07):
        P.box(bm, (x - 0.018, -0.09, 0.02), (x + 0.018, 0.09, 0.035), bevel=0.004)
    return bm


def disc():
    """带结构的圆盘（平放，X 方向直径 1）：底盘 + 上面一圈台阶 + 六道辐筋 + 中间凸台"""
    bm = bmesh.new()
    P.cyl(bm, (0, 0, -0.045), (0, 0, 0.0), 0.50, segs=36)
    P.cyl(bm, (0, 0, 0.0), (0, 0, 0.025), 0.42, 0.40, segs=36)
    for k in range(6):
        a = k * math.pi / 3
        c, s = math.cos(a), math.sin(a)
        b2 = bmesh.new()
        P.box(b2, (0.12, -0.02, 0.025), (0.40, 0.02, 0.045))
        bmesh.ops.transform(b2, matrix=Matrix.Rotation(a, 4, 'Z'), verts=b2.verts)
        me = bpy.data.meshes.new('_t')
        b2.to_mesh(me)
        b2.free()
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)
    P.cyl(bm, (0, 0, 0.025), (0, 0, 0.05), 0.12, 0.10, segs=24)
    return bm


def flat_ring():
    """平放的结构环（X 方向直径 1）：外环 + 12 个凸块"""
    bm = bmesh.new()
    ring_z(bm, 0.50, 0.38, -0.03, 0.03, segs=48)
    for k in range(12):
        a = k * math.pi / 6
        b2 = bmesh.new()
        P.box(b2, (0.40, -0.035, 0.03), (0.48, 0.035, 0.05), bevel=0.004)
        bmesh.ops.transform(b2, matrix=Matrix.Rotation(a, 4, 'Z'), verts=b2.verts)
        me = bpy.data.meshes.new('_t')
        b2.to_mesh(me)
        b2.free()
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)
    return bm


def coil_core():
    """线圈柱：最长轴 X，粗的一头 +X（lib_x thin_front=False）"""
    bm = bmesh.new()
    lathe_x(bm, [(-0.50, 0.06), (-0.40, 0.06), (-0.38, 0.05), (0.30, 0.05), (0.32, 0.09), (0.50, 0.09)], segs=20)
    for i in range(7):
        x = -0.32 + i * 0.09
        P.cyl(bm, (x, 0, 0), (x + 0.05, 0, 0), 0.075, segs=20)                  # 线圈
    return bm


def layer_hull():
    """布雷器外壳：X 0.56 × Y 0.22 × Z 0.16，顶上斜面 + 侧面格栅"""
    bm = bmesh.new()
    P.box(bm, (-0.28, -0.11, -0.08), (0.28, 0.11, 0.03), bevel=0.012)
    P.box(bm, (-0.24, -0.08, 0.03), (0.20, 0.08, 0.08), bevel=0.012)
    P.box(bm, (0.20, -0.06, 0.03), (0.26, 0.06, 0.06), bevel=0.008)
    for x in (-0.18, -0.12, -0.06, 0.0, 0.06):
        for s in (-1, 1):
            P.box(bm, (x - 0.012, s * 0.11 - 0.006, -0.05), (x + 0.012, s * 0.11 + 0.006, 0.01))   # 侧面格栅条
    return bm


BUILD = {
    ('机甲', 'Desirefx.me_Mega_Kitbash_Detail_52'): grapple_tube,
    ('机械零件2', 'Detail_288'): winch_drum,
    ('机甲2', 'Limb_02'): claw,
    ('机甲', 'Desirefx.me_Mega_Kitbash_Detail_60'): net_tube,
    ('枪炮', 'b1'): artillery,
    ('军事武器部件零件', 'Desirefx.me_Body328'): smoke_pod,
    ('军事武器部件零件', 'Desirefx.me_Body326'): emp_pod,
    ('枪炮', 'engine_6'): shield_ring,
    ('枪炮', 'engine_4'): thruster,
    ('枪炮', 'b3'): pack_plate,
    ('枪炮', 'a5'): disc,
    ('枪炮', 'a1'): flat_ring,
    ('枪炮', 'engine_5'): coil_core,
    ('科幻机械', 'scifi_kitbash_piece_03'): layer_hull,
}


def part(lib, name, rot=(0, 0, 0), size=None, axis=1, color=(0.47, 0.49, 0.52)):
    """和 make_shoulder_kits.part 一样的签名和返回值；rot 不用（自建件直接按转好以后的朝向建）"""
    fn = BUILD.get((lib, name))
    if fn is None:
        raise RuntimeError(f'[kitfree] 肩部没有 {lib} / {name} 的自建替代件：先在 tools/kitfree_shoulder.py 加上')
    ob = _obj(name, fn())
    me = ob.data
    lo = Vector([min(v.co[i] for v in me.vertices) for i in range(3)])
    hi = Vector([max(v.co[i] for v in me.vertices) for i in range(3)])
    me.transform(Matrix.Translation(-(lo + hi) / 2))
    s = size / (hi - lo)[axis] if size else 1.0
    me.transform(Matrix.Scale(s, 4))
    ob.color = (*color, 1)
    print(f'[kitfree] 肩部自建 {lib} / {name}：{len(me.polygons)} 面，尺寸 {[round(v, 3) for v in (hi - lo) * s]}', flush=True)
    return ob, (hi - lo) * s
