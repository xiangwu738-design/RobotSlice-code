"""从素材包（Kitbash 式：每个建筑 / 道具是一个集合）里挑出名字匹配的集合，每个集合合并成一个网格导出。
- groups.fbx：每组一个物体，修改器应用、变换烤进顶点，原点挪到底面中心
- groups.json：每组尺寸（米）、材质槽；每个材质用到的贴图（按 Diffuse / Spec / Gloss / Normal / Metal / Rough / Emissive 分）
- textures/：用到的贴图原文件
blender -b 素材.blend -P blender_export_groups.py -- 输出目录 集合名正则
"""
import json
import os
import re
import shutil
import sys

import bpy
from mathutils import Matrix, Vector

OUT, PATTERN = sys.argv[sys.argv.index('--') + 1:][:2]
os.makedirs(f'{OUT}/textures', exist_ok=True)
sc = bpy.context.scene
deps = bpy.context.evaluated_depsgraph_get()
ROLES = (('diffuse', 'Diffuse'), ('basecolor', 'Diffuse'), ('albedo', 'Diffuse'), ('spec', 'Spec'), ('gloss', 'Gloss'),
         ('normal', 'Normal'), ('metal', 'Metal'), ('rough', 'Rough'), ('emiss', 'Emissive'), ('opacity', 'Opacity'))


def clean(name):
    return re.sub(r'[^A-Za-z0-9_]', '_', name).strip('_') or 'x'


def images_in(tree, seen=None):
    seen = seen or set()
    out = []
    for n in tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            out.append(n.image)
        elif n.bl_idname == 'ShaderNodeGroup' and n.node_tree and n.node_tree.name not in seen:
            seen.add(n.node_tree.name)
            out += images_in(n.node_tree, seen)
    return out


def save_image(img):
    base = clean(os.path.splitext(os.path.basename(img.filepath or img.name))[0])
    ext = os.path.splitext(img.filepath or '')[1].lower() or '.png'
    dst = f'{OUT}/textures/{base}{ext}'
    if not os.path.exists(dst):
        if img.packed_file:
            open(dst, 'wb').write(img.packed_file.data)
        elif os.path.exists(bpy.path.abspath(img.filepath)):
            shutil.copyfile(bpy.path.abspath(img.filepath), dst)
        else:
            return None
    return base + ext


groups, used_mats = [], set()
made = []
for coll in bpy.data.collections:
    if not re.search(PATTERN, coll.name):
        continue
    parts = []
    for o in coll.all_objects:
        if o.type not in ('MESH', 'CURVE', 'FONT') or o.hide_render:
            continue
        me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), depsgraph=deps)
        if not me.polygons:
            bpy.data.meshes.remove(me)
            continue
        me.materials.clear()
        for s in o.material_slots:
            me.materials.append(s.material)
        me.transform(o.matrix_world)
        if o.matrix_world.determinant() < 0:
            me.flip_normals()
        po = bpy.data.objects.new(clean(coll.name) + '_part', me)
        sc.collection.objects.link(po)
        parts.append(po)
    if not parts:
        continue
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    if len(parts) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = 'SM_' + clean(coll.name)
    vs = [v.co for v in ob.data.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    ob.data.transform(Matrix.Translation(-Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))))
    mats = [m.name for m in ob.data.materials if m]
    used_mats.update(mats)
    groups.append({'name': ob.name, 'collection': coll.name, 'size': list(hi - lo), 'materials': mats,
                   'tris': sum(len(p.vertices) - 2 for p in ob.data.polygons)})
    made.append(ob)
    print(f'[groups] {ob.name} {hi.x - lo.x:.1f}x{hi.y - lo.y:.1f}x{hi.z - lo.z:.1f} 米 {groups[-1]["tris"]} 面')

materials = {}
for name in sorted(used_mats):
    m = bpy.data.materials[name]
    tex = {}
    if m.use_nodes:
        for img in images_in(m.node_tree):
            low = (img.name + os.path.basename(img.filepath)).lower()
            role = next((r for k, r in ROLES if k in low), None)
            f = save_image(img)
            if role and f and role not in tex:
                tex[role] = f
    materials[name] = {'textures': tex, 'base': list(m.diffuse_color[:3])}

bpy.ops.object.select_all(action='DESELECT')
for ob in made:
    ob.select_set(True)
bpy.ops.export_scene.fbx(filepath=f'{OUT}/groups.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                         mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False)
json.dump({'groups': groups, 'materials': materials}, open(f'{OUT}/groups.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'[groups] 导出 {len(groups)} 组、材质 {len(materials)} 个、贴图 {len(os.listdir(OUT + "/textures"))} 张 → {OUT}')
