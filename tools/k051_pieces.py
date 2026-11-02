"""K051：每个模块（集合 47 个 + Export 集合里的 36 件道具）单独拍一张 Workbench 斜视图，再拼成对照表。
图上标名字和尺寸；旁边放一个 2.5 米的人形柱当比例尺。
blender -b K051.blend -P tools/k051_pieces.py -- 输出目录
"""
import os
import sys

import bpy
from mathutils import Vector

OUT = sys.argv[sys.argv.index('--') + 1]
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'
sh.color_type = 'TEXTURE'
sh.show_shadows = True
sh.show_cavity = True
sh.cavity_type = 'BOTH'
try:
    sh.background_type = 'VIEWPORT'
    sh.background_color = (0.42, 0.46, 0.52)
except Exception:
    pass
sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = 640, 480, 100

# 比例尺：2.5 米橙色柱
bpy.ops.mesh.primitive_cylinder_add(radius=0.45, depth=2.5, location=(0, 0, 1.25))
ruler = bpy.context.active_object
ruler.name = 'Ruler'
rm = bpy.data.materials.new('RulerMat')
rm.diffuse_color = (1, 0.35, 0.05, 1)
ruler.data.materials.append(rm)
ruler.color = (1, 0.35, 0.05, 1)

cd = bpy.data.cameras.new('PieceCam')
cam = bpy.data.objects.new('PieceCam', cd)
sc.collection.objects.link(cam)
sc.camera = cam
cd.lens = 35
cd.clip_start, cd.clip_end = 0.1, 5000

meshes = [o for o in sc.objects if o.type == 'MESH' and o.name != 'Ruler']
root = sc.collection.children[0]
groups = []
for c in root.children:
    obs = [o for o in c.all_objects if o.type == 'MESH']
    if c.name == 'Export':
        for o in obs:
            groups.append((o.name.replace('KB3D_HTS_', '').replace('_Main', ''), [o]))
    elif obs:
        parts = sorted(o.name.replace('KB3D_HTS_', '').split('_') for o in obs)[0]
        nm = '_'.join(parts[:2])
        groups.append((nm, obs))

lines = []
for name, obs in groups:
    keep = set(obs) | {ruler}
    for o in meshes:
        o.hide_render = o not in keep
    pts = [o.matrix_world @ Vector(b) for o in obs for b in o.bound_box]
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    size = hi - lo
    ctr = (lo + hi) / 2
    ruler.location = (hi.x + 1.0, lo.y - 1.0, lo.z + 1.25)
    r = max(size.length, 4.0) * 1.05
    cam.location = ctr + Vector((r * 0.62, -r * 0.85, r * 0.55))
    cam.rotation_euler = (ctr - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = f'{OUT}/{name}.png'
    bpy.ops.render.render(write_still=True)
    lines.append(f'{name}\t{size.x:.1f}x{size.y:.1f}x{size.z:.1f}\tz {lo.z:.1f}..{hi.z:.1f}\t{len(obs)}')
open(f'{OUT}/pieces.txt', 'w', encoding='utf-8').write('\n'.join(lines))
print('[k051] 拍了', len(groups), '件')
