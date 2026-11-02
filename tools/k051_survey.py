"""K051 高科技街道：看清楚包里有什么。
- 统计：物体、三角面、范围、单位、材质 / 贴图（多少张、尺寸、缺失）、集合树（每个集合件数、尺寸、位置）、实例最多的网格
- 渲染（只用 Workbench，不占显卡太多）：正交俯视 + 四个斜俯视 + 几张视平线（机体 2.5 米的眼睛高度约 2.2 米）
blender -b K051.blend -P tools/k051_survey.py -- 输出目录 [--no-render]
"""
import json
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
OUT = argv[0]
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
meshes = [o for o in sc.objects if o.type == 'MESH' and not o.hide_render]


def bbox(objs):
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    if not pts:
        return None, None
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    return lo, hi


lo, hi = bbox(meshes)
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes)
types = {}
for o in sc.objects:
    types[o.type] = types.get(o.type, 0) + 1
info = {'types': types, 'tris': tris, 'lo': list(lo), 'hi': list(hi), 'unit_scale': sc.unit_settings.scale_length,
        'scenes': [s.name for s in bpy.data.scenes], 'view_layers': [v.name for v in sc.view_layers]}
print('[k051] 物体', types, '三角面', tris)
print('[k051] 范围', [round(v, 1) for v in lo], '~', [round(v, 1) for v in hi], '单位', sc.unit_settings.scale_length)

# 贴图
imgs = []
for i in bpy.data.images:
    path = bpy.path.abspath(i.filepath) if i.filepath else ''
    imgs.append({'name': i.name, 'size': list(i.size), 'packed': bool(i.packed_file), 'file': i.filepath,
                 'exists': bool(i.packed_file) or (bool(path) and os.path.exists(path)), 'users': i.users})
info['images'] = imgs
print('[k051] 贴图', len(imgs), '内嵌', sum(1 for i in imgs if i['packed']), '缺失', sum(1 for i in imgs if not i['exists']))

# 材质
mats = {}
for m in bpy.data.materials:
    if m.users == 0:
        continue
    d = {'users': m.users, 'images': [], 'nodes': []}
    if m.use_nodes:
        d['nodes'] = sorted({n.bl_idname.replace('ShaderNode', '') for n in m.node_tree.nodes} - {'NodeReroute', 'NodeFrame'})
        for n in m.node_tree.nodes:
            if n.bl_idname == 'ShaderNodeTexImage' and n.image:
                to = [l.to_socket.name + '@' + l.to_node.bl_idname.replace('ShaderNode', '') for o in n.outputs for l in o.links]
                d['images'].append([n.image.name, list(n.image.size), to])
            if n.bl_idname == 'ShaderNodeBsdfPrincipled':
                d['base'] = list(n.inputs['Base Color'].default_value[:3])
                d['emis'] = list(n.inputs['Emission Color'].default_value[:3]) + [n.inputs['Emission Strength'].default_value]
    mats[m.name] = d
info['materials'] = mats
print('[k051] 材质', len(mats))


# 集合树
def walk(c, depth=0, out=None):
    out = out if out is not None else []
    obs = [o for o in c.all_objects if o.type == 'MESH']
    l, h = bbox(obs) if obs else (None, None)
    entry = {'name': c.name, 'depth': depth, 'n': len(obs), 'children': len(c.children)}
    if l:
        entry['size'] = [round(v, 2) for v in (h - l)]
        entry['center'] = [round(v, 2) for v in (h + l) / 2]
        entry['min_z'] = round(l.z, 2)
        entry['tris'] = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in obs)
    out.append(entry)
    if depth < 3:
        for ch in c.children:
            walk(ch, depth + 1, out)
    return out


tree = walk(sc.collection)
info['collections'] = tree
for e in tree[:200]:
    if e['n']:
        print('[k051] ' + '  ' * e['depth'] + f"{e['name']}: {e['n']} 件 子集合 {e['children']} 尺寸 {e.get('size')} 中心 {e.get('center')} 面 {e.get('tris')}")

uniq = {}
for o in meshes:
    uniq[o.data.name] = uniq.get(o.data.name, 0) + 1
info['top_instanced'] = sorted(uniq.items(), key=lambda kv: -kv[1])[:30]
print('[k051] 不同网格', len(uniq), '实例最多', info['top_instanced'][:10])

# 每个物体
objs = []
for o in meshes:
    l, h = bbox([o])
    objs.append({'name': o.name, 'data': o.data.name, 'coll': o.users_collection[0].name if o.users_collection else '',
                 'parent': o.parent.name if o.parent else None, 'lo': [round(v, 2) for v in l], 'hi': [round(v, 2) for v in h],
                 'tris': sum(len(p.vertices) - 2 for p in o.data.polygons), 'mats': [s.material.name if s.material else None for s in o.material_slots]})
info['objects'] = objs
info['lights'] = [{'name': o.name, 'type': o.data.type, 'loc': list(o.matrix_world.translation), 'energy': o.data.energy,
                   'color': list(o.data.color)} for o in sc.objects if o.type == 'LIGHT']
info['empties'] = [{'name': o.name, 'loc': list(o.matrix_world.translation), 'inst': o.instance_collection.name if o.instance_collection else None}
                   for o in sc.objects if o.type == 'EMPTY'][:400]
json.dump(info, open(f'{OUT}/survey.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[k051] 写出', f'{OUT}/survey.json')

if '--no-render' in argv:
    raise SystemExit

# ---------------------------------------------------------------- Workbench 渲染
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'
sh.color_type = 'TEXTURE'
sh.show_shadows = True
sh.show_cavity = True
sh.cavity_type = 'BOTH'
sh.show_object_outline = False
sc.display.shadow_focus = 0.2
sc.render.film_transparent = False
sh.background_type = 'VIEWPORT' if hasattr(sh, 'background_type') else None
try:
    sh.background_type = 'VIEWPORT'
    sh.background_color = (0.55, 0.6, 0.66)
except Exception:
    pass
ctr, size = (lo + hi) / 2, (hi - lo)


def cam(name, loc, target=None, ortho=None, lens=30, res=(1600, 1000), rot=None):
    cd = bpy.data.cameras.new(name)
    co = bpy.data.objects.new(name, cd)
    sc.collection.objects.link(co)
    co.location = loc
    if rot is not None:
        co.rotation_euler = rot
    else:
        co.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    cd.clip_start, cd.clip_end = 0.5, 20000
    if ortho:
        cd.type = 'ORTHO'
        cd.ortho_scale = ortho
    else:
        cd.lens = lens
    sc.camera = co
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.filepath = f'{OUT}/{name}.png'
    bpy.ops.render.render(write_still=True)
    print('[k051] 渲染', sc.render.filepath)


# 俯视：正交，按范围
s = max(size.x, size.y) * 1.05
cam('top', (ctr.x, ctr.y, hi.z + 50), ortho=s, res=(1600, int(1600 * size.y / max(size.x, 1))) if size.x >= size.y else (int(1600 * size.x / size.y), 1600),
    rot=(0, 0, 0))
r = max(size.x, size.y) * 0.85
for k, (dx, dy) in enumerate(((1, -1), (-1, -1), (-1, 1), (1, 1))):
    cam(f'oblique_{k}', ctr + Vector((dx * r * 0.7, dy * r * 0.7, r * 0.55)), target=ctr, lens=28)
# 视平线：沿场地对角线和中线，2.2 米高
eye = lo.z + 2.2
for k, (p, q) in enumerate((((lo.x + size.x * 0.1, ctr.y), (hi.x, ctr.y)),
                            ((ctr.x, lo.y + size.y * 0.1), (ctr.x, hi.y)),
                            ((lo.x + size.x * 0.15, lo.y + size.y * 0.15), (hi.x, hi.y)),
                            ((hi.x - size.x * 0.15, hi.y - size.y * 0.15), (lo.x, lo.y)))):
    cam(f'eye_{k}', (p[0], p[1], eye), target=(q[0], q[1], eye + 1.0), lens=20)
