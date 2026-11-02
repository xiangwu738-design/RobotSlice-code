"""相位电子园区远景：从 K005 摩天楼包挑几栋现代高楼，每栋合并成一个网格，材质归成三类（白面板 / 深青玻璃 / 深色金属），
UE 里统一换成相位电子的白 + 冷青玻璃（原包是纽约砖楼配色，和 K051 的白色风格不搭）。不带贴图。
blender -b K005.blend -P tools/k051_backdrop_export.py -- 输出目录
输出 backdrop.fbx（SM_TPB_C<编号>，原点在底面中心）+ backdrop.json（尺寸、面数）
"""
import json
import os
import re
import sys

import bpy
from mathutils import Matrix, Vector

OUT = sys.argv[sys.argv.index('--') + 1]
os.makedirs(OUT, exist_ok=True)
PICK = ['10', '14', '17', '18', '26', '27', '28', '30', '31', '32', '33', '35', '36']
GLASS = re.compile(r'GLS|Window|Interior', re.I)
DARK = re.compile(r'Blk|Black|RoofTar|MullionDK|SmoothBlack|Chrome', re.I)
sc = bpy.context.scene
deps = bpy.context.evaluated_depsgraph_get()
cls_mats = {k: bpy.data.materials.new(f'TPB_{k}') for k in ('Panel', 'Glass', 'Dark')}


def klass(m):
    if m is None:
        return 'Panel'
    if GLASS.search(m.name):
        return 'Glass'
    if DARK.search(m.name):
        return 'Dark'
    return 'Panel'


made, info = [], []
for num in PICK:
    coll = bpy.data.collections.get(f'Collection {num}')
    if not coll:
        continue
    parts = []
    for o in coll.all_objects:
        if o.type not in ('MESH', 'CURVE') or o.hide_render:
            continue
        me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), depsgraph=deps)
        if not me.polygons:
            bpy.data.meshes.remove(me)
            continue
        # 每个面按原材质归类，换成三个统一材质
        remap = [klass(s.material) for s in o.material_slots] or ['Panel']
        order = ['Panel', 'Glass', 'Dark']
        for p in me.polygons:
            p.material_index = order.index(remap[p.material_index] if p.material_index < len(remap) else 'Panel')
        me.materials.clear()
        for k in order:
            me.materials.append(cls_mats[k])
        me.transform(o.matrix_world)
        if o.matrix_world.determinant() < 0:
            me.flip_normals()
        po = bpy.data.objects.new('tmp', me)
        sc.collection.objects.link(po)
        parts.append(po)
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    if len(parts) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = f'SM_TPB_C{num}'
    vs = [v.co for v in ob.data.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    ob.data.transform(Matrix.Translation(-Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))))
    made.append(ob)
    info.append({'name': ob.name, 'size': [round(v, 2) for v in hi - lo], 'tris': sum(len(p.vertices) - 2 for p in ob.data.polygons)})
    print('[backdrop]', info[-1])
bpy.ops.object.select_all(action='DESELECT')
for ob in made:
    ob.select_set(True)
bpy.ops.export_scene.fbx(filepath=f'{OUT}/backdrop.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                         mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False)
json.dump(info, open(f'{OUT}/backdrop.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[backdrop] 导出', len(made), '栋 →', OUT)
