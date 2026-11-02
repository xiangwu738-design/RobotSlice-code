"""11-02 把导出的机体 FBX 重新导入拍着色对比（只读）：blender -b --factory-startup -P tools/blender_fbx_shots.py -- <fbx> <输出前缀>
Workbench 工作室光（能看出法线：平面是不是平的、边是不是硬的），绑定姿势，四个镜头：全身斜侧、胸口特写、腿特写、背面。"""
import math
import sys

import bpy
from mathutils import Vector

i = sys.argv.index('--')
fbx, prefix = sys.argv[i + 1], sys.argv[i + 2]
only = sys.argv[i + 3].split(',') if len(sys.argv) > i + 3 else None
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=fbx)
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
lo = Vector((1e9,) * 3)
hi = Vector((-1e9,) * 3)
for o in meshes:
    for c in o.bound_box:
        p = o.matrix_world @ Vector(c)
        lo = Vector((min(lo[k], p[k]) for k in range(3)))
        hi = Vector((max(hi[k], p[k]) for k in range(3)))
H = hi.z - lo.z
ctr = (lo + hi) / 2
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.light = 'STUDIO'
sc.display.shading.color_type = 'MATERIAL'
sc.display.shading.show_specular_highlight = True
sc.display.shading.show_cavity = False
sc.render.resolution_x, sc.render.resolution_y = 900, 900
sc.world = bpy.data.worlds.new('w') if sc.world is None else sc.world
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
sc.collection.objects.link(cam)
sc.camera = cam
cam.data.lens = 50
for o in meshes:
    for m in o.data.materials:
        if m is not None and m.diffuse_color[0] + m.diffuse_color[1] + m.diffuse_color[2] < 0.3:
            m.diffuse_color = (0.32, 0.33, 0.35, 1)   # 太黑的槽看不出明暗，提亮一点
# FBX 前方：v3 骨架正面朝 -Y
shots = [('全身斜侧', Vector((-0.75, -1.0, 0.25)), ctr, H * 1.15), ('胸口', Vector((-0.35, -1.0, 0.1)), Vector((ctr.x, ctr.y, lo.z + H * 0.72)), H * 0.42),
         ('腿', Vector((-0.4, -1.0, 0.15)), Vector((ctr.x, ctr.y, lo.z + H * 0.3)), H * 0.45), ('背面', Vector((0.5, 1.0, 0.2)), Vector((ctr.x, ctr.y, lo.z + H * 0.6)), H * 0.65)]
for name, d, tgt, size in shots:
    if only and name not in only:
        continue
    d = d.normalized()
    dist = size / (2 * math.tan(cam.data.angle / 2)) * 1.05
    cam.location = tgt + d * dist
    cam.rotation_euler = (tgt - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = f'{prefix}_{name}.png'
    bpy.ops.render.render(write_still=True)
    print('[kitfree] 拍好', sc.render.filepath, flush=True)
