"""老旧街道（L_OldStreet）的 Blender 侧：从 老旧街道.blend 拆出这张图要的网格（不存盘，不改 .blend）。先跑 py -3 -I tools/oldstreet_layout.py。
blender -b Inbox/_oldstreet/老旧街道.blend -P tools/blender_oldstreet_export.py -- Inbox/_oldstreet/export [只导哪几个，逗号分隔]

坐标：导出时 v = (S·y, −S·x, S·z)（Blender 坐标，米），UE 按 (x, −y, z) 读，就是布局表的 (X, Y, Z)；旋转 −90°，不是镜像，文字方向不变。
- SM_OS_Ground：地面重切。路面、人行道都按原包贴图重新铺成整块矩形，沿街的 UV 正好整除周期（接缝对得上的关键）。原包那堆互相盖着的地面片、大地板（地面.001）、遮光棚（建筑.002）都不要。
- SM_OS_Curb：路缘石（原包 立方体.115 单元排一圈 + 立方体.001 / .112 两个弯角），沿街整除周期。
- SM_OS_Bollards：蓝白 U 形护桩（原包 3 排数组的单元，重新按整除周期的间距排）。
- SM_OS_Props：红绿灯、行人灯、标志牌、电杆、变压器、电线、配电箱、修车摊、桌椅、围栏。超出窗口的按周期挪回来。
- SM_OS_Bld_A/B/D/E/F：五种楼，原点在底面中心，UE 里按布局表实例化。
- SM_OS_Tree：一棵七叶树（去掉数组），原点在树干根部。原包的叶子贴图是丢的（生成的 UV 格子，所以作者的 EEVEE 里是黑碎片），UV 改到 K059 包的树叶图集上（T_OS_Leaf_D / _N，tools/oldstreet_textures.py 裁的）。
"""
import json
import math
import os
import random
import sys

import bpy
import numpy as np
from mathutils import Vector

ARGS = sys.argv[sys.argv.index('--') + 1:]
OUT = os.path.abspath(ARGS[0])
ONLY = set(ARGS[1].split(',')) if len(ARGS) > 1 else None
os.makedirs(OUT, exist_ok=True)
L = json.load(open('D:/RobotSlice/Inbox/_oldstreet/layout.json', encoding='utf-8'))
S, P0, ZR, XC, YC, W0, W1 = L['S'], L['P0'], L['ZR'], L['XC'], L['YC'], L['W0'], L['W1']
SLOT_OF = L['slot_of']
sc = bpy.context.scene
O = bpy.data.objects


def log(*a):
    print('[oldstreet]', *a)


def want(name):
    return ONLY is None or name in ONLY


class Builder:
    """攒顶点 / 三角形 / UV / 材质槽，最后建一个网格。输入是原包世界坐标（或任意坐标 + transform 函数）"""

    def __init__(self, name):
        self.name = name
        self.verts = []
        self.faces = []
        self.uvs = []
        self.mats = []
        self.face_mat = []
        self.n = 0

    def slot(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def add_mesh(self, me, matrix, xform, fallback='OS_Rail', keep=None, wrap_poly=False):
        """把一个已求值的 mesh 加进来；xform(np 世界坐标 n×3) → np 输出坐标 n×3。keep(中心) 决定多边形留不留"""
        uv_layer = me.uv_layers.active
        co = np.empty(len(me.vertices) * 3)
        me.vertices.foreach_get('co', co)
        co = co.reshape(-1, 3)
        m = np.array(matrix)
        world = (m[:3, :3] @ co.T).T + m[:3, 3]
        flip = np.linalg.det(m[:3, :3]) < 0
        mats = [mm.name if mm else None for mm in me.materials]
        for p in me.polygons:
            idx = list(p.vertices)
            pts = world[idx]
            c = pts.mean(0)
            if keep is not None and not keep(c):
                continue
            pts2 = pts.copy()
            if wrap_poly:
                pts2[:, 1] += ((c[1] - W0) % P0 + W0) - c[1]
            out = xform(pts2)
            loops = list(p.loop_indices)
            uvs = [tuple(uv_layer.data[l].uv) if uv_layer else (0.0, 0.0) for l in loops]
            if flip:
                out = out[::-1]
                uvs = uvs[::-1]
            base = self.n
            self.verts.extend(out.tolist())
            self.uvs.extend(uvs)
            self.faces.append(list(range(base, base + len(idx))))
            mn = mats[p.material_index] if p.material_index < len(mats) else None
            self.face_mat.append(self.slot(SLOT_OF.get(mn, fallback) if mn else fallback))
            self.n += len(idx)

    def add_quad(self, a, b, c, d, uv, mat):
        base = self.n
        self.verts.extend([a, b, c, d])
        self.uvs.extend(uv)
        self.faces.append([base, base + 1, base + 2, base + 3])
        self.face_mat.append(self.slot(mat))
        self.n += 4

    def build(self):
        me = bpy.data.meshes.new(self.name)
        me.from_pydata(self.verts, [], self.faces)
        me.update()
        uv = me.uv_layers.new(name='UVMap')
        flat = np.array(self.uvs, dtype=np.float32).ravel()
        uv.data.foreach_set('uv', flat)
        for s in self.mats:
            m = bpy.data.materials.get(s) or bpy.data.materials.new(s)
            me.materials.append(m)
        me.polygons.foreach_set('material_index', self.face_mat)
        me.polygons.foreach_set('use_smooth', [False] * len(me.polygons))
        if getattr(self, 'colors', None):
            ca = me.color_attributes.new('Col', 'BYTE_COLOR', 'POINT')
            ca.data.foreach_set('color', np.array(self.colors, dtype=np.float32).ravel())
        me.update()
        ob = bpy.data.objects.new(self.name, me)
        sc.collection.objects.link(ob)
        tris = sum(len(p.vertices) - 2 for p in me.polygons)
        co = np.array(self.verts)
        log(self.name, '三角形', tris, '槽', self.mats, '范围', co.min(0).round(2).tolist(), '~', co.max(0).round(2).tolist())
        return ob


def export(ob):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=f'{OUT}/{ob.name}.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                             mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
                             axis_forward='-Z', axis_up='Y', colors_type='LINEAR')
    log('写出', ob.name)


def world_xf(dx=0.0, dy=0.0):
    """原包世界坐标 → 导出坐标（地图坐标的 Blender 写法）"""
    def f(p):
        return np.stack([S * (p[:, 1] + dy - YC), -S * (p[:, 0] + dx - XC), S * (p[:, 2] - ZR)], 1)
    return f


def add_objects(b, names, dx=0.0, dy=0.0, keep=None, wrap_poly=False, wrap_obj=True):
    """把几个物体（含几何节点、集合实例）整个加进 Builder。object_instances 里的对象只在迭代当时有效，要当场取网格。
    wrap_obj：按物体中心 y 挪回窗口"""
    deps = bpy.context.evaluated_depsgraph_get()
    want_names = set(names)
    for inst in deps.object_instances:
        src = inst.parent.original.name if inst.is_instance else inst.object.original.name
        if src not in want_names or inst.object.type != 'MESH':
            continue
        ev = inst.object
        mw = inst.matrix_world.copy()
        me = ev.to_mesh()
        if len(me.polygons):
            yc_ = float(np.mean([(mw @ Vector(c)).y for c in ev.bound_box])) + dy
            ddy = dy + (((yc_ - W0) % P0 + W0) - yc_ if wrap_obj else 0.0)
            b.add_mesh(me, mw, world_xf(dx, ddy), keep=keep, wrap_poly=wrap_poly)
        ev.to_mesh_clear()


def unit_mesh(name, disable=('ARRAY',)):
    """关掉数组后的单元（世界坐标 np 顶点、面、UV、材质）"""
    o = O[name]
    saved = {}
    for m in o.modifiers:
        if m.type in disable:
            saved[m.name] = (m.show_viewport, m.show_render)
            m.show_viewport = m.show_render = False
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    ev = o.evaluated_get(deps)
    me = bpy.data.meshes.new_from_object(ev, depsgraph=deps)
    mw = ev.matrix_world.copy()
    for k, (v, r) in saved.items():
        o.modifiers[k].show_viewport, o.modifiers[k].show_render = v, r
    bpy.context.view_layer.update()
    return me, mw


# ---------------------------------------------------------------- 地面
if want('SM_OS_Ground'):
    b = Builder('SM_OS_Ground')
    rx0, rx1 = L['ROAD_X']
    ay0, ay1 = L['ARM_Y']
    vt = P0 / L['road_v_tiles']
    pt = L['pave_tile']
    FAR = L['FAR_X']
    EPS = 0.01   # 两头多伸 1 厘米，相邻周期的地面压一点点（UV 连续，看不出），避免接缝漏缝

    def V(x, y, z):
        return (S * (y - YC), -S * (x - XC), S * (z - ZR))

    def road(x0, x1, y0, y1):
        # U 横跨路面 0..1（Road003 自带三条黄线），V 沿街，整除周期
        uv = [(0.0, (y0 - W0) / vt), (1.0, (y0 - W0) / vt), (1.0, (y1 - W0) / vt), (0.0, (y1 - W0) / vt)]
        b.add_quad(V(x0, y0, ZR), V(x1, y0, ZR), V(x1, y1, ZR), V(x0, y1, ZR), uv, 'OS_Road')

    def arm(x0, x1, y0, y1):
        # 东横路：照原包，贴图 U 只用中间 0.33–0.65（只有中间一条黄线），V 沿横路 4.28 米一张
        t = 4.28
        uv = [(0.33, x0 / t), (0.33, x1 / t), (0.65, x1 / t), (0.65, x0 / t)]
        b.add_quad(V(x0, y0, ZR - 0.002), V(x1, y0, ZR - 0.002), V(x1, y1, ZR - 0.002), V(x0, y1, ZR - 0.002), uv, 'OS_Road')

    def pave(x0, x1, y0, y1):
        z = L['SIDEWALK_Z']
        uv = [(x0 / pt, (y0 - W0) / pt), (x1 / pt, (y0 - W0) / pt), (x1 / pt, (y1 - W0) / pt), (x0 / pt, (y1 - W0) / pt)]
        b.add_quad(V(x0, y0, z), V(x1, y0, z), V(x1, y1, z), V(x0, y1, z), uv, 'OS_Paving')

    road(rx0, rx1, W0 - EPS, W1 + EPS)
    arm(rx1, FAR, ay0, ay1)
    pave(-FAR, rx0, W0 - EPS, W1 + EPS)
    pave(rx1, FAR, W0 - EPS, ay0)
    pave(rx1, FAR, ay1, W1 + EPS)
    if want('SM_OS_Ground'):
        export(b.build())

# ---------------------------------------------------------------- 路缘石
if want('SM_OS_Curb'):
    b = Builder('SM_OS_Curb')
    me, mw = unit_mesh('立方体.115')    # 单元沿 y，长 0.672 米
    co = np.array([list(mw @ v.co) for v in me.vertices])
    u0, u1 = co[:, 1].min(), co[:, 1].max()
    ux = (co[:, 0].min() + co[:, 0].max()) / 2
    n = int(round(P0 / (u1 - u0)))
    step = P0 / n
    scale_y = step / (u1 - u0)
    log('路缘单元长', round(u1 - u0, 3), '排', n, '个，间距', round(step, 4))
    from mathutils import Matrix

    def place(x, y0, along_x=False, stretch=1.0, z=0.0):
        """放一个单元：along_y（默认）沿街，along_x 沿横路"""
        T = Matrix.Translation((-ux, -u0, 0.0))
        Sc = Matrix.Diagonal((1.0, stretch, 1.0, 1.0))
        R = Matrix.Rotation(-math.pi / 2, 4, 'Z') if along_x else Matrix.Identity(4)
        P = Matrix.Translation((x, y0, z))
        return P @ R @ Sc @ T @ mw

    # 西边一条：贯穿整个周期（x = ROAD_X[0]）
    xw, xe = L['ROAD_X']
    for i in range(n):
        b.add_mesh(me, place(xw, W0 + i * step, stretch=scale_y), world_xf())
    # 东边：两个弯角（原包曲线修改器的结果，整个加进来）。立方体.001 的直线段在 x=2.647，往东挪到 ROAD_X[1]（原包这段压在路面上）
    add_objects(b, ['立方体.001'], dx=xe - 2.647, wrap_obj=False)
    add_objects(b, ['立方体.112'], wrap_obj=False)
    # 东边直线段：立方体.001 北端 y≈15.37 到 立方体.112 南端 y≈-26.61（下一周期 = 50.31）。分窗口内的两段
    y_a, y_b = 15.37, -26.61 + P0
    ny = int(round((y_b - y_a) / (u1 - u0)))
    st = (y_b - y_a) / ny
    for i in range(ny):
        yy = y_a + i * st
        yy2 = W0 + (yy - W0) % P0
        b.add_mesh(me, place(xe, yy2, stretch=st / (u1 - u0)), world_xf())
    # 东横路两侧往东延长：立方体.001 到 x≈36.7，立方体.112 到 x≈31.8，都接到 x=70
    for yy, x_start in ((-4.011, 36.717 + xe - 2.647), (-7.906, 31.768)):
        nx = int((70.0 - x_start) / (u1 - u0))
        for i in range(nx):
            b.add_mesh(me, place(x_start + i * (u1 - u0), yy, along_x=True), world_xf())
    export(b.build())

# ---------------------------------------------------------------- 护桩
if want('SM_OS_Bollards'):
    b = Builder('SM_OS_Bollards')
    from mathutils import Matrix
    tree_y = [wrap_y for x_, wrap_y in L['trees_src']]
    cnt = 0
    for src, n, y_first, dx in L['bollards']:
        me, mw = unit_mesh(src)
        co = np.array([list(mw @ v.co) for v in me.vertices])
        yc0 = (co[:, 1].min() + co[:, 1].max()) / 2
        xc0 = (co[:, 0].min() + co[:, 0].max()) / 2
        step = P0 / n
        for i in range(n):
            y = y_first + i * step
            y_w = W0 + (y - W0) % P0
            x = xc0 + dx
            if x > 0 and L['bollard_skip_arm'][0] < y_w < L['bollard_skip_arm'][1]:
                continue
            if x < 0 and any(abs(((y_w - t - W0) % P0 + W0)) < L['bollard_skip_tree'] or abs(y_w - (W0 + (t - W0) % P0)) < L['bollard_skip_tree'] for t in tree_y):
                continue
            T = Matrix.Translation((x - xc0, y_w - yc0, 0.0))
            b.add_mesh(me, T @ mw, world_xf())
            cnt += 1
    log('护桩', cnt, '个')
    export(b.build())

# ---------------------------------------------------------------- 道具
if want('SM_OS_Props'):
    b = Builder('SM_OS_Props')
    add_objects(b, ['crossing man light', '平面.002', '柱体.008', '柱体.009', '配电箱', '配电箱.001'])
    # 修车摊：原包在 (−4.7, 9.0)，西排往外挪了、又顶着树干，移到树和楼之间 y≈12.6（x −5.5..−2.2）
    add_objects(b, ['F8~DQP[T5{QW7D[@4~E$(55'], dx=0.86, dy=3.6)
    # 桌椅：五个集合实例
    add_objects(b, ['桌椅', '桌椅.001', '桌椅.002', '桌椅.003', '桌椅.004'])
    # 电杆、横担、变压器、路灯、电线
    pole_names = []
    for suf in ('', '.001', '.002'):
        pole_names += [n + suf for n in ('telegraph_pole', 'ArmHV.A', 'Trans', 'ArmLV.A', 'ArmLV.B', 'ArmCom.A', 'Lamp', 'Cap.A')]
    add_objects(b, pole_names)
    wires = []
    for suf in ('', '.001'):
        wires += [n + suf for n in ('ElectricalWireGW', 'ElectricalWireHV', 'ElectricalWireLV', 'ElectricalWireCom')]
    add_objects(b, wires)
    # 多拉两组电线（原包就一组，在路口）：同样的线沿街再摆两处，两头扎进楼里，电杆藏在楼后。近看街上有电线
    add_objects(b, wires, dy=26.0)
    add_objects(b, wires, dy=49.0)
    # 围栏：平面 = 西人行道那条（数组第二份在 x≈-3.4，第一份在 x≈-14.4 的楼后面不要），只留 y 18.5..32（让开修车摊和树）；平面.001 = 东横路北边
    add_objects(b, ['平面'], keep=lambda c: c[0] > -8.0 and 18.5 < c[1] < 32.0, wrap_obj=False)
    add_objects(b, ['平面.001'], wrap_obj=False)
    export(b.build())

# ---------------------------------------------------------------- 楼
for t, info in L['types'].items():
    name = f'SM_OS_Bld_{t}'
    if not want(name):
        continue
    src = info['src']
    o = O[src]
    deps = bpy.context.evaluated_depsgraph_get()
    ev = o.evaluated_get(deps)
    me = ev.to_mesh()
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    m = np.array(ev.matrix_world)
    w = (m[:3, :3] @ co.T).T + m[:3, 3]
    cx, cy, z0 = (w[:, 0].min() + w[:, 0].max()) / 2, (w[:, 1].min() + w[:, 1].max()) / 2, w[:, 2].min()
    log(name, '世界范围', w.min(0).round(3).tolist(), w.max(0).round(3).tolist(), '布局表尺寸', info['size'], info['h'])

    def xf(p, cx=cx, cy=cy, z0=z0):
        return np.stack([S * (p[:, 1] - cy), -S * (p[:, 0] - cx), S * (p[:, 2] - z0)], 1)

    b = Builder(name)
    # 立方体.094 / 立方体.002 重合一份，这里只取一份
    b.add_mesh(me, ev.matrix_world, xf)
    ev.to_mesh_clear()
    if t == 'B':
        # 原包这栋是一圈立面（平面 + 实体化），没有顶，高处往下看是空的。把 A 的屋顶面（同一张贴图）切下来缩放盖上
        a = O[L['types']['A']['src']]
        ea = a.evaluated_get(bpy.context.evaluated_depsgraph_get())
        ma = ea.to_mesh()
        wa = np.array([list(ea.matrix_world @ v.co) for v in ma.vertices])
        top = wa[:, 2].max()
        xa0, xa1, ya0, ya1 = wa[:, 0].min(), wa[:, 0].max(), wa[:, 1].min(), wa[:, 1].max()
        bx0, bx1, by0, by1, bz1 = w[:, 0].min(), w[:, 0].max(), w[:, 1].min(), w[:, 1].max(), w[:, 2].max()

        def xf_roof(p):
            q = p.copy()
            q[:, 0] = bx0 + (p[:, 0] - xa0) / (xa1 - xa0) * (bx1 - bx0)
            q[:, 1] = by0 + (p[:, 1] - ya0) / (ya1 - ya0) * (by1 - by0)
            q[:, 2] = bz1 - 0.02
            return xf(q)

        def is_top(c):
            return c[2] > top - 0.6
        b.add_mesh(ma, ea.matrix_world, xf_roof, keep=is_top)
        ea.to_mesh_clear()
    export(b.build())

# ---------------------------------------------------------------- 树
if want('SM_OS_Tree'):
    o = O['bq_Tree_Aesculus-hippocastanum_D_summer.001']
    me = o.data     # 原始网格（没带数组），本地坐标原点就在树干根部
    org = np.array(o.matrix_world.translation)
    log('树 本地面数', len(me.polygons), '原点', org.round(3).tolist(), '缩放', list(o.matrix_world.to_scale()))
    uv = me.uv_layers.active.data
    # 叶子一片 4 个面（一片约 0.45 米），UV 铺满 0..1。按连通分量分片，每片随机落到图集的一格。
    # KB3D 图集裁出来的左下象限：一格复叶（7 小叶）占大头，两格单叶当变化。七叶树的复叶本来就是一片叶子分好几小叶，形状接近
    CELLS = [(0.342, 0.032, 0.986, 0.936, 0.72), (0.136, 0.420, 0.302, 0.732, 0.14), (0.122, 0.048, 0.308, 0.368, 0.14)]
    n = len(me.vertices)
    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    leaf_idx = 1
    polys = [p for p in me.polygons if p.material_index == leaf_idx]
    for p in polys:
        vs = list(p.vertices)
        a = find(vs[0])
        for v in vs[1:]:
            parent[find(v)] = a
    rnd = random.Random(7)
    cell_of = {}
    weights = [c[4] for c in CELLS]
    out_uv = np.zeros((len(me.loops), 2), dtype=np.float32)
    me.uv_layers.active.data.foreach_get('uv', out_uv.ravel())
    # 抽稀：放大 1.3 倍的树冠太密，街上全黑（第一轮截图）。随机留 LEAF_KEEP 的叶片，留下的每片绕自己中心放大 LEAF_GROW 倍，树冠轮廓不变、透光
    LEAF_KEEP, LEAF_GROW = 0.55, 1.2
    keep_card = {}
    card_polys = {}
    for p in polys:
        k = find(p.vertices[0])
        card_polys.setdefault(k, []).append(p.index)
        c = cell_of.get(k)
        if c is None:
            c = cell_of[k] = rnd.choices(CELLS, weights)[0]
            keep_card[k] = rnd.random() < LEAF_KEEP
        for l in p.loop_indices:
            u, v = out_uv[l]
            out_uv[l] = (c[0] + (u + 0.02) * (c[2] - c[0]), c[1] + v * (c[3] - c[1]))
    log('叶子片数', len(cell_of), '留下', sum(keep_card.values()))
    me2 = me.copy()
    me2.uv_layers.active.data.foreach_set('uv', out_uv.ravel())
    co2 = np.empty(len(me2.vertices) * 3)
    me2.vertices.foreach_get('co', co2)
    co2 = co2.reshape(-1, 3)
    drop_faces = set()
    for k, idxs in card_polys.items():
        vs = sorted({v for i in idxs for v in me2.polygons[i].vertices})
        if keep_card[k]:
            ctr = co2[vs].mean(0)
            co2[vs] = ctr + (co2[vs] - ctr) * LEAF_GROW
        else:
            drop_faces.update(idxs)
    b = Builder('SM_OS_Tree')
    # 材质名原包是 bq_Leaf_...（.001 物体带 .001 后缀）

    def xf_tree(p):
        return np.stack([S * p[:, 1], -S * p[:, 0], S * p[:, 2]], 1)

    # 去掉 .001 后缀好查表
    for s in me2.materials:
        pass
    SLOT_OF['bq_Bark_Aesculus-hippocastanum.001'] = 'OS_Bark'
    SLOT_OF['bq_Leaf_Aesculus-hippocastanum.001'] = 'OS_Leaf'
    from mathutils import Matrix
    faces_keep = np.ones(len(me2.polygons), dtype=bool)
    faces_keep[list(drop_faces)] = False
    uv_layer = me2.uv_layers.active
    co_out = xf_tree(co2)
    mats_ = [m.name if m else None for m in me2.materials]
    # 树冠的明暗层次：树不进 Lumen，叶子之间没有互相遮挡的间接光，整团一个亮度。按叶片离树冠外皮的远近（树冠中心约 (0, 0, 13)，半径 13×13×9 原包米）压暗里层，
    # 再加一点随机：R = 亮度（0.45–1），G = 色相（0 偏绿，1 偏黄）。每棵树都是同一个网格，不会段与段不同
    leaf_vc = {}
    for k, idxs in card_polys.items():
        vs = sorted({v for i in idxs for v in me2.polygons[i].vertices})
        ctr = co2[vs].mean(0)
        rr = math.sqrt((ctr[0] / 13.0) ** 2 + (ctr[1] / 13.0) ** 2 + ((ctr[2] - 13.0) / 9.0) ** 2)
        t = max(0.0, min(1.0, (rr - 0.35) / 0.6))
        t = t * t * (3 - 2 * t)
        leaf_vc[k] = (min(1.0, 0.42 + 0.53 * t + rnd.uniform(-0.05, 0.08)), rnd.random())
    b.colors = []
    for p in me2.polygons:
        if not faces_keep[p.index]:
            continue
        idx = list(p.vertices)
        base = b.n
        b.verts.extend(co_out[idx].tolist())
        b.uvs.extend([tuple(uv_layer.data[l].uv) for l in p.loop_indices])
        b.faces.append(list(range(base, base + len(idx))))
        b.face_mat.append(b.slot(SLOT_OF[mats_[p.material_index]]))
        b.n += len(idx)
        if p.material_index == leaf_idx:
            cv = leaf_vc[find(p.vertices[0])]
            b.colors.extend([(cv[0], cv[1], 0.0, 1.0)] * len(idx))
        else:
            b.colors.extend([(1.0, 0.5, 0.0, 1.0)] * len(idx))
    # 树干的碰撞单独在 UE 里摆隐藏圆柱
    export(b.build())

log('完成')
