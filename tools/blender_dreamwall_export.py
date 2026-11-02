"""梦核旷野图（L_Dreamfield）的 Blender 侧：从科幻峡谷包拆出「一面墙」的零件，加上程序生成的地面，导出 FBX 给 UE。
用户 10-31：「我的想法是只取峡谷一边，在下面做一个二级平台，岩壁放在一个旷野地图上」「找个梦核风格的旷野，和这个巨构结合吧」。

峡谷包里实际的结构（tools/blender_dreamwall_survey.py 统计）：
- Cube.009：顶上的甲板（一块 26×17.6×1.5 米的面板单元，阵列 13×7 + 沿 X 镜像，镜像前 245 万面）。两边甲板之间的沟只有 12 米宽。
- Cube.056：沟两侧挂下来的楼柱群（53×415×176 米，34.7 万面，两侧各一半，中间几乎贴着）。
- Plane：远处的 U 形大平面（14.7 公里长、600 米深），只是远景，不要。
所以「一面墙」= Cube.056 的 +X 那一半（墙面朝 -X）+ Cube.009 不镜像的那一侧甲板。

坐标：导出时换到「墙的局部坐标」——原点在墙面碰撞平面上（x）、Cube.056 长度中点（y）、甲板顶（z），墙面朝 -X，楼柱往下挂（z<0）。
UE 里按 (x, -y, z) × 100 读，墙面仍朝 -X。

blender -b Scene_02.blend -P tools/blender_dreamwall_export.py -- wall 输出目录
blender -b Scene_03.blend -P tools/blender_dreamwall_export.py -- clouds 输出目录
blender -b --factory-startup -P tools/blender_dreamwall_export.py -- ground 输出目录
"""
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

args = sys.argv[sys.argv.index('--') + 1:]
MODE, OUT = args[0], os.path.abspath(args[1])
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
META_PATH = f'{OUT}/dreamwall_{MODE}.json'


def log(*a):
    print('[dreamwall]', *a)


def mat(name):
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)


def eval_world_mesh(o, name, disable=()):
    """物体（含修改器，disable 里的修改器先关掉）→ 世界坐标的新网格"""
    saved = {}
    for m in o.modifiers:
        if m.name in disable or m.type in disable:
            saved[m.name] = (m.show_viewport, m.show_render)
            m.show_viewport = m.show_render = False
    deps = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), depsgraph=deps)
    for k, (v, r) in saved.items():
        o.modifiers[k].show_viewport, o.modifiers[k].show_render = v, r
    me.transform(o.matrix_world)
    if o.matrix_world.determinant() < 0:
        me.flip_normals()
    me.name = name
    return me


def new_obj(name, me, material):
    me.materials.clear()
    me.materials.append(mat(material))
    for p in me.polygons:
        p.material_index = 0
    ob = bpy.data.objects.new(name, me)
    sc.collection.objects.link(ob)
    return ob


def bounds(me):
    co = np.empty(len(me.vertices) * 3, dtype=np.float64)
    me.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    return co.min(0), co.max(0)


def tris(me):
    return sum(len(p.vertices) - 2 for p in me.polygons)


def export(objs, fbx):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(filepath=f'{OUT}/{fbx}', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                             mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
                             axis_forward='-Z', axis_up='Y')
    log('写出', f'{OUT}/{fbx}', [o.name for o in objs][:12])


meta = {'mode': MODE, 'meshes': {}}

# ================================================================ 墙
if MODE == 'wall':
    src = bpy.data.objects['Cube.056']
    me = eval_world_mesh(src, 'tmp_wall')
    # 按连通块分两半：中心 x > 0 的那一半（墙面朝 -X，对着沟）。连通块用 numpy 沿边传播最小编号（零件都小，几十轮就收敛）
    nv = len(me.vertices)
    ev = np.empty(len(me.edges) * 2, dtype=np.int64)
    me.edges.foreach_get('vertices', ev)
    ea, eb = ev[0::2], ev[1::2]
    lab = np.arange(nv)
    for _ in range(10000):
        m_ = np.minimum(lab[ea], lab[eb])
        new = lab.copy()
        np.minimum.at(new, ea, m_)
        np.minimum.at(new, eb, m_)
        new = new[new]            # 指针跳跃，加快收敛
        if np.array_equal(new, lab):
            break
        lab = new
    nf = len(me.polygons)
    fv0 = np.empty(nf, dtype=np.int64)
    loop_start = np.empty(nf, dtype=np.int64)
    me.polygons.foreach_get('loop_start', loop_start)
    lv = np.empty(len(me.loops), dtype=np.int64)
    me.loops.foreach_get('vertex_index', lv)
    fv0 = lv[loop_start]
    fpart = lab[fv0]
    fc = np.empty(nf * 3)
    me.polygons.foreach_get('center', fc)
    fc = fc.reshape(-1, 3)
    fa = np.empty(nf)
    me.polygons.foreach_get('area', fa)
    _, inv = np.unique(fpart, return_inverse=True)
    psum = np.bincount(inv, weights=fa * fc[:, 0])
    parea = np.bincount(inv, weights=fa)
    pcx = psum / np.maximum(parea, 1e-9)
    keep = pcx[inv] > 0
    n_parts, n_keep = len(parea), int((pcx > 0).sum())
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    drop = [bm.faces[i] for i in np.nonzero(~keep)[0]]
    bmesh.ops.delete(bm, geom=drop, context='FACES')
    bm.to_mesh(me)
    bm.free()
    lo, hi = bounds(me)
    log(f'Cube.056 连通块 {n_parts} 个，+X 一半 {n_keep} 个，{tris(me)} 面，范围 {lo.round(2)} ~ {hi.round(2)}')

    # 墙面位置：朝 -X 的面（法线 x < -0.7）按面积加权的 x 分位数；碰撞平面取 30% 分位（七成墙面在平面后面，机体贴墙时不会悬空太远）
    xs, ws, zs = [], [], []
    for p in me.polygons:
        if p.normal.x < -0.7:
            xs.append(p.center.x)
            ws.append(p.area)
            zs.append(p.center.z)
    xs, ws = np.array(xs), np.array(ws)
    order = np.argsort(xs)
    cum = np.cumsum(ws[order]) / ws.sum()
    pct = {q: float(xs[order][np.searchsorted(cum, q / 100)]) for q in (5, 10, 30, 50, 70, 90, 95)}
    log('朝 -X 的面 x 分位数（米）', {k: round(v, 2) for k, v in pct.items()})

    # 甲板：Cube.009 关掉两个阵列和镜像，只留「自动平滑」几何节点 → 一块面板单元
    deck_src = bpy.data.objects['Cube.009']
    arr = [m for m in deck_src.modifiers if m.type == 'ARRAY']
    deck = eval_world_mesh(deck_src, 'tmp_deck', disable=('ARRAY', 'MIRROR'))
    dlo, dhi = bounds(deck)
    # 阵列步长（米，世界坐标）：第一个阵列沿物体局部 X（世界 -Y），第二个沿局部 -Y（世界 +X）
    me_lo, me_hi = bounds(deck_src.data)      # 阵列的「相对偏移」按网格本身的包围盒算
    lsize = me_hi - me_lo
    rot = deck_src.matrix_world.to_3x3()
    step_a = rot @ Vector((arr[0].relative_offset_displace[0] * lsize[0], arr[0].relative_offset_displace[1] * lsize[1], 0))
    step_b = rot @ Vector((arr[1].relative_offset_displace[0] * lsize[0], arr[1].relative_offset_displace[1] * lsize[1], 0))
    log(f'甲板单元 {tris(deck)} 面，范围 {dlo.round(2)} ~ {dhi.round(2)}；阵列 {arr[0].count}×{arr[1].count}，步长 {step_a[:]} / {step_b[:]}')

    # 墙的局部坐标：x 原点 = 碰撞平面（30% 分位），y 原点 = Cube.056 长度中点，z 原点 = 甲板顶
    x_ref = pct[30]
    y_mid = float((lo[1] + hi[1]) / 2)
    z_top = float(dhi[2])
    T = Matrix.Translation(Vector((-x_ref, -y_mid, -z_top)))
    me.transform(T)
    deck.transform(T)
    wall_ob = new_obj('SM_DW_WallDetail', me, 'DW_Panel')
    deck_ob = new_obj('SM_DW_DeckTile', deck, 'DW_Panel')
    wlo, whi = bounds(me)
    klo, khi = bounds(deck)
    meta['frame'] = {'x_ref': x_ref, 'y_mid': y_mid, 'z_top': z_top, 'front_pct': pct}
    meta['meshes']['SM_DW_WallDetail'] = {'min': wlo.tolist(), 'max': whi.tolist(), 'tris': tris(me)}
    meta['meshes']['SM_DW_DeckTile'] = {'min': klo.tolist(), 'max': khi.tolist(), 'tris': tris(deck)}
    meta['deck'] = {'count_a': arr[0].count, 'count_b': arr[1].count, 'step_a': list(step_a), 'step_b': list(step_b)}
    # 墙面最深处（楼柱群背后的衬墙放这里）：+X 一半的最大 x
    meta['back_x'] = float(whi[0])
    objs = [wall_ob, deck_ob]

    # 墙面细节小件（2–15 米的方块件）：原点挪到底面中心，平台立面上当装饰
    bits = []
    for o in sorted(bpy.data.objects, key=lambda o: o.name):
        if o.type != 'MESH' or not o.name.startswith('Cube.0') or o.name in ('Cube.009', 'Cube.056'):
            continue
        if 'Main' not in [c.name for c in o.users_collection]:
            continue
        bm_ = eval_world_mesh(o, 'tmp_' + o.name)
        if len(bm_.polygons) < 6:
            continue
        blo, bhi = bounds(bm_)
        bm_.transform(Matrix.Translation(Vector((-(blo[0] + bhi[0]) / 2, -(blo[1] + bhi[1]) / 2, -blo[2]))))
        name = f'SM_DW_Bit{len(bits) + 1:02d}'
        ob = new_obj(name, bm_, 'DW_Panel')
        blo, bhi = bounds(bm_)
        meta['meshes'][name] = {'src': o.name, 'min': blo.tolist(), 'max': bhi.tolist(), 'tris': tris(bm_)}
        bits.append(ob)
        log(f'{name} ← {o.name}：{(bhi - blo).round(2)} 米，{tris(bm_)} 面')
    export([wall_ob], 'dw_wall.fbx')
    export([deck_ob], 'dw_deck.fbx')
    export(bits, 'dw_bits.fbx')

# ================================================================ 云（Scene_03 的元球云，已经是网格）
elif MODE == 'clouds':
    seen_sizes, clouds = [], []
    for o in sorted((o for o in bpy.data.objects if o.name.startswith('Mball') and o.type == 'MESH'), key=lambda o: o.name):
        me = eval_world_mesh(o, 'tmp_' + o.name)
        lo, hi = bounds(me)
        size = tuple(np.round(hi - lo, 0))
        if size in seen_sizes:      # 同一个形状复制出来的，只要一份
            bpy.data.meshes.remove(me)
            continue
        seen_sizes.append(size)
        c = (lo + hi) / 2
        me.transform(Matrix.Translation(Vector((-c[0], -c[1], -lo[2]))))
        name = f'SM_DF_Cloud{len(clouds) + 1:02d}'
        ob = new_obj(name, me, 'DF_Cloud')
        lo, hi = bounds(me)
        meta['meshes'][name] = {'src': o.name, 'min': lo.tolist(), 'max': hi.tolist(), 'tris': tris(me)}
        clouds.append(ob)
        log(f'{name} ← {o.name}：{(hi - lo).round(1)} 米，{tris(me)} 面')
    export(clouds, 'df_clouds.fbx')

# ================================================================ 地面（程序生成，近处一块细的带碰撞，远处一块粗的不带碰撞）
elif MODE == 'ground':
    # 坐标按 UE 的意图写（米，墙面在 X=+300、朝 -X），写 Blender 网格时 y 取反（UE 读 FBX 是 (x, -y, z)）
    WALL_X = 300.0

    # 高度函数和 UE 搭图、截图共用 tools/dreamfield_terrain.py（标量写法，这里 np.vectorize）
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import dreamfield_terrain as terrain
    height = np.vectorize(terrain.height, otypes=[np.float64])

    def grid(name, x0, x1, y0, y1, step, hole=None, drop=0.0):
        """hole = 近处那块的范围：完全在它里面（再缩一格）的面挖掉；在它里面的点往下压 drop 米，
        和近处网格重叠一圈、藏在近处底下（不会 z 打架，接缝处也不会漏出天空）"""
        nx, ny = int(round((x1 - x0) / step)) + 1, int(round((y1 - y0) / step)) + 1
        xs = np.linspace(x0, x1, nx)
        ys = np.linspace(y0, y1, ny)
        X, Y = np.meshgrid(xs, ys, indexing='ij')
        Z = height(X, Y)
        idx = np.arange(nx * ny).reshape(nx, ny)
        a, b, c, d = idx[:-1, :-1].ravel(), idx[1:, :-1].ravel(), idx[1:, 1:].ravel(), idx[:-1, 1:].ravel()
        faces = np.stack([a, d, c, b], 1)   # y 取反后保持朝上
        if hole:
            hx0, hx1, hy0, hy1 = hole
            eps = 1e-3
            inside = (X > hx0 + eps) & (X < hx1 - eps) & (Y > hy0 + eps) & (Y < hy1 - eps)
            Z = np.where(inside, Z - drop, Z)
            cx = (X[:-1, :-1] + X[1:, 1:]).ravel() / 2
            cy = (Y[:-1, :-1] + Y[1:, 1:]).ravel() / 2
            gone = (cx > hx0 + step) & (cx < hx1 - step) & (cy > hy0 + step) & (cy < hy1 - step)
            faces = faces[~gone]
        verts = np.stack([X.ravel(), -Y.ravel(), Z.ravel()], 1)
        me = bpy.data.meshes.new(name)
        me.from_pydata(verts.tolist(), [], faces.tolist())
        me.update()
        # 法线朝上检查
        up = sum(1 for p in me.polygons[:50] if p.normal.z > 0)
        if up < 25:
            me.flip_normals()
        ob = new_obj(name, me, 'DF_Grass')
        log(f'{name}：{nx}×{ny} 点，{tris(me)} 面，步长 {step} 米，高度 {Z.min():.2f}~{Z.max():.2f}')
        meta['meshes'][name] = {'min': [x0, y0, float(Z.min())], 'max': [x1, y1, float(Z.max())], 'tris': tris(me), 'step': step}
        return ob

    NEAR = (-475.0, 350.0, -625.0, 625.0)      # 场地（X -300~300、Y -450~450）外再留 175 米；边界落在远处网格的 25 米格线上
    near = grid('SM_DF_GroundNear', NEAR[0], NEAR[1], NEAR[2], NEAR[3], 5.0)
    # 远处：同一个高度函数、25 米一格，不带碰撞（机体出不去）
    far = grid('SM_DF_GroundFar', -6000.0, 400.0, -4500.0, 4500.0, 25.0, hole=NEAR, drop=0.3)
    meta['wall_x'] = WALL_X
    meta['near'] = NEAR
    export([near], 'df_ground_near.fbx')
    export([far], 'df_ground_far.fbx')

json.dump(meta, open(META_PATH, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
log('写出', META_PATH)
