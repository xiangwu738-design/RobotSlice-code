"""下落循环候选的腿部角度（Blender 命令行，不碰 UE）：每个动作 FBX 按 30 帧/秒逐帧算两条腿的膝弯曲（大腿 → 小腿，带正负：往后弯为正，
反折为负；折过 180 度会连续展开）、髋前屈 / 外展、膝侧弯、小腿绕自身扭转、两膝 / 两脚左右间距，打印每个动作的范围和单帧最大变化。
挑「干净的下落循环」用（10-31 换掉 M_Neutral_Jump_Loop_Fall）。
blender -b --factory-startup -P tools/fall_clip_legs.py -- <动作.fbx 通配符> [...]"""
import glob
import math
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
files = []
for pat in args:
    files += sorted(glob.glob(pat, recursive=True))
scene = bpy.context.scene


def ang(a, b):
    return math.degrees(a.angle(b)) if a.length > 1e-6 and b.length > 1e-6 else 0.0


for f in files:
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for a in list(bpy.data.actions):
        bpy.data.actions.remove(a)
    try:
        bpy.ops.import_scene.fbx(filepath=f, automatic_bone_orientation=False)
    except Exception as e:
        print('[legs] 读不了', f, e)
        continue
    arm = next((o for o in bpy.data.objects if o.type == 'ARMATURE'), None)
    if not arm or not arm.animation_data or not arm.animation_data.action:
        print('[legs] 没有动作', f)
        continue
    act = arm.animation_data.action
    f0, f1 = act.frame_range
    src_fps = scene.render.fps / scene.render.fps_base
    n = int(round((f1 - f0) / src_fps * 30))
    W = arm.matrix_world

    def P(b):
        return W @ arm.pose.bones[b].head

    stats = {}
    prev = {}
    unwrap = {}
    rows = []
    for i in range(n + 1):
        fr = f0 + i / 30 * src_fps
        scene.frame_set(int(fr), subframe=fr - int(fr))
        pel = P('pelvis')
        # 骨盆朝向：两髋连线 = 左右，上 = 骨盆 → spine_03
        right = (P('thigh_r') - P('thigh_l')).normalized()
        up = (P('spine_03') - pel).normalized()
        fwd = up.cross(right).normalized()
        # Blender 里的人形朝 -Y：前方按脚尖方向校正一次
        if i == 0:
            toe = (P('ball_l') + P('ball_r')) * 0.5 - (P('foot_l') + P('foot_r')) * 0.5
            sign_f = 1.0 if toe.dot(fwd) >= 0 else -1.0
        fwd = fwd * sign_f
        right = fwd.cross(up).normalized() * -1.0 if fwd.cross(up).dot(right) < 0 else fwd.cross(up).normalized()
        row = {}
        for s, side in (('l', -1.0), ('r', 1.0)):
            hip, knee, ank, ball = P('thigh_' + s), P('calf_' + s), P('foot_' + s), P('ball_' + s)
            th = (knee - hip).normalized()
            ca = (ank - knee).normalized()
            # 膝弯曲：在大腿的矢状面（大腿方向 × 骨盆左右）里量小腿相对大腿转了多少，往后弯为正
            side_ax = right
            a = math.degrees(math.atan2(th.cross(ca).dot(side_ax), th.dot(ca)))
            a = -a  # 往后弯为正
            key = 'knee_' + s
            if key in unwrap:
                d = (a - unwrap[key] + 180.0) % 360.0 - 180.0
                a = unwrap[key] + d
            unwrap[key] = a
            row[key] = a
            # 膝侧弯：小腿偏出大腿矢状面的角度
            row['kside_' + s] = math.degrees(math.asin(max(-1.0, min(1.0, ca.dot(right))))) - math.degrees(math.asin(max(-1.0, min(1.0, th.dot(right)))))
            # 髋前屈：大腿在骨盆前后方向抬起多少（垂直向下 = 0）
            row['hip_' + s] = math.degrees(math.atan2(th.dot(fwd), -th.dot(up)))
            row['abd_' + s] = math.degrees(math.asin(max(-1.0, min(1.0, th.dot(right) * side))))
            # 脚：脚掌方向相对小腿的俯仰
            ft = (ball - ank).normalized()
            row['ank_' + s] = ang(ca, ft)
        row['knee_gap'] = (P('calf_r') - P('calf_l')).dot(right)
        row['foot_gap'] = (P('foot_r') - P('foot_l')).dot(right)
        # 每帧转角：两条腿骨（大腿 / 小腿 / 脚）方向一帧变了多少度（局部抖动）
        cur = {b: (P(c) - P(b)).normalized() for b, c in (('thigh_l', 'calf_l'), ('calf_l', 'foot_l'), ('thigh_r', 'calf_r'), ('calf_r', 'foot_r'))}
        row['step'] = max(ang(cur[b], prev[b]) for b in cur) if prev else 0.0
        prev = cur
        rows.append(row)
    name = f.replace('\\', '/').split('/')[-1]
    keys = ['knee_l', 'knee_r', 'kside_l', 'kside_r', 'hip_l', 'hip_r', 'abd_l', 'abd_r', 'ank_l', 'ank_r', 'knee_gap', 'foot_gap', 'step']
    desc = ' '.join('%s %.0f~%.0f' % (k, min(r[k] for r in rows), max(r[k] for r in rows)) for k in keys)
    print('[legs] %-44s %.2f秒 | %s' % (name, (f1 - f0) / src_fps, desc))
    if '--trace' in sys.argv[-1:]:
        for i, r in enumerate(rows):
            print('    %3d 膝 %4.0f/%4.0f 髋 %4.0f/%4.0f 外展 %4.0f/%4.0f 膝距 %4.0f 脚距 %4.0f 帧转 %4.0f' % (
                i, r['knee_l'], r['knee_r'], r['hip_l'], r['hip_r'], r['abd_l'], r['abd_r'], r['knee_gap'], r['foot_gap'], r['step']))
