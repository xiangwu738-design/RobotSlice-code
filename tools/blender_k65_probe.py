"""月球基地：K65 每组的正交俯视图（带 10 米网格的底图用 PIL 后加）+ 穹顶门洞能不能穿过去（射线）。只读，不存 .blend。
blender -b Inbox/K65_月球基地.blend -P tools/blender_k65_probe.py -- 输出目录 [top] [ray]
"""
import json
import math
import os
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
OUT = os.path.abspath(args[0])
STEPS = args[1:] or ['top', 'ray']
os.makedirs(OUT, exist_ok=True)
try:
    bpy.context.preferences.system.gl_texture_limit = 'CLAMP_512'
except Exception:
    pass
coll = {c.name: c for c in bpy.data.collections}


def group(k):
    return [o for o in coll[f'Export {k}'].all_objects if o.type == 'MESH']


if 'ray' in STEPS:
    deps = bpy.context.evaluated_depsgraph_get()
    objs = group(35)

    def cast(orig, d, maxd):
        """沿射线找所有物体的命中：返回 [(距离, 物体短名)] 排序"""
        hits = []
        for o in objs:
            inv = o.matrix_world.inverted()
            lo = inv @ Vector(orig)
            ld = (inv.to_3x3() @ Vector(d)).normalized()
            t = 0.0
            for _ in range(6):
                ok, loc, nor, idx = o.ray_cast(lo + ld * t, ld, distance=maxd)
                if not ok:
                    break
                w = o.matrix_world @ loc
                dist = (w - Vector(orig)).length
                if dist > maxd:
                    break
                hits.append((round(dist, 2), o.name.replace('KB3D_LNB_BldgXLG_C_', '')))
                t = (loc - lo).length + 0.02
        return sorted(hits)
    print('[ray] 从外往穹顶中心（沿 Y，从 y=-95 走到 y=-30），不同高度和左右偏移')
    for x in (-5.0, -3.0, -1.5, 0.0, 1.5, 3.0, 5.0):
        for z in (0.8, 1.5, 2.5, 3.5, 5.0, 7.0):
            hs = cast((x, -95.0, z), (0, 1, 0), 65.0)
            print(f'[ray] x={x:5.1f} z={z:4.1f}:', [(round(-95 + h[0], 1), h[1]) for h in hs][:8])
    print('[ray] 俯射，看 Gate/Main 的顶高（沿 y=-90...-50）')
    for y in range(-90, -48, 4):
        hs = cast((0.0, float(y), 60.0), (0, 0, -1), 62.0)
        print(f'[ray] y={y}:', [(round(60 - h[0], 1), h[1]) for h in hs][:5])

if 'top' in STEPS:
    view = bpy.data.scenes.new('top')
    view.render.engine = 'BLENDER_WORKBENCH'
    sh = view.display.shading
    sh.light = 'STUDIO'
    sh.color_type = 'TEXTURE'
    sh.show_cavity = True
    sh.cavity_type = 'BOTH'
    sh.background_type = 'VIEWPORT'
    sh.background_color = (0.16, 0.17, 0.19)
    view.render.image_settings.file_format = 'PNG'
    cam_d = bpy.data.cameras.new('c')
    cam_d.type = 'ORTHO'
    cam = bpy.data.objects.new('c', cam_d)
    view.collection.objects.link(cam)
    view.camera = cam
    holder = bpy.data.collections.new('h')
    view.collection.children.link(holder)
    if bpy.context.window:
        bpy.context.window.scene = view
    index = []
    for k in range(2, 36):
        for o in list(holder.objects):
            holder.objects.unlink(o)
        objs = group(k)
        lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
        for o in objs:
            holder.objects.link(o)
            for c in o.bound_box:
                w = o.matrix_world @ Vector(c)
                lo = Vector(map(min, lo, w))
                hi = Vector(map(max, hi, w))
        # 固定比例：10 像素 = 1 米... 取 4 像素 / 米，最大建筑 182 米 → 约 760 像素
        wm, hm = (hi.x - lo.x) + 8, (hi.y - lo.y) + 8
        ppm = 4.0
        view.render.resolution_x = max(64, int(wm * ppm))
        view.render.resolution_y = max(64, int(hm * ppm))
        cam_d.ortho_scale = max(wm, hm)
        cam.location = ((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, hi.z + 20)
        cam.rotation_euler = (0, 0, 0)
        cam_d.clip_start, cam_d.clip_end = 1, (hi.z - lo.z) + 60
        fn = f'{k:02d}.png'
        view.render.filepath = os.path.join(OUT, fn)
        bpy.ops.render.render(write_still=True, scene=view.name)
        name = objs[0].name.replace('KB3D_LNB_', '').rsplit('_', 1)[0]
        index.append({'k': k, 'name': name, 'file': fn, 'x0': lo.x - 4, 'x1': hi.x + 4, 'y0': lo.y - 4, 'y1': hi.y + 4, 'zmin': lo.z, 'zmax': hi.z, 'ppm': ppm})
        print('[top]', k, name, [round(v, 1) for v in (lo.x, hi.x, lo.y, hi.y, lo.z, hi.z)])
    json.dump(index, open(os.path.join(OUT, 'top_index.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
