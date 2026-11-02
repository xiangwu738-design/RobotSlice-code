"""枪侧视（从 -Y 看，正交，Workbench），x -30..60、z -22..23 厘米，出图后用 PIL 画 5 厘米网格。
blender -b --factory-startup -P gun_side.py -- 输出目录 fbx ..."""
import bpy
import sys
import os
from mathutils import Vector

a = sys.argv[sys.argv.index('--') + 1:]
out = a[0]
os.makedirs(out, exist_ok=True)
X0, X1, Z0, Z1 = -20, 16, -14, 14
PX = 20   # 每厘米像素
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.light = 'STUDIO'
sc.display.shading.show_cavity = True
sc.render.resolution_x = (X1 - X0) * PX
sc.render.resolution_y = (Z1 - Z0) * PX
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
sc.collection.objects.link(cam)
sc.camera = cam
cam.data.type = 'ORTHO'
cam.data.ortho_scale = (X1 - X0) / 100
cam.location = ((X0 + X1) / 200, -2.0, (Z0 + Z1) / 200)
cam.rotation_euler = (1.5707963, 0, 0)
for f in a[1:]:
    for ob in [o for o in bpy.data.objects if o.type == 'MESH']:
        bpy.data.objects.remove(ob, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=f)
    name = f.replace('\\', '/').rsplit('/', 1)[-1].rsplit('.', 1)[0]
    sc.render.filepath = f'{out}/{name}.png'
    bpy.ops.render.render(write_still=True)
    print('[side]', name)
