"""双手武器挂点核对：动作里左手（hand_l）在右手挂的刀的坐标系里在哪。刀柄轴对了的话，左手应该在柄轴上、刀身反方向十几厘米。
blender -b --factory-startup -P check_twohand.py -- 动作.fbx 位置x,y,z 四元数x,y,z,w [更多候选...]"""
import bpy
import sys
from mathutils import Vector, Matrix, Quaternion

a = sys.argv[sys.argv.index('--') + 1:]
path = a[0]
cands = []
for i in range(1, len(a), 2):
    cands.append((Vector([float(v) for v in a[i].split(',')]), [float(v) for v in a[i + 1].split(',')]))
bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
act = arm.animation_data.action
f0, f1 = [int(v) for v in act.frame_range]
S = Matrix.Diagonal((1, -1, 1))
for ci, (pos, q) in enumerate(cands):
    Rue = Quaternion((q[3], q[0], q[1], q[2])).to_matrix()
    rows = []
    for fr in range(f0, f1 + 1, max(1, (f1 - f0) // 6)):
        bpy.context.scene.frame_set(fr)
        pr = arm.pose.bones['hand_r'].matrix
        pl = arm.pose.bones['hand_l'].matrix
        C = pr.to_3x3().normalized() @ S
        t = pr.translation
        o = t + C @ pos
        A = (C @ Rue @ Vector((1, 0, 0))).normalized()
        rel = pl.translation - o
        along = rel.dot(A)
        perp = (rel - A * along).length
        fore = (pr.translation - arm.pose.bones['lowerarm_r'].matrix.translation).normalized()
        rows.append(f'f{fr}: 左手沿刀身 {along:6.1f} 离柄轴 {perp:5.1f}  刀身·前臂 {A.dot(fore):5.2f}')
    print(f'[two] 候选{ci} ' + ' | '.join(rows))
