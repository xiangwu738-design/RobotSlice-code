"""量模板人形的手（网格）在手坐标系里的尺寸：原点 hand_r 骨头，d = 手腕→中指根，n = 手心法线（朝手心外），k = d×n。
另外把 UE 的 hand_r 局部轴换算出来（Blender 骨头矩阵：UE X = 镜像(列0)，UE Y = -镜像(列1)，UE Z = 镜像(列2)，镜像 = y 取反）。
blender -b --factory-startup -P manny_hand.py -- <SKM_Manny_Simple.fbx>"""
import bpy
import sys
import math
from mathutils import Vector, Matrix

rig = sys.argv[sys.argv.index('--') + 1]
bpy.ops.import_scene.fbx(filepath=rig, automatic_bone_orientation=False)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
mesh = next(o for o in bpy.data.objects if o.type == 'MESH')
dg = bpy.context.evaluated_depsgraph_get()
me = mesh.evaluated_get(dg).to_mesh()
mw = mesh.matrix_world
amw = arm.matrix_world
B = lambda n: amw @ arm.data.bones[n].head_local

for side in ('r',):
    H = B('hand_' + side)
    d = (B('middle_01_' + side) - H).normalized()
    k0 = (B('pinky_01_' + side) - B('index_01_' + side))
    n = d.cross(k0).normalized()
    if n.dot(B('thumb_03_' + side) - H) < 0:
        n = -n
    k = d.cross(n)
    print('[m] d', tuple(round(v, 3) for v in d), 'n', tuple(round(v, 3) for v in n), 'k', tuple(round(v, 3) for v in k))
    to_h = lambda p: Vector(((p - H).dot(k), (p - H).dot(n), (p - H).dot(d))) * 100
    for nm in ['index_01', 'middle_01', 'ring_01', 'pinky_01', 'index_02', 'middle_02', 'middle_03', 'pinky_03', 'thumb_01', 'thumb_02', 'thumb_03',
               'index_metacarpal', 'middle_metacarpal', 'pinky_metacarpal']:
        p = to_h(B(f'{nm}_{side}'))
        print(f'[m]   {nm:18s} k {p.x:6.2f}  n {p.y:6.2f}  d {p.z:6.2f}')
    # 网格：手掌（d 在 1.5–8 厘米、k 在 ±4 厘米）在手心 / 手背方向的厚度，按 d 分段
    vg_idx = {g.index: g.name for g in mesh.vertex_groups}
    hand_verts = []
    for v in me.vertices:
        w = {vg_idx[g.group]: g.weight for g in v.groups}
        if sum(wt for nm, wt in w.items() if nm.endswith('_' + side) and (nm.startswith('hand') or any(nm.startswith(f) for f in ('index', 'middle', 'ring', 'pinky', 'thumb')))) > 0.5:
            hand_verts.append(to_h(mw @ v.co))
    print('[m] 手的顶点', len(hand_verts))
    for d0 in (2, 4, 6, 8, 10, 12, 14, 16, 18):
        S = [p for p in hand_verts if abs(p.z - d0) < 0.6 and abs(p.x) < 4.5]
        if S:
            print(f'[m]   d={d0:2d}: n {min(p.y for p in S):6.2f} ~ {max(p.y for p in S):6.2f}   k {min(p.x for p in S):6.2f} ~ {max(p.x for p in S):6.2f}')
    # UE 局部轴
    hb = arm.data.bones['hand_' + side]
    M = (amw @ hb.matrix_local).to_3x3()
    cols = [M.col[i].normalized() for i in range(3)]
    mir = lambda v: Vector((v.x, -v.y, v.z))
    ue = [mir(cols[0]), -mir(cols[1]), mir(cols[2])]
    print('[m] UE hand 局部 X/Y/Z（UE 坐标）', [tuple(round(c, 3) for c in a) for a in ue])
    print('[m] UE 手方向 d（UE 坐标）', tuple(round(c, 3) for c in mir(d)), ' 手心 n', tuple(round(c, 3) for c in mir(n)))
    # 手坐标系里 UE 局部轴（方便以后在 C++ 里对照）：d / n / k 在 UE 局部的分量
    for nm, v in (('d', d), ('n', n), ('k', k)):
        u = mir(v)
        print(f'[m]   {nm} 在 UE hand 局部 = ({u.dot(ue[0]):.3f}, {u.dot(ue[1]):.3f}, {u.dot(ue[2]):.3f})')
    print('[m] 手腕到中指根', round((B('middle_01_' + side) - H).length * 100, 2), '厘米')
