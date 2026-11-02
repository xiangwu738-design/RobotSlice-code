"""动作挑选用：把 FBX 动作（只有骨架）按时间均匀取几帧，导出主要关节的世界坐标到 json，之后 tools/anim_stick_sheet.py 画成火柴人缩略图。
blender -b --factory-startup -P tools/anim_stick_dump.py -- <输出 json> <帧数> <fbx> [fbx ...]"""
import json
import os
import sys

import bpy

ARGS = sys.argv[sys.argv.index('--') + 1:]
OUT, N, FILES = ARGS[0], int(ARGS[1]), ARGS[2:]
BONES = ['pelvis', 'spine_03', 'spine_05', 'neck_01', 'head', 'clavicle_l', 'upperarm_l', 'lowerarm_l', 'hand_l', 'middle_metacarpal_l',
         'clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r', 'middle_metacarpal_r', 'thigh_l', 'calf_l', 'foot_l', 'ball_l',
         'thigh_r', 'calf_r', 'foot_r', 'ball_r']
result = {}
for f in FILES:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=f, automatic_bone_orientation=False)
    arm = next((o for o in bpy.context.scene.objects if o.type == 'ARMATURE'), None)
    if not arm or not arm.animation_data or not arm.animation_data.action:
        print('[stick] 没有动作', f)
        continue
    act = arm.animation_data.action
    f0, f1 = act.frame_range
    scn = bpy.context.scene
    fps = scn.render.fps / scn.render.fps_base
    frames = []
    for k in range(N):
        fr = f0 + (f1 - f0) * k / max(1, N - 1)
        scn.frame_set(int(fr), subframe=fr - int(fr))
        joints = {}
        for b in BONES:
            pb = arm.pose.bones.get(b)
            if pb:
                p = arm.matrix_world @ pb.head
                joints[b] = [round(p.x, 4), round(p.y, 4), round(p.z, 4)]
        frames.append(joints)
    name = os.path.splitext(os.path.basename(f))[0]
    result[name] = {'length': (f1 - f0) / fps, 'frames': frames}
    print('[stick]', name, round((f1 - f0) / fps, 2))
json.dump(result, open(OUT, 'w', encoding='utf-8'))
print('[stick] 完成', len(result))
