"""K62 苏联建筑（Kitbash3D Soviet Blocks）导出给 UE（苏联板楼图 L_Soviet）。每栋楼拆成两个网格、各自一个 FBX：
  SM_SV_<组>_Solid：结构（混凝土 / 砖 / 玻璃 / 屋面 / 桥 / 水塔 ...），有碰撞；_Deco：管线、空调、晾衣、栏杆、楼梯、天线 ...，无碰撞（机体高速贴墙飞不会被细件卡住）。
原点 = Solid 包围盒的水平中心，地面高 0。另外写每栋的 0.5 米高度图（Solid 顶面高度 + 地面往上第一个遮挡高度），布局脚本用来算空隙和视线。
Export 1 的单件道具 → props.fbx（SM_SVP_<名>，原点底面中心）。
blender -b Inbox/K62_苏联建筑.blend -P tools/k62_export.py -- Inbox/_k62/export [组,组...]
"""
import json
import os
import re
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

args = sys.argv[sys.argv.index('--') + 1:]
OUT = os.path.abspath(args[0])
ONLY = args[1].split(',') if len(args) > 1 and args[1] else None
os.makedirs(f'{OUT}/raw', exist_ok=True)
os.makedirs(f'{OUT}/fbx', exist_ok=True)
os.makedirs(f'{OUT}/heights', exist_ok=True)
sc = bpy.data.scenes['KB3D_SovietBlocks-Native']
bpy.context.window.scene = sc
deps = bpy.context.evaluated_depsgraph_get()

# 整个物体都算结构
WHOLE_SOLID = {'Bridge', 'WaterTower', 'Tank', 'Helipad', 'Tower', 'Chimney', 'Pillars', 'BasketballField'}
# 按面的材质分：结构材质进 Solid，其余（窗框、栏杆、空调、布、灯）进 Deco
SPLIT = {'Building', 'Facade', 'Base', 'Wall', 'TopFloor', 'RooftopEntrance', 'RooftopWindow'}
SOLID_MAT = re.compile(r'KB3D_SVB_(Brick|Concrete|Glass|MetalCorrugated|SlateFinish|TarRoofing)')


def kind_of(name, group):
    k = re.sub(r'\.\d+$', '', name.replace('KB3D_SVB_' + group + '_', ''))
    return re.sub(r'[A-Z]$', '', k)


def clean(s):
    return re.sub(r'[^A-Za-z0-9_]', '_', s).strip('_') or 'x'


# ---------------------------------------------------------------- 贴图：内嵌的 PNG 原样写出
written = {}


def save_image(img):
    if img is None:
        return None
    if img.name in written:
        return written[img.name]
    name = clean(os.path.splitext(img.name)[0]) + '.png'
    dst = f'{OUT}/raw/{name}'
    if not os.path.exists(dst):
        if img.packed_file:
            open(dst, 'wb').write(img.packed_file.data)
        else:
            written[img.name] = None
            return None
    written[img.name] = name
    return name


def describe(m):
    d = {'color': [0.8, 0.8, 0.8], 'rough_v': 0.6, 'metal_v': 0.0, 'masked': False}
    if not m.use_nodes or not m.node_tree:
        return d
    for n in m.node_tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            role = os.path.splitext(n.image.name)[0].rsplit('_', 1)[-1].lower()
            key = {'basecolor': 'base', 'metallic': 'metal', 'roughness': 'rough', 'normal': 'normal', 'opacity': 'opacity', 'emissive': 'emissive'}.get(role)
            if key:
                d[key] = save_image(n.image)
    d['masked'] = bool(d.get('opacity'))
    return d


def object_mesh(o):
    me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), depsgraph=deps)
    # 注意：me.materials.clear() 会把每个面的 material_index 清零（整栋楼全变第一个材质），只能原地替换
    for i, s in enumerate(o.material_slots):
        if i < len(me.materials):
            me.materials[i] = s.material
        else:
            me.materials.append(s.material)
    me.transform(o.matrix_world)
    if o.matrix_world.determinant() < 0:
        me.flip_normals()
    if me.uv_layers.active:
        me.uv_layers.active.name = 'UVMap'
        for uv in list(me.uv_layers):
            if uv.name != 'UVMap':
                me.uv_layers.remove(uv)
    return me


def split_mesh(me):
    """按面材质分成 (结构, 细件)，某一边没有面就是 None"""
    solid_idx = {i for i, m in enumerate(me.materials) if m and SOLID_MAT.match(m.name)}
    mi = np.zeros(len(me.polygons), dtype=np.int32)
    me.polygons.foreach_get('material_index', mi)
    is_solid = np.isin(mi, list(solid_idx)) if solid_idx else np.zeros(len(mi), dtype=bool)
    out = []
    for want in (True, False):
        if want and not is_solid.any():
            out.append(None)
            continue
        if not want and is_solid.all():
            out.append(None)
            continue
        bm = bmesh.new()
        bm.from_mesh(me)
        bm.faces.ensure_lookup_table()
        kill = [f for f in bm.faces if bool(is_solid[f.index]) != want]
        bmesh.ops.delete(bm, geom=kill, context='FACES')
        n = bpy.data.meshes.new('split')
        for m in me.materials:
            n.materials.append(m)
        bm.to_mesh(n)
        bm.free()
        out.append(n)
    return out


def join_meshes(name, meshes):
    """把几个网格合成一个物体（链进场景）"""
    objs = []
    for me in meshes:
        po = bpy.data.objects.new(name + '_p', me)
        sc.collection.objects.link(po)
        objs.append(po)
    bpy.ops.object.select_all(action='DESELECT')
    for p in objs:
        p.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = name
    return ob


def bounds(ob):
    n = len(ob.data.vertices)
    co = np.zeros(n * 3, dtype=np.float32)
    ob.data.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    return co.min(0), co.max(0)


def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def export_fbx(path, objs):
    bpy.ops.object.select_all(action='DESELECT')
    for ob in objs:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                             mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
                             axis_forward='-Z', axis_up='Y')


def remove(ob):
    me = ob.data
    bpy.data.objects.remove(ob)
    bpy.data.meshes.remove(me)


def height_maps(ob, name):
    """Solid 的 0.5 米高度图：top = 从天上往下第一个面（屋顶高度，没有 = 0）；clear = 地面 0.3 米往上第一个面（没有 = 999），< 3.5 就是机体走不过去"""
    co = np.zeros(len(ob.data.vertices) * 3, dtype=np.float64)
    ob.data.vertices.foreach_get('co', co)
    verts = co.reshape(-1, 3).tolist()
    polys = [tuple(p.vertices) for p in ob.data.polygons]
    tree = BVHTree.FromPolygons(verts, polys, all_triangles=False)
    lo, hi = bounds(ob)
    cell = 0.5
    nx, ny = int((hi[0] - lo[0]) / cell) + 2, int((hi[1] - lo[1]) / cell) + 2
    top = np.zeros((ny, nx), dtype=np.float32)
    clear = np.full((ny, nx), 999.0, dtype=np.float32)
    dn, up = Vector((0, 0, -1)), Vector((0, 0, 1))
    for j in range(ny):
        y = float(lo[1]) + (j + 0.5) * cell
        for i in range(nx):
            x = float(lo[0]) + (i + 0.5) * cell
            r = tree.ray_cast(Vector((x, y, 400.0)), dn, 500.0)
            if r[0] is not None:
                top[j, i] = r[0].z
                r2 = tree.ray_cast(Vector((x, y, 0.3)), up, 500.0)
                if r2[0] is not None:
                    clear[j, i] = r2[0].z
    np.savez_compressed(f'{OUT}/heights/{name}.npz', top=top, clear=clear, x0=float(lo[0]), y0=float(lo[1]), cell=cell)


# ---------------------------------------------------------------- 楼
groups = {}
for o in sc.objects:
    if o.type != 'MESH':
        continue
    m = re.match(r'KB3D_SVB_(Bldg(?:LG|MD|SM)_[A-Z])_', o.name)
    if m:
        groups.setdefault(m.group(1), []).append(o)
meta = {}
used = set()
for g in sorted(groups):
    if ONLY and g not in ONLY:
        continue
    solid, deco, areas = [], [], {}
    for o in groups[g]:
        k = kind_of(o.name, g)
        me = object_mesh(o)
        if not me.polygons:
            bpy.data.meshes.remove(me)
            continue
        if k in WHOLE_SOLID:
            solid.append(me)
        elif k in SPLIT:
            for p in me.polygons:
                mn = me.materials[p.material_index].name if me.materials and p.material_index < len(me.materials) and me.materials[p.material_index] else 'None'
                areas[mn] = areas.get(mn, 0.0) + p.area
            a, b = split_mesh(me)
            bpy.data.meshes.remove(me)
            if a:
                solid.append(a)
            if b:
                deco.append(b)
        else:
            deco.append(me)
    base = g.replace('Bldg', '')
    so = join_meshes(f'SM_SV_{base}_Solid', solid)
    lo, hi = bounds(so)
    shift = Matrix.Translation((-(float(lo[0]) + float(hi[0])) / 2, -(float(lo[1]) + float(hi[1])) / 2, 0))
    so.data.transform(shift)
    objs = [so]
    entry = {'name': so.name, 'tris_solid': tri_count(so)}
    lo, hi = bounds(so)
    entry['min'], entry['max'] = [float(x) for x in lo], [float(x) for x in hi]
    used.update(m.name for m in so.data.materials if m)
    do = None
    if deco:
        do = join_meshes(f'SM_SV_{base}_Deco', deco)
        do.data.transform(shift)
        objs.append(do)
        entry['tris_deco'] = tri_count(do)
        l2, h2 = bounds(do)
        entry['deco_min'], entry['deco_max'] = [float(x) for x in l2], [float(x) for x in h2]
        used.update(m.name for m in do.data.materials if m)
    entry['slots_solid'] = [m.name for m in so.data.materials if m]
    entry['slots_deco'] = [m.name for m in do.data.materials if m] if do else []
    entry['area_by_mat'] = {k: round(v) for k, v in sorted(areas.items(), key=lambda kv: -kv[1])}
    export_fbx(f'{OUT}/fbx/SV_{base}.fbx', objs)
    height_maps(so, base)
    # 楼层高度：玻璃面的 Z 分布的自相关（2.5–4.5 米里峰值）
    zs = []
    for p in so.data.polygons:
        m = so.data.materials[p.material_index]
        if m and m.name in ('KB3D_SVB_Glass', 'KB3D_SVB_GlassBlack') and abs(p.normal.z) < 0.2:
            zs.append(p.center.z)
    if len(zs) > 20:
        h, _ = np.histogram(zs, bins=np.arange(0, float(hi[2]) + 0.1, 0.1))
        h = h.astype(np.float64) - h.mean()
        ac = [(h[:-lag] * h[lag:]).sum() for lag in range(25, 46)]
        entry['floor_h'] = 2.5 + 0.1 * int(np.argmax(ac))
    meta[base] = entry
    print(f'[k62] {g} {hi[0]-lo[0]:.1f}x{hi[1]-lo[1]:.1f}x{hi[2]-lo[2]:.1f} solid {entry["tris_solid"]} deco {entry.get("tris_deco", 0)} 层高 {entry.get("floor_h")}', flush=True)
    for ob in objs:
        remove(ob)
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)
    json.dump({'buildings': meta}, open(f'{OUT}/buildings.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)

# ---------------------------------------------------------------- 单件道具
props = {}
if not ONLY:
    objs = []
    for o in bpy.data.collections['Export 1'].all_objects:
        if o.type != 'MESH':
            continue
        name = 'SM_SVP_' + clean(o.name.replace('KB3D_SVB_', '').replace('_Main', ''))
        me = object_mesh(o)
        ob = join_meshes(name, [me])
        lo, hi = bounds(ob)
        ob.data.transform(Matrix.Translation((-(float(lo[0]) + float(hi[0])) / 2, -(float(lo[1]) + float(hi[1])) / 2, -float(lo[2]))))
        lo, hi = bounds(ob)
        props[name] = {'min': [float(x) for x in lo], 'max': [float(x) for x in hi], 'tris': tri_count(ob), 'slots': [m.name for m in ob.data.materials if m]}
        used.update(props[name]['slots'])
        objs.append(ob)
    export_fbx(f'{OUT}/fbx/SV_props.fbx', objs)
    for ob in objs:
        remove(ob)

materials = {m: describe(bpy.data.materials[m]) for m in sorted(used) if m in bpy.data.materials}
# k059_pack_textures.py 读 meshes.json 的 materials
old = {}
if os.path.exists(f'{OUT}/meshes.json'):
    old = json.load(open(f'{OUT}/meshes.json', encoding='utf-8')).get('materials', {})
old.update(materials)
json.dump({'materials': old, 'props': props}, open(f'{OUT}/meshes.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[k62] 完成', len(meta), '栋，道具', len(props), '件，材质', len(old), flush=True)
