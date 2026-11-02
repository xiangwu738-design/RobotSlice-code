"""城市图用的两个基本网格：SM_City_Box（1 米立方，原点在中心，世界投影材质用）、SM_City_SignQuad（1×1 米竖直面片，
正面朝本地 -Y，原点在底边中点，UV 0..1 正着贴图）。
blender -b --factory-startup -P tools/blender_k059_basic.py -- Inbox/_k059/export_basic"""
import os
import sys

import bpy

out = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
os.makedirs(out, exist_ok=True)
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
bpy.ops.mesh.primitive_cube_add(size=1)
box = bpy.context.active_object
box.name = box.data.name = 'SM_City_Box'
box.data.materials.append(bpy.data.materials.new('M_Box'))
# 竖直面片：四个点在 XZ 平面，法线朝 -Y
me = bpy.data.meshes.new('SM_City_SignQuad')
me.from_pydata([(-0.5, 0, 0), (0.5, 0, 0), (0.5, 0, 1), (-0.5, 0, 1)], [], [(0, 1, 2, 3)])
uv = me.uv_layers.new(name='UVMap')
for li, (u, v) in zip(range(4), ((0, 0), (1, 0), (1, 1), (0, 1))):
    uv.data[li].uv = (u, v)
me.update()
if me.polygons[0].normal.y > 0:
    me.flip_normals()
me.materials.append(bpy.data.materials.new('M_Sign'))
q = bpy.data.objects.new('SM_City_SignQuad', me)
bpy.context.scene.collection.objects.link(q)
print('[basic] 面片法线', tuple(round(v, 2) for v in me.polygons[0].normal))
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.fbx(filepath=f'{out}/basic.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                         mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False)
print('[basic] →', out)
