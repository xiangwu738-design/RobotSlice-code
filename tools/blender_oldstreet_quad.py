"""街景贴片用的 100×100 米面片（只有一个四边形）：朝 UE -X，中心在原点，U 沿 UE +Y，V 从上到下。
blender -b --factory-startup -P tools/blender_oldstreet_quad.py -- 输出目录"""
import os
import sys

import bpy

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
os.makedirs(OUT, exist_ok=True)
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
# Blender 坐标 (X, -Y, Z)，UE 读成 (X, Y, Z)
verts = [(0, 50, -50), (0, -50, -50), (0, -50, 50), (0, 50, 50)]
me = bpy.data.meshes.new('SM_OS_Quad')
me.from_pydata(verts, [], [(0, 1, 2, 3)])
me.update()
uv = me.uv_layers.new(name='UVMap')
for l, u in zip(uv.data, [(0, 0), (1, 0), (1, 1), (0, 1)]):
    l.uv = u
ob = bpy.data.objects.new('SM_OS_Quad', me)
bpy.context.scene.collection.objects.link(ob)
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.fbx(filepath=f'{OUT}/SM_OS_Quad.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True, mesh_smooth_type='FACE', use_mesh_modifiers=False,
                         add_leaf_bones=False, bake_anim=False, axis_forward='-Z', axis_up='Y')
print('[oldstreet] 面片好了')
