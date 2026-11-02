"""苏联板楼的下雪：雪花载体网格（纯技术网格，不是美术资源）。一堆很小的四边形（每片 1 厘米，放在 70×70×35 米的盒子里），材质 M_SV_Snow 用世界位置偏移把它们
绕着相机循环、面向相机放大成雪花。这样雪永远跟着相机，不用 C++ / 蓝图；冬季包和天气包的 Cascade 雪只有 15 米见方，要挂在相机上才能用（这张图不改 C++）。
blender -b --factory-startup -P tools/soviet_snowbox.py -- Inbox/_k62/snowbox.fbx 片数
UV0 = 四角 (0,0)-(1,1)，UV1 = 每片的随机数；片的中心在整数厘米上（着色器用 round 还原，同一片四个角算出来的循环位置完全一样，不会在盒子边上撕开）
"""
import random
import sys

import bpy

args = sys.argv[sys.argv.index('--') + 1:]
OUT, N = args[0], int(args[1])
random.seed(62)
HX, HY, HZ = 3500, 3500, 1750   # 厘米（Blender 里按 0.01 缩放成米）
verts, faces, uv0, uv1 = [], [], [], []
for i in range(N):
    x, y, z = random.randint(-HX, HX), random.randint(-HY, HY), random.randint(-HZ, HZ)
    r1, r2 = random.random(), random.random()
    k = len(verts)
    for (u, v) in ((0, 0), (1, 0), (1, 1), (0, 1)):
        verts.append(((x + (u - 0.5)) * 0.01, (y + (v - 0.5)) * 0.01, z * 0.01))
        uv0 += [u, v]
        uv1 += [r1, r2]
    faces.append((k, k + 1, k + 2, k + 3))
bpy.ops.wm.read_factory_settings(use_empty=True)
me = bpy.data.meshes.new('SM_SV_SnowBox')
me.from_pydata(verts, [], faces)
me.update()
a = me.uv_layers.new(name='UVMap')
b = me.uv_layers.new(name='UVRand')
a.data.foreach_set('uv', uv0)
b.data.foreach_set('uv', uv1)
ob = bpy.data.objects.new('SM_SV_SnowBox', me)
bpy.context.scene.collection.objects.link(ob)
bpy.ops.object.select_all(action='DESELECT')
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.fbx(filepath=OUT, use_selection=True, object_types={'MESH'}, apply_unit_scale=True, mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False, axis_forward='-Z', axis_up='Y')
print('[snowbox] 完成', N, flush=True)
