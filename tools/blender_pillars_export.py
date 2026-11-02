"""石柱山水（L_Pillars）的 Blender 侧：把 Inbox/_f25/冲破穹顶.blend 拆成网格（不保存、不改原文件）。
用户 11-02：「石柱山水我是打算加入的……这就是一张偏空战的地图。」场景一侧平面上的飞船、小船、脚手架、人不要；藤蔓是 4 万片散片，贴近像碎屑，
不搬进游戏，而是把它们的位置烤进石柱的顶点色 R（= 这块岩石长了多少苔藓藤蔓），材质里按这个画苔藓，远看和原作位置一样。
- SM_PL_Rock_*：每根石柱一个网格（世界坐标，原作里同一个网格被不同缩放摆了很多次，烤成世界坐标后每根都有自己的苔藓分布）
- SM_PL_Terrain_*：四块地形（世界坐标）
- SM_PL_Bush_*：五丛灌木（世界坐标，带叶片材质）
- pillars.json：范围、水面高度、相机、各网格的三角形数、贴图文件名；ivy / 花的散布点（UE 里撒植物用）
- bake.npz：地形和石柱的三角形（tools/pillars_spots.py 分析落脚点用）
blender -b 冲破穹顶.blend -P tools/blender_pillars_export.py -- 输出目录
"""
import json
import os
import sys

import bpy
import numpy as np
from mathutils import Vector, kdtree

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1:][0])
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
WATER_Z = 1.19  # 原作两块水（Cube.001 顶 1.10、RealWater 顶 1.38）和平面 1.19 的中间，地形和石柱脚都按它看着对
IVY_NEAR, IVY_FAR = 0.10, 0.42  # 顶点离最近一片藤蔓这么近算满苔藓 / 这么远算没有（原作单位）


def log(*a):
    print('[pillars]', *a)


deps = bpy.context.evaluated_depsgraph_get()
ivy = []
flowers = []
for dup in deps.object_instances:
    if not dup.is_instance or not dup.instance_object:
        continue
    nm = dup.instance_object.name
    if nm.startswith('BagaPie_Ivy_'):
        ivy.append(tuple(dup.matrix_world.translation))
    elif dup.parent and dup.parent.name.startswith('scatter_obj'):
        flowers.append(tuple(dup.matrix_world.translation) + (nm[:4],))
log('藤蔓实例', len(ivy), '花实例', len(flowers))
kd = kdtree.KDTree(len(ivy))
for i, p in enumerate(ivy):
    kd.insert(p, i)
kd.balance()


def mat(name):
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)


SLOT = {'Rock_073.001': 'PL_RockA', 'Rock_073.002': 'PL_RockB', 'Rock_073': 'PL_RockC', 'Material.001': 'PL_Terrain', 'Bush_013': 'PL_BushA', 'Bush_013.001': 'PL_BushB'}


def tex_of(m):
    out = {}
    for n in m.node_tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            links = [l.to_socket.name for o in n.outputs for l in o.links]
            out[links[0] if links else n.name] = os.path.basename(n.image.filepath) or n.image.name
    return out


meta = {'water_z': WATER_Z, 'meshes': {}, 'tex': {}, 'ivy_count': len(ivy)}
bake = {}


def world_mesh(o, name, ivy_mask=False):
    me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), depsgraph=deps)
    me.transform(o.matrix_world)
    if o.matrix_world.determinant() < 0:
        me.flip_normals()
    for i, s in enumerate(me.materials):
        src = s.name if s else ''
        if src in SLOT:
            if src not in meta['tex']:
                meta['tex'][src] = {'slot': SLOT[src], 'tex': tex_of(s)}
            me.materials[i] = mat(SLOT[src])
    for a in list(me.color_attributes):
        me.color_attributes.remove(a)
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    if ivy_mask:
        attr = me.color_attributes.new('PL', 'BYTE_COLOR', 'POINT')
        col = np.ones((len(me.vertices), 4), dtype=np.float32)
        d = np.empty(len(me.vertices), dtype=np.float32)
        for i, v in enumerate(co):
            d[i] = kd.find(v)[2]
        t = np.clip((IVY_FAR - d) / (IVY_FAR - IVY_NEAR), 0, 1)
        col[:, 0] = t * t * (3 - 2 * t)
        # G：离水面的高度 0..1（0 = 水面，1 = 高出水面 20 单位），材质里画水线和脚下的潮湿
        col[:, 1] = np.clip((co[:, 2] - WATER_Z) / 20.0, 0, 1)
        col[:, 2] = 0
        attr.data.foreach_set('color', col.ravel())
        me.color_attributes.active_color = attr
    po = bpy.data.objects.new(name, me)
    sc.collection.objects.link(po)
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    meta['meshes'][name] = {'min': co.min(0).tolist(), 'max': co.max(0).tolist(), 'tris': tris, 'src': o.name}
    log(name, '←', o.name, tris, '三角形', co.min(0).round(1), '~', co.max(0).round(1))
    return po


def export(ob):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=f'{OUT}/{ob.name}.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                             mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
                             axis_forward='-Z', axis_up='Y', colors_type='LINEAR', prioritize_active_color=True)


def tri_arrays(ob):
    me = ob.data
    me.calc_loop_triangles()
    n = len(me.loop_triangles)
    v = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get('co', v)
    tri = np.empty(n * 3, dtype=np.int32)
    me.loop_triangles.foreach_get('vertices', tri)
    return v.reshape(-1, 3).astype(np.float32), tri.reshape(-1, 3), me


def taper_edges(me):
    """地形是几块方形网格，边上高出水面的地方是一刀切的直边（原作里靠水和树挡着）：网格边界往里 TAPER 单位内压到水面以下，四周自然入水"""
    TAPER = 3.0
    edge_faces = {}
    for p in me.polygons:
        for ek in p.edge_keys:
            edge_faces[ek] = edge_faces.get(ek, 0) + 1
    border = {v for ek, n in edge_faces.items() if n == 1 for v in ek}
    kdb = kdtree.KDTree(len(border))
    for i, v in enumerate(border):
        c = me.vertices[v].co
        kdb.insert((c.x, c.y, 0.0), i)
    kdb.balance()
    under = -0.62  # 水底平面的高度（四块地形最低的边），边上压到这里，和水底接平，隔着水看不到直边
    moved = 0
    for v in me.vertices:
        c = v.co
        if c.z <= under:
            continue
        d = kdb.find((c.x, c.y, 0.0))[2]
        if d < TAPER:
            t = d / TAPER
            t = t * t * (3 - 2 * t)
            c.z = under + (c.z - under) * t
            moved += 1
    me.update()
    log(me.name, '边界', len(border), '压低顶点', moved)


O = bpy.data.objects
rocks = sorted((o for o in O if o.type == 'MESH' and (o.name.startswith('Rock_') or o.name.startswith('Small_stone'))), key=lambda o: o.name)
terr = sorted((o for o in O if o.type == 'MESH' and o.name.startswith('Terrain')), key=lambda o: o.name)
bush = sorted((o for o in O if o.type == 'MESH' and o.name.startswith('Bush_')), key=lambda o: o.name)
rock_v, rock_t = [], []
base = 0
for o in rocks:
    ob = world_mesh(o, 'SM_PL_' + o.name.replace('.', '_'), ivy_mask=True)
    export(ob)
    v, t, _ = tri_arrays(ob)
    rock_v.append(v)
    rock_t.append(t + base)
    base += len(v)
terr_v, terr_t, terr_uv = [], [], []
base = 0
for i, o in enumerate(terr):
    ob = world_mesh(o, f'SM_PL_Terrain_{i}')
    taper_edges(ob.data)
    export(ob)
    v, t, me = tri_arrays(ob)
    # 按三角形存 uv（uv 在 loop 上）
    li = np.empty(len(me.loop_triangles) * 3, dtype=np.int32)
    me.loop_triangles.foreach_get('loops', li)
    uv = np.empty(len(me.loops) * 2, dtype=np.float32)
    me.uv_layers.active.data.foreach_get('uv', uv)
    uv = uv.reshape(-1, 2)
    terr_v.append(v[t.ravel()])
    terr_uv.append(uv[li])
for o in bush:
    ob = world_mesh(o, 'SM_PL_' + o.name.replace('.', '_'))
    export(ob)

np.savez_compressed(f'{OUT}/bake.npz', rock_v=np.concatenate(rock_v), rock_t=np.concatenate(rock_t),
                    terr_v=np.concatenate(terr_v), terr_uv=np.concatenate(terr_uv))
cam = sc.camera
fwd = cam.matrix_world.to_3x3() @ Vector((0, 0, -1))
meta['camera'] = {'loc': list(cam.location), 'fwd': list(fwd), 'lens': cam.data.lens, 'sensor': cam.data.sensor_width}
meta['ivy'] = [[round(x, 2) for x in p] for p in ivy[::20]]
meta['flowers'] = [[round(p[0], 2), round(p[1], 2), round(p[2], 2), p[3]] for p in flowers]
json.dump(meta, open(f'{OUT}/pillars.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
log('写出', OUT, '网格', len(meta['meshes']), '贴图', meta['tex'])
