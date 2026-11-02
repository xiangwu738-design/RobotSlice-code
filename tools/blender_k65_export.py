"""月球基地：K65（KB3D Lunar Base）整包导出给 UE。原点 = 素材原点（所有组 / 零件都叠在世界原点），UE 摆放时按原点转。
blender -b Inbox/K65_月球基地.blend -P tools/blender_k65_export.py -- Inbox/_k65/export
- 34 组预拼好的建筑（Export 2..35）→ SM_K65_<组名，如 LG_A>_Solid / _Deco / _Glass；Export 1 的 43 件单个零件 → SM_K65_P_<零件>_Solid / _Deco / _Glass
- Solid：有碰撞。Deco：不要碰撞的细件（天线、灯、栏杆、树、线缆、贴花、太阳能板……，机体高速飞过不会被细杆卡住）。Glass：透明玻璃（UE 里单独半透明材质，不开 Nanite）
- 同一物体里线缆 / 贴花 / 透明玻璃 / 树的面按材质拆出去（穹顶的拱架和拉索是同一个物体）
输出 k65.fbx + meshes.json（和 tools/k059_pack_textures.py 同格式，贴图整理接着用它）+ raw/（原始贴图）
FBX 按 blender_k059_export.py 的设置（UE 里按 (x, -y, z) 读）。
"""
import json
import os
import re
import sys

import bmesh
import bpy
from mathutils import Vector

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
os.makedirs(f'{OUT}/raw', exist_ok=True)
scene_main = next(s for s in bpy.data.scenes if s.name != 'Scene' or len(s.objects) > 100)
sc = bpy.context.scene
deps = bpy.context.evaluated_depsgraph_get()
coll = {c.name: c for c in bpy.data.collections}

DECO_NAME = re.compile(r'_(Antenna[A-Z]?|Lamp[A-Z]?|Railing|Trees|Console[A-Z]?|Planter|WindTurbine[A-Z]?|SolarPanels?[A-Z]?)$|^KB3D_LNB_(Antenna_[B-H]|Lamp_[AB]|Bench_A|Planter_A|SolarPanels_[AB])_Main$')
FACE_DECO = {'KB3D_LNB_Cable', 'KB3D_LNB_Decals', 'KB3D_LNB_TreeWalnutBark', 'KB3D_LNB_TreeWlnutLeaf', 'Dots Stroke'}
FACE_GLASS = {'KB3D_LNB_GlassBlueTransparent'}


def clean(s):
    return re.sub(r'[^A-Za-z0-9_]', '_', s).strip('_') or 'x'


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


def describe(m):
    d = {'color': [0.8, 0.8, 0.8], 'rough_v': 0.6, 'metal_v': 0.0, 'masked': False}
    if not m.use_nodes or not m.node_tree:
        d['color'] = list(m.diffuse_color[:3])
        return d
    for n in m.node_tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            role = os.path.splitext(n.image.name)[0].rsplit('_', 1)[-1].lower()
            key = {'basecolor': 'base', 'metallic': 'metal', 'roughness': 'rough', 'normal': 'normal', 'opacity': 'opacity', 'emissive': 'emissive'}.get(role)
            if key:
                d[key] = save_image(n.image)
    d['masked'] = bool(d.get('opacity'))
    return d


def object_is_deco(o):
    if DECO_NAME.search(o.name):
        return True
    dims = [o.dimensions[i] for i in range(3)]
    return max(dims[0], dims[1]) < 1.0 or dims[2] < 0.5     # 很小 / 很矮的东西不挡机体（城区图同规则）


groups = []
for k in range(2, 36):
    c = coll.get(f'Export {k}')
    if not c:
        continue
    obs = [o for o in c.all_objects if o.type == 'MESH']
    name = obs[0].name.replace('KB3D_LNB_', '').replace('Bldg', '').rsplit('_', 1)[0]
    groups.append((name, obs))
for o in sorted((o for o in coll['Export 1'].all_objects if o.type == 'MESH'), key=lambda o: o.name):
    groups.append(('P_' + o.name.replace('KB3D_LNB_', '').replace('_Main', ''), [o]))
# Export 36 是 Tank_A：当零件
c36 = coll.get('Export 36')
if c36:
    groups.append(('P_Tank_A', [o for o in c36.all_objects if o.type == 'MESH']))

made, meta, used = [], [], set()
total = 0


def finish(ob, full):
    global total
    vs = [v.co for v in ob.data.vertices]
    if not vs:
        return
    lo = [min(v[i] for v in vs) for i in range(3)]
    hi = [max(v[i] for v in vs) for i in range(3)]
    mats = [m.name for m in ob.data.materials if m]
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    total += tris
    used.update(mats)
    meta.append({'name': full, 'min': lo, 'max': hi, 'materials': mats, 'tris': tris})
    made.append(ob)
    print(f'[k65] {full} {hi[0]-lo[0]:.1f}x{hi[1]-lo[1]:.1f}x{hi[2]-lo[2]:.1f} 米 {tris} 面', flush=True)


for gname, obs in groups:
    parts = {'Solid': [], 'Deco': [], 'Glass': []}
    for o in obs:
        me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), depsgraph=deps)
        if not me.polygons:
            bpy.data.meshes.remove(me)
            continue
        # 别用 me.materials.clear()：它会把每个面的材质编号也清零（全部面变成第一个材质）。按槽逐个写（槽挂在物体上的情况也算）
        for i, s in enumerate(o.material_slots):
            if i < len(me.materials):
                me.materials[i] = s.material
            else:
                me.materials.append(s.material)
        me.transform(o.matrix_world)
        if o.matrix_world.determinant() < 0:
            me.flip_normals()
        po = bpy.data.objects.new('tmp', me)
        sc.collection.objects.link(po)
        parts['Deco' if object_is_deco(o) else 'Solid'].append(po)
    for cls, lst in parts.items():
        if not lst:
            continue
        bpy.ops.object.select_all(action='DESELECT')
        for p in lst:
            p.select_set(True)
        bpy.context.view_layer.objects.active = lst[0]
        if len(lst) > 1:
            bpy.ops.object.join()
        ob = bpy.context.view_layer.objects.active
        ob.name = ob.data.name = f'SM_K65_{gname}_{cls}'
        parts[cls] = [ob]
    # 面按材质拆出去：Solid → Deco / Glass；Deco → Glass（透明材质不能进 Nanite）
    def split(src_cls, names, dst_cls):
        if not parts[src_cls]:
            return
        ob = parts[src_cls][0]
        slots = [i for i, m in enumerate(ob.data.materials) if m and m.name in names]
        if not slots:
            return
        if len(slots) == len(ob.data.materials) and not parts[dst_cls]:
            ob.name = ob.data.name = f'SM_K65_{gname}_{dst_cls}'
            parts[dst_cls] = [ob]
            parts[src_cls] = []
            return
        bpy.ops.object.select_all(action='DESELECT')
        ob.select_set(True)
        bpy.context.view_layer.objects.active = ob
        before = set(bpy.data.objects)
        bpy.ops.object.mode_set(mode='EDIT')
        bm = bmesh.from_edit_mesh(ob.data)
        n = 0
        for f in bm.faces:
            f.select = f.material_index in slots
            n += f.select
        bmesh.update_edit_mesh(ob.data)
        if n and n < len(bm.faces):
            bpy.ops.mesh.separate(type='SELECTED')
        bpy.ops.object.mode_set(mode='OBJECT')
        new = [x for x in bpy.data.objects if x not in before]
        if n and not new:      # 全部面都是这类材质
            ob.name = ob.data.name = f'SM_K65_{gname}_{dst_cls}'
            if parts[dst_cls]:
                bpy.ops.object.select_all(action='DESELECT')
                parts[dst_cls][0].select_set(True)
                ob.select_set(True)
                bpy.context.view_layer.objects.active = parts[dst_cls][0]
                bpy.ops.object.join()
            else:
                parts[dst_cls] = [ob]
            parts[src_cls] = []
            return
        for x in new:
            if parts[dst_cls]:
                bpy.ops.object.select_all(action='DESELECT')
                parts[dst_cls][0].select_set(True)
                x.select_set(True)
                bpy.context.view_layer.objects.active = parts[dst_cls][0]
                bpy.ops.object.join()
            else:
                x.name = x.data.name = f'SM_K65_{gname}_{dst_cls}'
                parts[dst_cls] = [x]

    split('Solid', FACE_GLASS, 'Glass')
    split('Deco', FACE_GLASS, 'Glass')
    split('Solid', FACE_DECO, 'Deco')
    for cls, lst in parts.items():
        if lst:
            ob = lst[0]
            ob.name = ob.data.name = f'SM_K65_{gname}_{cls}'
            # 用不到的材质槽清掉（拆完后槽还在，UE 里会多出空段）
            bpy.ops.object.select_all(action='DESELECT')
            ob.select_set(True)
            bpy.context.view_layer.objects.active = ob
            try:
                bpy.ops.object.material_slot_remove_unused()
            except Exception as e:
                print('[k65] 清槽失败', ob.name, e)
            finish(ob, ob.name)

materials = {m: describe(bpy.data.materials[m]) for m in sorted(used)}
bpy.ops.object.select_all(action='DESELECT')
for ob in made:
    ob.select_set(True)
bpy.ops.export_scene.fbx(filepath=f'{OUT}/k65.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                         mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
                         axis_forward='-Z', axis_up='Y')
json.dump({'mode': 'k65', 'meshes': meta, 'materials': materials}, open(f'{OUT}/meshes.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'[k65] 完成：{len(meta)} 个网格，{total} 面，材质 {len(materials)}，原始贴图 {len(os.listdir(OUT + "/raw"))} 张 → {OUT}', flush=True)
