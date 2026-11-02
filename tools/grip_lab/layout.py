"""模板人形右手各指关节在手坐标系（k=往食指 / 拇指、n=手心、d=往指尖，厘米）里的位置，打印成 Python 表。
左手核对镜像。"""
import bpy
import sys
from mathutils import Vector

rig = sys.argv[sys.argv.index('--') + 1]
bpy.ops.import_scene.fbx(filepath=rig, automatic_bone_orientation=False)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
amw = arm.matrix_world
B = lambda n: amw @ arm.data.bones[n].head_local
for side in ('r', 'l'):
    H = B('hand_' + side)
    d = (B('middle_01_' + side) - H).normalized()
    n = d.cross(B('pinky_01_' + side) - B('index_01_' + side)).normalized()
    if n.dot(B('thumb_03_' + side) - H) < 0:
        n = -n
    k = d.cross(n)
    if k.dot(B('thumb_01_' + side) - H) < 0:
        k = -k   # 往拇指那边
    to_h = lambda p: tuple(round(v, 2) for v in (((p - H).dot(k)) * 100, ((p - H).dot(n)) * 100, ((p - H).dot(d)) * 100))
    out = {}
    for f in ('thumb', 'index', 'middle', 'ring', 'pinky'):
        pts = [to_h(B(f'{f}_0{j}_{side}')) for j in (1, 2, 3)]
        out[f] = pts
    print(f'[lay] {side} = {out}')
