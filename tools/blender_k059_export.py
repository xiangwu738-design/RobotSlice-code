"""城市图（K059 + K005）素材导出给 UE：每件一个网格，写 FBX + meshes.json + 原始贴图（之后 k059_pack_textures.py 整理成 UE 用的三张）。
blender -b "Inbox/K059 _城市街道.blend" -P tools/blender_k059_export.py -- k059 Inbox/_k059/export_k059
blender -b "Inbox/K005——摩天大楼建筑物城市街道楼房.blend" -P tools/blender_k059_export.py -- k005 Inbox/_k059/export_k005 [编号,编号...]
- k059：小品组 Export 2..53 → SM_CS_Vig<nn>（去掉人行道板，保留原包原点：带人行道的组原点在人行道中间，地面高 0）；
        Export 1 的 83 件道具 → SM_CS_<名字>（原点挪到底面中心）
- k005：楼 Collection <n> → SM_CB_C<nn>，放大 2.4 倍（原包一层只有 1.35 米），原点底面中心
FBX 按厘米导出（apply_unit_scale + 场景单位 0.01 的做法和机体一样不需要：静态网格 UE 按 1 米 = 100 厘米读，导出时 apply_unit_scale 就对）。
"""
import json
import os
import re
import sys

import bpy
from mathutils import Matrix, Vector

args = sys.argv[sys.argv.index('--') + 1:]
MODE, OUT = args[0], os.path.abspath(args[1])
PICK = [int(x) for x in args[2].split(',')] if len(args) > 2 and args[2] else None
S005 = 2.4
os.makedirs(f'{OUT}/raw', exist_ok=True)
sc = bpy.context.scene
deps = bpy.context.evaluated_depsgraph_get()


def clean(s):
    return re.sub(r'[^A-Za-z0-9_]', '_', s).strip('_') or 'x'


# ---------------------------------------------------------------- 贴图：原样写出（内嵌的直接写字节）
written = {}


def save_image(img):
    if img is None:
        return None
    if img.name in written:
        return written[img.name]
    base = os.path.basename((img.filepath or img.name).replace('\\', '/'))
    stem, ext = os.path.splitext(base)
    ext = ext.lower() if ext else '.png'
    name = clean(stem) + ext
    dst = f'{OUT}/raw/{name}'
    if not os.path.exists(dst):
        if img.packed_file:
            open(dst, 'wb').write(img.packed_file.data)
        else:
            p = bpy.path.abspath(img.filepath)
            if not os.path.exists(p):
                written[img.name] = None
                return None
            open(dst, 'wb').write(open(p, 'rb').read())
    written[img.name] = name
    return name


def upstream_image(sock, depth=0):
    """沿连线往上找第一张贴图（穿过法线贴图 / 运算 / 混合节点）"""
    if not sock.is_linked or depth > 4:
        return None
    n = sock.links[0].from_node
    if n.bl_idname == 'ShaderNodeTexImage':
        return n.image
    for i in n.inputs:
        if i.is_linked:
            r = upstream_image(i, depth + 1)
            if r:
                return r
    return None


# K005 没贴图 / 连错线的材质：手填（底色、粗糙度、金属度）
K005_FLAT = {
    'GLS': ((0.10, 0.12, 0.14), 0.08, 0.5), 'GLS_Thick': ((0.09, 0.11, 0.13), 0.1, 0.5),
    'Interiors': ((0.06, 0.06, 0.065), 0.9, 0.0), 'Chrome': ((0.6, 0.6, 0.62), 0.25, 1.0),
    'SmoothBlackMetal': ((0.03, 0.03, 0.035), 0.4, 1.0), 'GreenMetal': ((0.12, 0.2, 0.16), 0.5, 0.6),
    'Awning': ((0.2, 0.2, 0.22), 0.8, 0.0), 'WindowShade': ((0.55, 0.53, 0.5), 0.9, 0.0),
    'WhiteLogo': ((0.8, 0.8, 0.8), 0.5, 0.0), 'MullionDK': ((0.08, 0.085, 0.09), 0.5, 0.8),
    'BlkMetal': ((0.06, 0.06, 0.065), 0.5, 0.8), 'YellowPainted': ((0.6, 0.45, 0.08), 0.6, 0.0),
    'Dots Stroke': ((0.7, 0.7, 0.7), 0.6, 0.0),
}
K005_METAL = {'GalvanizedMetal': 1.0}


def describe(m):
    """材质 → {'base': 文件|None, 'normal': 文件, 'height': 文件, 'rough': 文件, 'metal': 文件, 'opacity':, 'emissive':,
               'color': [r,g,b], 'rough_v': 0.x, 'metal_v': 0.x, 'masked': bool}"""
    d = {'color': [0.8, 0.8, 0.8], 'rough_v': 0.6, 'metal_v': 0.0, 'masked': False}
    if MODE == 'k005' and m.name.split('.')[0] in K005_FLAT:
        c, r, mt = K005_FLAT[m.name.split('.')[0]]
        d.update(color=list(c), rough_v=r, metal_v=mt)
        return d
    if not m.node_tree:
        d['color'] = list(m.diffuse_color[:3])
        return d
    if MODE == 'k059':
        # KB3D_CTS_<名>_<basecolor|metallic|roughness|normal|opacity|emissive>.png
        for n in m.node_tree.nodes:
            if n.bl_idname == 'ShaderNodeTexImage' and n.image:
                role = os.path.splitext(n.image.name)[0].rsplit('_', 1)[-1].lower()
                key = {'basecolor': 'base', 'metallic': 'metal', 'roughness': 'rough', 'normal': 'normal',
                       'opacity': 'opacity', 'emissive': 'emissive'}.get(role)
                if key:
                    d[key] = save_image(n.image)
        d['masked'] = bool(d.get('opacity'))
        return d
    # K005：Base Color / Roughness 接的贴图；法线口接的图按文件名分 Normal / Bump（高度）
    P = next((n for n in m.node_tree.nodes if n.bl_idname == 'ShaderNodeBsdfPrincipled'), None)
    if not P:
        return d
    if not P.inputs['Base Color'].is_linked:
        d['color'] = list(P.inputs['Base Color'].default_value[:3])
    b = upstream_image(P.inputs['Base Color'])
    if b:
        d['base'] = save_image(b)
    r = upstream_image(P.inputs['Roughness'])
    if r and 'rough' in (r.filepath or r.name).lower():
        d['rough'] = save_image(r)
    else:
        d['rough_v'] = 0.85
    nimg = upstream_image(P.inputs['Normal'])
    if nimg:
        low = (nimg.filepath or nimg.name).lower()
        if 'normal' in low:
            d['normal'] = save_image(nimg)
        elif 'bump' in low:
            d['height'] = save_image(nimg)
    d['metal_v'] = K005_METAL.get(m.name.split('.')[0], 0.0)
    return d


# ---------------------------------------------------------------- 网格
groups = []   # (名字, [物体], 原点处理: 'keep' | 'bottom', 缩放)
if MODE == 'k059':
    exp = {c.name: c for c in bpy.data.collections if c.name.startswith('Export ')}
    for k in range(2, 54):
        objs = [o for o in exp[f'Export {k}'].all_objects if o.type == 'MESH' and 'SideWalk' not in o.name]
        groups.append((f'SM_CS_Vig{k:02d}', objs, 'keep', 1.0))
    for o in sorted((o for o in exp['Export 1'].all_objects if o.type == 'MESH'), key=lambda o: o.name):
        groups.append(('SM_CS_' + clean(o.name.replace('KB3D_CTS_', '').replace('_Main', '')), [o], 'bottom', 1.0))
else:
    ids = PICK or [int(c.name.split()[1]) for c in bpy.data.collections if re.fullmatch(r'Collection \d+', c.name)]
    for k in ids:
        c = bpy.data.collections.get(f'Collection {k}')
        if not c:
            continue
        objs = [o for o in c.all_objects if o.type in ('MESH', 'CURVE') and not o.hide_render]
        groups.append((f'SM_CB_C{k:02d}', objs, 'bottom', S005))

made, meta, used = [], [], set()
for name, objs, origin, scale in groups:
    parts = []
    for o in objs:
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
        po = bpy.data.objects.new(name + '_p', me)
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
    ob.name = ob.data.name = name
    vs = [v.co for v in ob.data.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    if origin == 'bottom':
        ob.data.transform(Matrix.Translation(-Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))))
    if scale != 1.0:
        ob.data.transform(Matrix.Scale(scale, 4))
    vs = [v.co for v in ob.data.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    mats = [m.name for m in ob.data.materials if m]
    used.update(mats)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    meta.append({'name': name, 'min': list(lo), 'max': list(hi), 'materials': mats, 'tris': tris})
    made.append(ob)
    print(f'[export] {name} {hi.x - lo.x:.1f}x{hi.y - lo.y:.1f}x{hi.z - lo.z:.1f} 米 {tris} 面')

# 地面 / 方块用的世界投影材质（M_WorldPBR）：人行道板导出时去掉了，材质也要单独带出来
EXTRA = ['KB3D_CTS_AsphaltA', 'KB3D_CTS_AsphaltBreak', 'KB3D_CTS_Sidewalk', 'KB3D_CTS_StreetTiles', 'KB3D_CTS_ConcreteRoad',
         'KB3D_CTS_ConcreteDecorA', 'KB3D_CTS_SoilGravel', 'KB3D_CTS_SubwayTiles', 'KB3D_CTS_SteelRoughGray',
         'KB3D_CTS_SteelPaintBlackA', 'KB3D_CTS_MetalPaintGrayA', 'KB3D_CTS_MetalPaintWhite'] if MODE == 'k059' else []
materials = {m: describe(bpy.data.materials[m]) for m in sorted(used | {e for e in EXTRA if e in bpy.data.materials})}
bpy.ops.object.select_all(action='DESELECT')
for ob in made:
    ob.select_set(True)
bpy.ops.export_scene.fbx(filepath=f'{OUT}/{MODE}.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                         mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
                         axis_forward='-Z', axis_up='Y')
json.dump({'mode': MODE, 'meshes': meta, 'materials': materials}, open(f'{OUT}/meshes.json', 'w', encoding='utf-8'),
          ensure_ascii=False, indent=1)
print(f'[export] {MODE}: {len(meta)} 个网格，{sum(m["tris"] for m in meta)} 面，材质 {len(materials)}，原始贴图 {len(os.listdir(OUT + "/raw"))} 张 → {OUT}')
