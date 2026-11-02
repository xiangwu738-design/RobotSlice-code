"""素材包总览：按所有网格的范围摆一台斜 45° 俯视相机，加太阳和天光，用 Eevee 出一张图；顺带列出各组（集合）名字和尺寸。
blender -b 素材.blend -P kit_overview.py -- 输出.png"""
import math
import sys

import bpy
from mathutils import Vector

out = sys.argv[sys.argv.index('--') + 1]
sc = bpy.context.scene
meshes = [o for o in sc.objects if o.type == 'MESH' and not o.hide_render]
pts = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
lo = Vector([min(p[i] for p in pts) for i in range(3)])
hi = Vector([max(p[i] for p in pts) for i in range(3)])
ctr, size = (lo + hi) / 2, (hi - lo)
for c in bpy.data.collections:
    obs = [o for o in c.all_objects if o.type == 'MESH']
    if 0 < len(obs) < 200:
        p = [o.matrix_world @ Vector(b) for o in obs for b in o.bound_box]
        d = [max(q[i] for q in p) - min(q[i] for q in p) for i in range(3)]
        print(f'[kit] {c.name}: {len(obs)} 件 {d[0]:.1f}x{d[1]:.1f}x{d[2]:.1f} 米')
cam_data = bpy.data.cameras.new('OverviewCam')
cam_data.lens = 35
cam = bpy.data.objects.new('OverviewCam', cam_data)
sc.collection.objects.link(cam)
r = max(size.x, size.y, size.z) * 1.15
cam.location = ctr + Vector((r * 0.75, -r * 0.95, r * 0.65))
cam.rotation_euler = (ctr - cam.location).to_track_quat('-Z', 'Y').to_euler()
cam_data.clip_end = r * 10
sc.camera = cam
sun = bpy.data.objects.new('Sun', bpy.data.lights.new('Sun', 'SUN'))
sun.data.energy = 3.5
sun.rotation_euler = (math.radians(50), 0, math.radians(35))
sc.collection.objects.link(sun)
if not sc.world:
    sc.world = bpy.data.worlds.new('W')
sc.world.color = (0.35, 0.4, 0.48)
if sc.world.use_nodes and sc.world.node_tree.nodes.get('Background'):
    sc.world.node_tree.nodes['Background'].inputs[0].default_value = (0.35, 0.4, 0.48, 1)
    sc.world.node_tree.nodes['Background'].inputs[1].default_value = 1.0
engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = 1280, 720, 100
sc.render.filepath = out
bpy.ops.render.render(write_still=True)
print('[kit] 总览', out, '范围', [round(v, 1) for v in size])
