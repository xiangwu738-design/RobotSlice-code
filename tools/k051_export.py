"""把 K051 整包模块（加上 k051_layout.py 布局表里用到的 skip / only 变体）导出成 UE 用的网格和贴图。
每个模块（含 skip / only 变体）按用途合并成最多三个网格，原点都是模块原点（素材里所有模块都叠在世界原点）：
- SM_HTS_<模块>_Solid：有碰撞（楼体、路面、支柱、门……）
- SM_HTS_<模块>_Deco：不要碰撞的细件（路灯、椅子、长凳、栏杆、天线……），机体高速飞过不会被细杆卡住
- SM_HTS_<模块>_Holo：材质全是 Holo* 的全息片（UE 里用半透明叠加材质，不投影、无碰撞）
输出：meshes.fbx、export.json（每个网格的模块、类别、材质、面数；每个材质的贴图）、textures/（原始贴图）
blender -b K051.blend -P tools/k051_export.py -- layout.json 输出目录
"""
import json
import os
import re
import sys

import bpy

argv = sys.argv[sys.argv.index('--') + 1:]
LAYOUT, OUT = argv[0], argv[1]
os.makedirs(f'{OUT}/textures', exist_ok=True)
L = json.load(open(LAYOUT, encoding='utf-8'))
sc = bpy.context.scene
root = sc.collection.children[0]
deps = bpy.context.evaluated_depsgraph_get()

pieces = {}
for c in root.children:
    obs = [o for o in c.all_objects if o.type == 'MESH']
    if c.name == 'Export':
        for o in obs:
            pieces[o.name.replace('KB3D_HTS_', '').replace('_Main', '')] = [o]
    elif obs:
        parts = sorted(o.name.replace('KB3D_HTS_', '').split('_') for o in obs)[0]
        pieces['_'.join(parts[:2])] = obs

DECO = re.compile(r'_(Lamp|Chair|Bench|Console|Table|Railing|Antenna|CoffeMachine|Device|Light)[A-Z]?$')


def key_name(it):
    k = it['piece']
    if it.get('skip'):
        k += '_No' + ''.join(it['skip'])
    if it.get('only'):
        k += '_Only' + ''.join(it['only'])
    return k


def kind(o):
    mats = [s.material.name for s in o.material_slots if s.material]
    if mats and all('Holo' in m for m in mats):
        return 'Holo'
    if DECO.search(o.name):
        return 'Deco'
    return 'Solid'


keys = {p: {'piece': p} for p in pieces}          # 整包都导（以后改布局不用重导），再加布局里用到的 skip / only 变体
for it in L['items']:
    if not it['piece'].startswith(('TPB_', 'BRAND_')):       # 远景高楼来自 K005，另有 k051_backdrop_export.py
        keys.setdefault(key_name(it), it)

made, meshes = [], []
for k, it in sorted(keys.items()):
    obs = pieces[it['piece']]
    if it.get('skip'):
        obs = [o for o in obs if not any(o.name.endswith('_' + s) for s in it['skip'])]
    if it.get('only'):
        obs = [o for o in obs if any(o.name.endswith('_' + s) for s in it['only'])]
    groups = {}
    for o in obs:
        groups.setdefault(kind(o), []).append(o)
    for cls, members in groups.items():
        parts = []
        for o in members:
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
            po = bpy.data.objects.new('tmp', me)
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
        name = f'SM_HTS_{k}_{cls}'
        ob.name = ob.data.name = name
        made.append(ob)
        tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
        meshes.append({'name': name, 'key': k, 'class': cls, 'tris': tris,
                       'materials': [m.name for m in ob.data.materials if m]})
        print(f'[export] {name} {tris} 面')

# ---------------------------------------------------------------- 材质和贴图
ROLE = (('basecolor', 'D'), ('normal', 'N'), ('roughness', 'R'), ('metallic', 'M'), ('emissive', 'E'))
used = sorted({m for x in meshes for m in x['materials']})
materials = {}
for name in used:
    m = bpy.data.materials[name]
    tex = {}
    info = {'textures': tex, 'base': [0.8, 0.8, 0.8], 'rough': 0.5, 'metal': 0.0, 'alpha': 1.0}
    for n in m.node_tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            low = n.image.name.lower()
            role = next((r for k, r in ROLE if k in low), None)
            if not role:
                continue
            fn = re.sub(r'[^A-Za-z0-9_]', '_', os.path.splitext(n.image.name)[0]) + '.png'
            dst = f'{OUT}/textures/{fn}'
            if not os.path.exists(dst) and n.image.packed_file:
                open(dst, 'wb').write(n.image.packed_file.data)
            tex[role] = fn
        if n.bl_idname == 'ShaderNodeBsdfPrincipled':
            info['base'] = list(n.inputs['Base Color'].default_value[:3])
            info['rough'] = n.inputs['Roughness'].default_value
            info['metal'] = n.inputs['Metallic'].default_value
            info['alpha'] = n.inputs['Alpha'].default_value
            info['transmission'] = n.inputs['Transmission Weight'].default_value if 'Transmission Weight' in n.inputs else 0
    materials[name] = info

bpy.ops.object.select_all(action='DESELECT')
for ob in made:
    ob.select_set(True)
bpy.ops.export_scene.fbx(filepath=f'{OUT}/meshes.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                         mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False)
json.dump({'meshes': meshes, 'materials': materials}, open(f'{OUT}/export.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'[export] {len(meshes)} 个网格、{sum(x["tris"] for x in meshes)} 面、材质 {len(materials)} → {OUT}')
