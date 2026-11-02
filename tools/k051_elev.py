"""K051：指定模块的正交侧视 + 俯视（Workbench），看平台高度、桥面厚度、能不能走。
blender -b K051.blend -P tools/k051_elev.py -- 输出目录 名字前缀,名字前缀..
"""
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
OUT = argv[0]
WANT = argv[1].split(',')
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'
sh.color_type = 'TEXTURE'
sh.show_cavity = True
try:
    sh.background_type = 'VIEWPORT'
    sh.background_color = (0.42, 0.46, 0.52)
except Exception:
    pass
cd = bpy.data.cameras.new('C')
cam = bpy.data.objects.new('C', cd)
sc.collection.objects.link(cam)
sc.camera = cam
cd.type = 'ORTHO'
cd.clip_start, cd.clip_end = 0.1, 5000
meshes = [o for o in sc.objects if o.type == 'MESH']
for w in WANT:
    obs = [o for o in meshes if o.name.replace('KB3D_HTS_', '').startswith(w)]
    for o in meshes:
        o.hide_render = o not in obs
    pts = [o.matrix_world @ Vector(b) for o in obs for b in o.bound_box]
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    c = (lo + hi) / 2
    s = hi - lo
    for view, loc, rot, ortho in (('side', (c.x, lo.y - 100, c.z), (1.5708, 0, 0), max(s.x, s.z) * 1.1),
                                  ('end', (hi.x + 100, c.y, c.z), (1.5708, 0, 1.5708), max(s.y, s.z) * 1.1),
                                  ('top', (c.x, c.y, hi.z + 100), (0, 0, 0), max(s.x, s.y) * 1.1)):
        cam.location = loc
        cam.rotation_euler = rot
        cd.ortho_scale = ortho
        sc.render.resolution_x, sc.render.resolution_y = 800, 500
        sc.render.filepath = f'{OUT}/{w}_{view}.png'
        bpy.ops.render.render(write_still=True)
    # 每个物体的 z 范围
    for o in obs:
        p = [o.matrix_world @ Vector(b) for b in o.bound_box]
        print('[elev]', w, o.name, 'x', round(min(q.x for q in p), 2), round(max(q.x for q in p), 2), 'y', round(min(q.y for q in p), 2),
              round(max(q.y for q in p), 2), 'z', round(min(q.z for q in p), 2), round(max(q.z for q in p), 2))
