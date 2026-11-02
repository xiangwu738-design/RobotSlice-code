"""把一个现成的 Blender 场景拆成 UE 能重建的三样东西：
- scene.fbx：所有网格物体（曲线、文字先转网格，修改器都应用），旋转缩放烤进顶点，统统挪到原点
- scene.json：每个物体的位置（米，Blender 坐标）、材质槽、包围盒；灯光（类型、位置、朝向、颜色、功率、角度）；
  每个材质用到的贴图、发光颜色强度、底色金属度粗糙度
- textures/：材质用到的贴图（内嵌的直接写出原始文件）
UE 里由 ue_import_scene.py 导入网格、建材质、按 json 摆放和布灯。
blender -b 场景.blend -P blender_export_scene.py -- 输出目录 [--skip 集合名,集合名]
"""
import json
import os
import re
import shutil
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
OUT = argv[0]
SKIP = set(argv[argv.index('--skip') + 1].split(',')) if '--skip' in argv else set()
os.makedirs(f'{OUT}/textures', exist_ok=True)
sc = bpy.context.scene


def clean(name):
    return re.sub(r'[^A-Za-z0-9_]', '_', name).strip('_') or 'x'


# ---------------------------------------------------------------- 贴图
written = {}


def save_image(img):
    if img.name in written:
        return written[img.name]
    base = os.path.basename(img.filepath or img.name) or img.name
    stem, ext = os.path.splitext(base)
    if not ext:
        ext = '.png'
    name = clean(stem) + ext.lower()
    dst = f'{OUT}/textures/{name}'
    if img.packed_file:
        with open(dst, 'wb') as f:
            f.write(img.packed_file.data)
    else:
        src = bpy.path.abspath(img.filepath)
        if os.path.exists(src):
            shutil.copyfile(src, dst)
        else:
            written[img.name] = None
            return None
    written[img.name] = name
    return name


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


materials = {}
for m in bpy.data.materials:
    info = {'kind': 'pbr', 'images': [], 'base': [0.8, 0.8, 0.8], 'metal': 0.0, 'rough': 0.5, 'emission': None}
    if m.use_nodes:
        for n in m.node_tree.nodes:
            if n.bl_idname == 'ShaderNodeBsdfPrincipled':
                info['base'] = list(n.inputs['Base Color'].default_value[:3])
                info['metal'] = n.inputs['Metallic'].default_value
                info['rough'] = n.inputs['Roughness'].default_value
                ec = n.inputs['Emission Color'].default_value
                es = n.inputs['Emission Strength'].default_value
                if max(ec[:3]) > 0.01 and es > 0 and not n.inputs['Emission Color'].is_linked:
                    info['emission'] = [*ec[:3], es]
            elif n.bl_idname == 'ShaderNodeEmission':
                info['kind'] = 'emissive'
                info['emission'] = [*n.inputs[0].default_value[:3], n.inputs[1].default_value]
            elif n.bl_idname in ('ShaderNodeVolumeScatter', 'ShaderNodeVolumePrincipled', 'ShaderNodeVolumeAbsorption'):
                info['kind'] = 'volume'
        info['images'] = [x for x in dict.fromkeys(save_image(i) for i in images_in(m.node_tree)) if x]
    else:
        info['base'] = list(m.diffuse_color[:3])
    materials[m.name] = info

# ---------------------------------------------------------------- 物体
deps = bpy.context.evaluated_depsgraph_get()


def skipped(o):
    return o.hide_render or any(c.name in SKIP for c in o.users_collection)


objects = []
new_objs = []
src = [o for o in sc.objects if o.type in ('MESH', 'CURVE', 'FONT', 'SURFACE', 'META') and not skipped(o)]
for i, o in enumerate(src):
    ev = o.evaluated_get(deps)
    me = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=False, depsgraph=deps)
    if len(me.polygons) == 0:
        bpy.data.meshes.remove(me)
        continue
    # 物体级材质槽也带上
    mats = [s.material for s in o.material_slots]
    me.materials.clear()
    for mt in mats:
        me.materials.append(mt)
    mw = o.matrix_world.copy()
    loc, rot, scl = mw.decompose()
    me.transform(Matrix.LocRotScale(None, rot, scl))
    if mw.determinant() < 0:
        me.flip_normals()
    name = f'SM_{i:03d}_{clean(o.name)}'
    me.name = name
    no = bpy.data.objects.new(name, me)
    sc.collection.objects.link(no)
    new_objs.append(no)
    vs = [loc + v.co for v in me.vertices]
    lo = [min(v[k] for v in vs) for k in range(3)]
    hi = [max(v[k] for v in vs) for k in range(3)]
    objects.append({'name': name, 'source': o.name, 'collection': o.users_collection[0].name if o.users_collection else '',
                    'loc': list(loc), 'materials': [mt.name if mt else None for mt in mats], 'min': lo, 'max': hi,
                    'tris': sum(len(p.vertices) - 2 for p in me.polygons)})

# ---------------------------------------------------------------- 灯光
lights = []
for o in sc.objects:
    if o.type != 'LIGHT' or skipped(o):
        continue
    L = o.data
    d = (o.matrix_world.to_3x3() @ Vector((0, 0, -1))).normalized()
    item = {'name': o.name, 'type': L.type, 'loc': list(o.matrix_world.translation), 'dir': list(d),
            'color': list(L.color), 'energy': L.energy}
    if L.type == 'SPOT':
        item['spot_size'] = L.spot_size
        item['spot_blend'] = L.spot_blend
    if L.type == 'AREA':
        item['size'] = [L.size, L.size_y if L.shape in ('RECTANGLE', 'ELLIPSE') else L.size]
        item['up'] = list((o.matrix_world.to_3x3() @ Vector((0, 1, 0))).normalized())
    if L.type in ('POINT', 'SPOT'):
        item['radius'] = getattr(L, 'shadow_soft_size', 0.1)
    lights.append(item)

# ---------------------------------------------------------------- 导出
bpy.ops.object.select_all(action='DESELECT')
for no in new_objs:
    no.select_set(True)
bpy.context.view_layer.objects.active = new_objs[0]
bpy.ops.export_scene.fbx(filepath=f'{OUT}/scene.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                         mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False)
used = sorted({m for ob in objects for m in ob['materials'] if m})
json.dump({'objects': objects, 'lights': lights, 'materials': {k: v for k, v in materials.items() if k in used}},
          open(f'{OUT}/scene.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'[export] 网格 {len(objects)} 个，三角面 {sum(o["tris"] for o in objects)}，灯 {len(lights)} 盏，材质 {len(used)} 个，'
      f'贴图 {sum(1 for v in written.values() if v)} 张 → {OUT}')
