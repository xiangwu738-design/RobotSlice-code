"""老旧街道：每栋楼 / 主要道具单独拍四面 Workbench 图（只读）。输出目录/名字_方向.png
blender -b 老旧街道.blend -P tools/blender_oldstreet_thumbs.py -- 输出目录"""
import os
import sys

import bpy
from mathutils import Vector

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
NAMES = ['建筑', '旧房屋2', '旧房屋2.001', '立方体.116', '立方体.117', '立方体.118', '立方体.003', '立方体.002', '立方体.094', '立方体.095', '立方体', '立方体.113', '柱体',
         'F8~DQP[T5{QW7D[@4~E$(55', 'crossing man light', '柱体.008', '柱体.001', '平面.005', '配电箱']
for o in sc.objects:
    o.hide_render = True
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'
sh.color_type = 'TEXTURE'
sh.show_shadows = False
sh.show_cavity = False
sc.render.resolution_x, sc.render.resolution_y = 640, 480
cam = bpy.data.cameras.new('c')
cob = bpy.data.objects.new('c', cam)
sc.collection.objects.link(cob)
sc.camera = cob
cam.lens = 30
cam.clip_end = 2000
for i, n in enumerate(NAMES):
    o = bpy.data.objects.get(n)
    if not o:
        continue
    o.hide_render = False
    ev = o.evaluated_get(bpy.context.evaluated_depsgraph_get())
    cs = [ev.matrix_world @ Vector(c) for c in ev.bound_box]
    lo = Vector([min(c[k] for c in cs) for k in range(3)])
    hi = Vector([max(c[k] for c in cs) for k in range(3)])
    ctr = (lo + hi) / 2
    R = (hi - lo).length * 1.0 + 0.5
    for name, d in (('xp', (1, 0)), ('xm', (-1, 0)), ('yp', (0, 1)), ('ym', (0, -1))):
        pos = ctr + Vector((d[0], d[1], 0.25)).normalized() * R
        cob.location = pos
        cob.rotation_euler = (ctr - pos).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = os.path.join(OUT, f'{i:02d}_{name}.png')
        bpy.ops.render.render(write_still=True)
    o.hide_render = True
    print('[oldstreet] 拍', i, n)
