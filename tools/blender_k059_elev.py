"""楼房立面比例检查：把选中的集合并排摆开，旁边放 2.5 米机体方块和 1.8 米人方块，正交正视 + 侧视各一张（Workbench）。
blender -b K005.blend -P tools/blender_k059_elev.py -- 输出前缀 集合编号,编号,...
"""
import math
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
out, ids = args[0], [int(x) for x in args[1].split(',')]
try:
    bpy.context.preferences.system.gl_texture_limit = 'CLAMP_1024'
except Exception:
    pass
view = bpy.data.scenes.new('elev')
view.render.engine = 'BLENDER_WORKBENCH'
sh = view.display.shading
sh.light, sh.color_type, sh.show_cavity = 'STUDIO', 'TEXTURE', True
sh.background_type, sh.background_color = 'VIEWPORT', (0.75, 0.78, 0.82)
view.render.image_settings.file_format = 'PNG'
if bpy.context.window:
    bpy.context.window.scene = view
x = 0.0
H = 0.0
for k in ids:
    c = bpy.data.collections.get(f'Collection {k}')
    objs = [o for o in c.all_objects if o.type == 'MESH']
    lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
    for o in objs:
        for b in o.bound_box:
            w = o.matrix_world @ Vector(b)
            lo, hi = Vector(map(min, lo, w)), Vector(map(max, hi, w))
    inst = bpy.data.objects.new(f'I{k}', None)
    inst.instance_type = 'COLLECTION'
    inst.instance_collection = c
    inst.location = (x - lo.x, -lo.y, -lo.z)
    view.collection.objects.link(inst)
    x += (hi.x - lo.x) + 4
    H = max(H, hi.z - lo.z)
    print(f'[elev] C{k} 尺寸 {hi.x - lo.x:.1f}x{hi.y - lo.y:.1f}x{hi.z - lo.z:.1f}')
# 比例尺：2.5 米机体（橙）、1.8 米人（绿）、每 3 米一道横线
for name, h, w, col in (('mech', 2.5, 1.2, (1, .45, 0, 1)), ('man', 1.8, 0.5, (0, .8, .2, 1))):
    bpy.ops.mesh.primitive_cube_add(size=1)
    o = bpy.context.active_object
    o.scale = (w, w, h)
    o.location = (-3 if name == 'mech' else -1.5, -2, h / 2)
    m = bpy.data.materials.new(name)
    m.diffuse_color = col
    o.data.materials.append(m)
    for cc in o.users_collection:
        cc.objects.unlink(o)
    view.collection.objects.link(o)
for z in range(0, int(H) + 4, 3):
    bpy.ops.mesh.primitive_cube_add(size=1)
    o = bpy.context.active_object
    o.scale = (x + 8, 0.05, 0.06)
    o.location = (x / 2 - 4, -3, z)
    for cc in o.users_collection:
        cc.objects.unlink(o)
    view.collection.objects.link(o)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
cam.data.type = 'ORTHO'
cam.data.ortho_scale = max(x + 8, H * 1.6)
cam.location = (x / 2 - 4, -200, H / 2)
cam.rotation_euler = (math.radians(90), 0, 0)
cam.data.clip_end = 1000
view.collection.objects.link(cam)
view.camera = cam
view.render.resolution_x = 2400
view.render.resolution_y = int(2400 * min(1.0, H * 1.6 / max(x + 8, 1)) + 200)
view.render.filepath = out + '_front.png'
bpy.ops.render.render(write_still=True, scene=view.name)
print('[elev] 写入', out)
