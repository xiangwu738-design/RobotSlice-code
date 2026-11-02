"""科幻峡谷包：只显示指定物体，用 Workbench 拍正交 / 透视预览（看墙的形状、取哪一面）。
blender -b Scene_02.blend -P tools/blender_dreamwall_preview.py -- 输出目录 物体1,物体2 [视图,...]
视图：top / front（从 +X 往 -X 看）/ back（从 -X 往 +X 看）/ side（从 +Y 往 -Y 看）/ persp（场景相机）/ obl（斜俯视）
"""
import math
import os
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
OUT = os.path.abspath(args[0])
NAMES = args[1].split(',')
VIEWS = args[2].split(',') if len(args) > 2 else ['top', 'front', 'side', 'obl']
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
deps = bpy.context.evaluated_depsgraph_get()

keep = [bpy.data.objects[n] for n in NAMES if n in bpy.data.objects]
for o in sc.objects:
    o.hide_render = o not in keep and o.type != 'CAMERA'
# 包围盒（世界坐标，含修改器）
lo = Vector((1e9, 1e9, 1e9))
hi = -lo
for o in keep:
    ev = o.evaluated_get(deps)
    for c in ev.bound_box:
        w = o.matrix_world @ Vector(c)
        lo = Vector(map(min, lo, w))
        hi = Vector(map(max, hi, w))
ctr = (lo + hi) / 2
size = hi - lo
print('[preview] 范围', [round(v, 1) for v in lo], [round(v, 1) for v in hi])

sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'
sh.color_type = 'SINGLE'
sh.single_color = (0.8, 0.8, 0.82)
sh.show_cavity = True
sh.cavity_type = 'BOTH'
sh.show_shadows = False
sc.render.resolution_x = 1600
sc.render.resolution_y = 1000
sc.render.film_transparent = False
sc.render.image_settings.media_type = 'IMAGE' if hasattr(sc.render.image_settings, 'media_type') else None
sc.render.image_settings.file_format = 'PNG'
if sc.world:
    sc.world.color = (0.25, 0.27, 0.3)

cam_data = bpy.data.cameras.new('PrevCam')
cam = bpy.data.objects.new('PrevCam', cam_data)
sc.collection.objects.link(cam)
orig_cam = sc.camera


def shoot(name):
    cd = cam_data
    cd.clip_start = 0.1
    cd.clip_end = 100000
    if name == 'persp':
        sc.camera = orig_cam
        orig_cam.data.clip_end = 100000
    else:
        sc.camera = cam
        cd.type = 'ORTHO'
        d = max(size) * 3
        if name == 'top':
            cam.location = ctr + Vector((0, 0, d))
            cam.rotation_euler = (0, 0, 0)
            cd.ortho_scale = max(size.x, size.y) * 1.05
        elif name == 'front':
            cam.location = ctr + Vector((d, 0, 0))
            cam.rotation_euler = (math.pi / 2, 0, math.pi / 2)
            cd.ortho_scale = max(size.y, size.z * 1.6) * 1.05
        elif name == 'back':
            cam.location = ctr + Vector((-d, 0, 0))
            cam.rotation_euler = (math.pi / 2, 0, -math.pi / 2)
            cd.ortho_scale = max(size.y, size.z * 1.6) * 1.05
        elif name == 'side':
            cam.location = ctr + Vector((0, d, 0))
            cam.rotation_euler = (math.pi / 2, 0, math.pi)
            cd.ortho_scale = max(size.x, size.z * 1.6) * 1.05
        elif name == 'obl':
            cd.type = 'PERSP'
            cd.lens = 30
            r = max(size) * 1.1
            cam.location = ctr + Vector((r * 0.9, r * 0.6, r * 0.55))
            dirv = ctr - cam.location
            cam.rotation_euler = dirv.to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = f'{OUT}/{"_".join(NAMES)[:40]}_{name}.png'.replace('.', '_', 0)
    bpy.ops.render.render(write_still=True)
    print('[preview] 写出', sc.render.filepath)


for v in VIEWS:
    shoot(v)
