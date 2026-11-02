"""K059 组 / 道具缩略图（Workbench 贴图色，只读不存 .blend）。
blender -b "Inbox/K059 _城市街道.blend" -P tools/blender_k059_thumbs.py -- 输出目录 [groups|props|re:正则] [边长]
groups：Export 2..53 每组一张斜俯视；props：Export 1 里 83 件单个道具各一张；
re:正则：别的包（K005 / K032 楼房）按集合名匹配，每个集合一张。
每张图名 = 编号_名字.png，另写 index.json（名字、尺寸 米）。"""
import json
import math
import os
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
out = os.path.abspath(args[0])
mode = args[1] if len(args) > 1 else 'groups'
res = int(args[2]) if len(args) > 2 else 512
os.makedirs(out, exist_ok=True)
try:
    bpy.context.preferences.system.gl_texture_limit = 'CLAMP_1024'
except Exception as e:
    print('[thumbs] 贴图限尺寸失败', e)

view = bpy.data.scenes.new('thumbs')
view.render.engine = 'BLENDER_WORKBENCH'
sh = view.display.shading
sh.light = 'STUDIO'
sh.color_type = 'TEXTURE'
sh.show_cavity = True
sh.cavity_type = 'BOTH'
sh.show_shadows = True
sh.background_type = 'VIEWPORT'
sh.background_color = (0.16, 0.17, 0.19)
view.render.resolution_x = view.render.resolution_y = res
view.render.image_settings.file_format = 'PNG'
cam_data = bpy.data.cameras.new('cam')
cam_data.lens = 55
cam = bpy.data.objects.new('cam', cam_data)
view.collection.objects.link(cam)
view.camera = cam
holder = bpy.data.collections.new('holder')
view.collection.children.link(holder)
if bpy.context.window:
    bpy.context.window.scene = view

exp = {c.name: c for c in bpy.data.collections if c.name.startswith('Export ')}
items = []
if mode.startswith('re:'):
    import re
    cs = [c for c in bpy.data.collections if re.search(mode[3:], c.name)]
    cs.sort(key=lambda c: int(re.sub(r'\D', '', c.name) or 0))
    for c in cs:
        items.append((int(re.sub(r'\D', '', c.name) or 0), c.name, [o for o in c.all_objects if o.type == 'MESH']))
elif mode == 'groups':
    for k in range(2, 54):
        c = exp.get(f'Export {k}')
        if c:
            items.append((k, c.name, [o for o in c.all_objects if o.type == 'MESH']))
else:
    for k, o in enumerate(sorted((o for o in exp['Export 1'].all_objects if o.type == 'MESH'), key=lambda o: o.name)):
        items.append((k, o.name, [o]))

index = []
for k, name, objs in items:
    for o in list(holder.objects):
        holder.objects.unlink(o)
    for o in objs:
        holder.objects.link(o)
    lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
    for o in objs:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
    ctr, rad = (lo + hi) / 2, max((hi - lo).length / 2, 0.05)
    d = Vector((0.9, -1.3, 0.75)).normalized()
    cam.location = ctr + d * rad / math.sin(cam_data.angle / 2) * 1.02
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    cam_data.clip_start, cam_data.clip_end = rad * 0.01, rad * 20
    short = name.replace('KB3D_CTS_', '').replace('_Main', '')
    if not objs:
        continue
    fn = f'{k:03d}_{short}.png'.replace(' ', '_')
    view.render.filepath = os.path.join(out, fn)
    bpy.ops.render.render(write_still=True, scene=view.name)
    size = [round(hi[i] - lo[i], 2) for i in range(3)]
    index.append({'id': k, 'name': name, 'file': fn, 'size': size, 'parts': [o.name for o in objs]})
    print(f'[thumbs] {k} {name} {size}')
json.dump(index, open(os.path.join(out, 'index.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
