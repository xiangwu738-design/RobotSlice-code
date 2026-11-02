"""K62：每栋楼拍 Workbench 正交图（俯视 / 正面 / 侧面，旁边放 2.5 米机体红柱和 3 米一层的刻度），另存每栋零件明细。只读，不存 .blend。
blender -b Inbox/K62_苏联建筑.blend -P tools/k62_thumbs.py -- 输出目录 [BldgSM_A,BldgMD_C,...]
"""
import math
import os
import re
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
OUT = os.path.abspath(args[0])
ONLY = args[1].split(',') if len(args) > 1 else None
os.makedirs(OUT, exist_ok=True)
sc = bpy.data.scenes['KB3D_SovietBlocks-Native']
bpy.context.window_manager.windows[0].scene = sc if bpy.context.window_manager.windows else None

groups = {}
for o in sc.objects:
    if o.type != 'MESH':
        continue
    m = re.match(r'KB3D_SVB_(Bldg(?:LG|MD|SM)_[A-Z])', o.name)
    if m:
        groups.setdefault(m.group(1), []).append(o)
for o in sc.objects:
    o.hide_render = True

r = sc.render
r.engine = 'BLENDER_WORKBENCH'
r.resolution_percentage = 100
r.image_settings.file_format = 'JPEG'
r.image_settings.quality = 85
s = sc.display.shading
s.light = 'STUDIO'
s.color_type = 'OBJECT'
s.show_cavity = True
s.cavity_type = 'WORLD'
s.show_object_outline = True
s.show_shadows = False

# 红柱（2.5 米机体）和比例尺（每 3 米一格的条）
bpy.ops.mesh.primitive_cube_add(size=1)
mech = bpy.context.active_object
mech.name = 'MechRef'
mech.color = (1, 0.1, 0.1, 1)
cam_data = bpy.data.cameras.new('Ortho')
cam_data.type = 'ORTHO'
cam = bpy.data.objects.new('OrthoCam', cam_data)
sc.collection.objects.link(cam)
sc.camera = cam
scale_bars = []
for i in range(60):
    bpy.ops.mesh.primitive_cube_add(size=1)
    b = bpy.context.active_object
    b.name = f'Bar{i}'
    b.color = (0.05, 0.05, 0.05, 1) if i % 2 else (0.95, 0.85, 0.2, 1)
    b.hide_render = True
    scale_bars.append(b)


def shoot(name, group, view, lo, hi):
    c = (lo + hi) / 2
    size = hi - lo
    pad = 4
    if view == 'top':
        cam.location = (c.x, c.y, hi.z + 50)
        cam.rotation_euler = (0, 0, 0)
        w, h = size.x + pad * 2, size.y + pad * 2
    elif view == 'front':     # 看 +Y 方向（看到 -Y 面）
        cam.location = (c.x, lo.y - 80, c.z)
        cam.rotation_euler = (math.pi / 2, 0, 0)
        w, h = size.x + pad * 2, size.z + pad * 2
    elif view == 'side':      # 看 -X 方向（看到 +X 面）
        cam.location = (hi.x + 80, c.y, c.z)
        cam.rotation_euler = (math.pi / 2, 0, math.pi / 2)
        w, h = size.y + pad * 2, size.z + pad * 2
    else:                     # 斜视
        d = max(size.x, size.y, size.z) * 2
        cam.location = (c.x - d * 0.7, c.y - d * 0.7, c.z + d * 0.5)
        look = c - cam.location
        cam.rotation_euler = look.to_track_quat('-Z', 'Y').to_euler()
        w, h = max(size.x, size.y) * 1.7, max(size.x, size.y, size.z) * 1.3
    cam_data.ortho_scale = max(w, h)
    px = 20.0 * 0 + 900.0
    asp = w / h if h else 1
    if asp >= 1:
        r.resolution_x, r.resolution_y = int(px), max(64, int(px / asp))
    else:
        r.resolution_x, r.resolution_y = max(64, int(px * asp)), int(px)
    r.filepath = f'{OUT}/{name}_{view}.jpg'
    bpy.ops.render.render(write_still=True)


for g in sorted(groups):
    if ONLY and g not in ONLY:
        continue
    objs = groups[g]
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    for o in objs:
        o.hide_render = False
        n = o.name.replace('KB3D_SVB_' + g + '_', '')
        kind = re.sub(r'[A-Z]$', '', re.sub(r'\.\d+$', '', n))
        o.color = {'Building': (0.72, 0.72, 0.72, 1), 'Facade': (0.45, 0.65, 0.8, 1), 'Bridge': (0.3, 0.7, 0.4, 1), 'Stairs': (0.9, 0.6, 0.2, 1)}.get(kind, (0.85, 0.45, 0.75, 1))
    # 机体参照放在 -X -Y 角外
    mech.location = (lo.x - 1.5, lo.y - 1.5, 1.25)
    mech.scale = (1.2, 1.2, 2.5)
    for i, b in enumerate(scale_bars):
        hh = i * 3 + 1.5
        b.hide_render = hh > hi.z
        b.location = (lo.x - 3.2, lo.y - 3.2, hh)
        b.scale = (1.0, 1.0, 3.0)
    for v in ('top', 'front', 'side', 'iso'):
        shoot(g, g, v, Vector((lo.x - 4.5, lo.y - 4.5, lo.z)), hi)
    for o in objs:
        o.hide_render = True
    print('THUMB', g, flush=True)
print('THUMBS OK', flush=True)
